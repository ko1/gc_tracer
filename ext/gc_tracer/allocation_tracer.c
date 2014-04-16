/*
 * allocation tracer: adds GC::Tracer::start_allocation_tracing
 *
 * By Koichi Sasada
 * created at Thu Apr 17 03:50:38 2014.
 */

#include "ruby/ruby.h"
#include "ruby/debug.h"

struct allocation_info {
    /* all of information don't need marking. */
    int living;
    VALUE flags;
    VALUE klass;

    /* allocation info */
    const char *path;
    unsigned long line;
    const char *class_path;
    VALUE mid;
    size_t generation;

    struct allocation_info *next;
};

struct traceobj_arg {
    int running;
    st_table *aggregate_table;  /* user defined key -> [count, total_age, max_age, min_age] */
    st_table *object_table;     /* obj (VALUE)      -> allocation_info */
    st_table *str_table;        /* cstr             -> refcount */
    struct allocation_info *freed_allocation_info;
};

extern VALUE rb_mGCTracer;

static char *
keep_unique_str(st_table *tbl, const char *str)
{
    st_data_t n;

    if (st_lookup(tbl, (st_data_t)str, &n)) {
	char *result;

	st_insert(tbl, (st_data_t)str, n+1);
	st_get_key(tbl, (st_data_t)str, (st_data_t *)&result);

	return result;
    }
    else {
	return NULL;
    }
}

static const char *
make_unique_str(st_table *tbl, const char *str, long len)
{
    if (!str) {
	return NULL;
    }
    else {
	char *result;

	if ((result = keep_unique_str(tbl, str)) == NULL) {
	    result = (char *)ruby_xmalloc(len+1);
	    strncpy(result, str, len);
	    result[len] = 0;
	    st_add_direct(tbl, (st_data_t)result, 1);
	}
	return result;
    }
}

static void
delete_unique_str(st_table *tbl, const char *str)
{
    if (str) {
	st_data_t n;

	st_lookup(tbl, (st_data_t)str, &n);
	if (n == 1) {
	    st_delete(tbl, (st_data_t *)&str, 0);
	    ruby_xfree((char *)str);
	}
	else {
	    st_insert(tbl, (st_data_t)str, n-1);
	}
    }
}

struct memcmp_key_data {
    const char *path;
    int line;
};

static int
memcmp_hash_compare(st_data_t a, st_data_t b)
{
    struct memcmp_key_data *k1 = (struct memcmp_key_data *)a;
    struct memcmp_key_data *k2 = (struct memcmp_key_data *)b;

    return (k1->path == k2->path && k1->line == k2->line) ? 0 : 1;
}

static st_index_t
memcmp_hash_hash(st_data_t a)
{
    struct memcmp_key_data *k1 = (struct memcmp_key_data *)a;
    return (((st_index_t)k1->path) << 8) & (st_index_t)k1->line;
}

static const struct st_hash_type memcmp_hash_type = {
    memcmp_hash_compare, memcmp_hash_hash
};

static struct traceobj_arg *tmp_trace_arg; /* TODO: Do not use global variables */

static struct traceobj_arg *
get_traceobj_arg(void)
{
    if (tmp_trace_arg == 0) {
	tmp_trace_arg = ALLOC_N(struct traceobj_arg, 1);
	tmp_trace_arg->aggregate_table = st_init_table(&memcmp_hash_type);
	tmp_trace_arg->object_table = st_init_numtable();
	tmp_trace_arg->str_table = st_init_strtable();
	tmp_trace_arg->freed_allocation_info = NULL;
    }
    return tmp_trace_arg;
}

static int
free_keys_i(st_data_t key, st_data_t value, void *data)
{
    ruby_xfree((void *)key);
    return ST_CONTINUE;
}

static int
free_values_i(st_data_t key, st_data_t value, void *data)
{
    ruby_xfree((void *)value);
    return ST_CONTINUE;
}

static int
free_key_values_i(st_data_t key, st_data_t value, void *data)
{
    ruby_xfree((void *)key);
    ruby_xfree((void *)value);
    return ST_CONTINUE;
}

static void
clear_traceobj_arg(void)
{
    struct traceobj_arg * arg = get_traceobj_arg();

    st_foreach(arg->aggregate_table, free_key_values_i, 0);
    st_clear(arg->aggregate_table);
    st_foreach(arg->object_table, free_values_i, 0);
    st_clear(arg->object_table);
    st_foreach(arg->str_table, free_keys_i, 0);
    st_clear(arg->str_table);
}

static struct allocation_info *
create_allocation_info(void)
{
    return (struct allocation_info *)ruby_xmalloc(sizeof(struct allocation_info));
}

static void
free_allocation_info(struct traceobj_arg *arg, struct allocation_info *info)
{
    delete_unique_str(arg->str_table, info->path);
    delete_unique_str(arg->str_table, info->class_path);
    ruby_xfree(info);
}

static void
newobj_i(VALUE tpval, void *data)
{
    struct traceobj_arg *arg = (struct traceobj_arg *)data;
    rb_trace_arg_t *tparg = rb_tracearg_from_tracepoint(tpval);
    VALUE obj = rb_tracearg_object(tparg);
    VALUE path = rb_tracearg_path(tparg);
    VALUE line = rb_tracearg_lineno(tparg);
    VALUE mid = rb_tracearg_method_id(tparg);
    VALUE klass = rb_tracearg_defined_class(tparg);
    struct allocation_info *info;
    const char *path_cstr = RTEST(path) ? make_unique_str(arg->str_table, RSTRING_PTR(path), RSTRING_LEN(path)) : 0;
    VALUE class_path = (RTEST(klass) && !OBJ_FROZEN(klass)) ? rb_class_path_cached(klass) : Qnil;
    const char *class_path_cstr = RTEST(class_path) ? make_unique_str(arg->str_table, RSTRING_PTR(class_path), RSTRING_LEN(class_path)) : 0;

    if (st_lookup(arg->object_table, (st_data_t)obj, (st_data_t *)&info)) {
	if (info->living) {
	    /* do nothing. there is possibility to keep living if FREEOBJ events while suppressing tracing */
	}
	/* reuse info */
	delete_unique_str(arg->str_table, info->path);
	delete_unique_str(arg->str_table, info->class_path);
    }
    else {
	info = create_allocation_info();
    }

    info->next = NULL;
    info->living = 1;
    info->flags = RBASIC(obj)->flags;
    info->klass = RBASIC_CLASS(obj);

    info->path = path_cstr;
    info->line = NUM2INT(line);
    info->mid = mid;
    info->class_path = class_path_cstr;
    info->generation = rb_gc_count();
    st_insert(arg->object_table, (st_data_t)obj, (st_data_t)info);
}

#define MAX_KEY_SIZE 4

void
aggregator_i(void *data)
{
    size_t gc_count = rb_gc_count();
    struct traceobj_arg *arg = (struct traceobj_arg *)data;
    struct allocation_info *info = arg->freed_allocation_info;

    arg->freed_allocation_info = NULL;

    while (info) {
	struct allocation_info *next_info = info->next;
	st_data_t key, val;
	struct memcmp_key_data key_data;
	int *val_buff;
	int age = (int)(gc_count - info->generation);

	key_data.path = info->path;
	key_data.line = info->line;
	key = (st_data_t)&key_data;
	keep_unique_str(arg->str_table, info->path);

	if (st_lookup(arg->aggregate_table, key, &val) == 0) {
	    struct memcmp_key_data *key_buff = ALLOC_N(struct memcmp_key_data, 1);
	    *key_buff = key_data;
	    key = (st_data_t)key_buff;

	    /* count, total age, max age, min age */
	    val_buff = ALLOC_N(int, 4);
	    val_buff[0] = val_buff[1] = 0;
	    val_buff[2] = val_buff[3] = age;

	    st_insert(arg->aggregate_table, (st_data_t)key_buff, (st_data_t)val_buff);
	}
	else {
	    val_buff = (int *)val;
	}

	val_buff[0] += 1;
	val_buff[1] += age;
	if (val_buff[2] > age) val_buff[2] = age;
	if (val_buff[3] < age) val_buff[3] = age;

	free_allocation_info(arg, info);
	info = next_info;
    }
}

static void
move_to_freed_list(struct traceobj_arg *arg, VALUE obj, struct allocation_info *info)
{
    info->next = arg->freed_allocation_info;
    arg->freed_allocation_info = info;
    st_delete(arg->object_table, (st_data_t *)&obj, (st_data_t *)&info);
}

static void
freeobj_i(VALUE tpval, void *data)
{
    struct traceobj_arg *arg = (struct traceobj_arg *)data;
    rb_trace_arg_t *tparg = rb_tracearg_from_tracepoint(tpval);
    VALUE obj = rb_tracearg_object(tparg);
    struct allocation_info *info;

    if (arg->freed_allocation_info == NULL) {
	rb_postponed_job_register_one(0, aggregator_i, arg);
    }

    if (st_lookup(arg->object_table, (st_data_t)obj, (st_data_t *)&info)) {
	move_to_freed_list(arg, obj, info);
    }
}

static void
start_alloc_hooks(VALUE mod)
{
    VALUE newobj_hook, freeobj_hook;
    struct traceobj_arg *arg = get_traceobj_arg();

    if ((newobj_hook = rb_ivar_get(rb_mGCTracer, rb_intern("newobj_hook"))) == Qnil) {
	rb_ivar_set(rb_mGCTracer, rb_intern("newobj_hook"), newobj_hook = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, newobj_i, arg));
	rb_ivar_set(rb_mGCTracer, rb_intern("freeobj_hook"), freeobj_hook = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_FREEOBJ, freeobj_i, arg));
    }
    else {
	freeobj_hook = rb_ivar_get(rb_mGCTracer, rb_intern("freeobj_hook"));
    }

    rb_tracepoint_enable(newobj_hook);
    rb_tracepoint_enable(freeobj_hook);
}

static int
aggregate_result_i(st_data_t key, st_data_t val, void *data)
{
    VALUE result = (VALUE)data;
    int *val_buff = (int *)val;
    struct memcmp_key_data *key_buff = (struct memcmp_key_data *)key;
    VALUE k = rb_ary_new3(2, rb_str_new2(key_buff->path), INT2FIX(key_buff->line));
    VALUE v = rb_ary_new3(4, INT2FIX(val_buff[0]), INT2FIX(val_buff[1]), INT2FIX(val_buff[2]), INT2FIX(val_buff[3]));

    rb_hash_aset(result, k, v);

    return ST_CONTINUE;
}

static int
aggregate_rest_object_i(st_data_t key, st_data_t val, void *data)
{
    struct traceobj_arg *arg = (struct traceobj_arg *)data;
    struct allocation_info *info = (struct allocation_info *)val;
    move_to_freed_list(arg, (VALUE)key, info);
    return ST_CONTINUE;
}

static VALUE
aggregate_result(struct traceobj_arg *arg)
{
    VALUE result = rb_hash_new();
    st_foreach(arg->object_table, aggregate_rest_object_i, (st_data_t)arg);
    aggregator_i(arg);
    st_foreach(arg->aggregate_table, aggregate_result_i, (st_data_t)result);
    clear_traceobj_arg();
    return result;
}

static VALUE
stop_allocation_tracing(VALUE self)
{
    VALUE newobj_hook = rb_ivar_get(rb_mGCTracer, rb_intern("newobj_hook"));
    VALUE freeobj_hook = rb_ivar_get(rb_mGCTracer, rb_intern("freeobj_hook"));

    /* stop hooks */
    if (newobj_hook && freeobj_hook) {
	rb_tracepoint_disable(newobj_hook);
	rb_tracepoint_disable(freeobj_hook);
    }
    else {
	rb_raise(rb_eRuntimeError, "not started yet.");
    }

    return Qnil;
}

VALUE
gc_tracer_stop_allocation_tracing(VALUE self)
{
    stop_allocation_tracing(self);
    return aggregate_result(get_traceobj_arg());
}

VALUE
gc_tracer_start_allocation_tracing(int argc, VALUE *argv, VALUE self)
{
    if (rb_ivar_get(rb_mGCTracer, rb_intern("allocation_tracer")) != Qnil) {
	rb_raise(rb_eRuntimeError, "can't run recursive");
    }
    else {
	start_alloc_hooks(rb_mGCTracer);

	if (rb_block_given_p()) {
	    rb_ensure(rb_yield, Qnil, stop_allocation_tracing, Qnil);
	    return aggregate_result(get_traceobj_arg());
	}
    }

    return Qnil;
}

