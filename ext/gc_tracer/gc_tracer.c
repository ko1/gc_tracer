/*
 * gc_tracer adds GC::Tracer module.
 *
 * By Koichi Sasada
 * created at Wed Feb 26 10:52:59 2014.
 */

#include "ruby/ruby.h"
#include "ruby/debug.h"
#include "gc_tracer.h"
#include <stdio.h>
#include <assert.h>

#ifdef HAVE_RB_OBJ_GC_FLAGS
size_t rb_obj_gc_flags(VALUE obj, ID* flags, size_t max);
static ID id_young;
#endif

struct gc_hooks {
    VALUE hooks[3];
    VALUE enabled;
    void (*funcs[3])(void *data, int event_index);
    void *args[3];
    void *data;
};

static const char *event_names[] = {
    "gc_start",
    "gc_end_m",
    "gc_end_s"
};

/* common funcs */

static void
gc_start_i(VALUE tpval, void *data)
{
    struct gc_hooks *hooks = (struct gc_hooks *)data;
    (*hooks->funcs[0])(hooks->args[0], 0);
}

static void
gc_end_mark_i(VALUE tpval, void *data)
{
    struct gc_hooks *hooks = (struct gc_hooks *)data;
    (*hooks->funcs[1])(hooks->args[1], 1);
}

static void
gc_end_sweep_i(VALUE tpval, void *data)
{
    struct gc_hooks *hooks = (struct gc_hooks *)data;
    (*hooks->funcs[2])(hooks->args[2], 2);
}

static void
create_gc_hooks(struct gc_hooks *hooks)
{
    if (hooks->hooks[0] == 0) {
	int i;

	hooks->hooks[0] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_START,     gc_start_i,     hooks);
	hooks->hooks[1] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_MARK,  gc_end_mark_i,  hooks);
	hooks->hooks[2] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_SWEEP, gc_end_sweep_i, hooks);

	/* mark for GC */
	for (i=0; i<3; i++) rb_gc_register_mark_object(hooks->hooks[i]);
    }
}

static void
start_gc_hooks(struct gc_hooks *hooks)
{
    if (hooks->enabled == Qfalse) {
	int i;
	hooks->enabled = Qtrue;
	for (i=0; i<3; i++) {
	    rb_tracepoint_enable(hooks->hooks[i]);
	}
    }
}

static void
stop_gc_hooks(struct gc_hooks *hooks)
{
    hooks->enabled = Qfalse;

    if (hooks->hooks[0] != 0) {
	rb_tracepoint_disable(hooks->hooks[0]);
	rb_tracepoint_disable(hooks->hooks[1]);
	rb_tracepoint_disable(hooks->hooks[2]);
    }
}

/* logger */

struct gc_hooks logger;
static FILE *gc_trace_out = NULL;
static VALUE gc_trace_items, gc_trace_items_types;

#ifdef HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

enum gc_key_type {
    GC_STAT_KEY,
    GC_LATEST_GC_INFO_KEY,
#ifdef HAVE_GETRUSAGE
    GC_RUSAGE_TIMEVAL_KEY,
    GC_RUSAGE_KEY,
#endif
};

/* the following code is only for internal tuning. */

/* Source code to use RDTSC is quoted and modified from
 * http://www.mcs.anl.gov/~kazutomo/rdtsc.html
 * written by Kazutomo Yoshii <kazutomo@mcs.anl.gov>
 */

#if defined(__GNUC__) && defined(__i386__)
typedef unsigned long long tick_t;

static inline tick_t
tick(void)
{
    unsigned long long int x;
    __asm__ __volatile__ ("rdtsc" : "=A" (x));
    return x;
}

#elif defined(__GNUC__) && defined(__x86_64__)
typedef unsigned long long tick_t;

static __inline__ tick_t
tick(void)
{
    unsigned long hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo)|( ((unsigned long long)hi)<<32);
}

#elif defined(_WIN32) && defined(_MSC_VER)
#include <intrin.h>
typedef unsigned __int64 tick_t;

static inline tick_t
tick(void)
{
    return __rdtsc();
}

#else /* use clock */
typedef clock_t tick_t;
static inline tick_t
tick(void)
{
    return clock();
}
#endif

static tick_t start_tick;

static void
out_str(const char *str)
{
    fprintf(gc_trace_out, "%s\t", str);
}

static void
out_terminate(void)
{
    fprintf(gc_trace_out, "\n");
}

static void
out_sizet(size_t size)
{
    fprintf(gc_trace_out, "%lu\t", (unsigned long)size);
}

static void
out_tick(void)
{
    out_sizet(tick() - start_tick);
}

static void
out_obj(VALUE obj)
{
    if (NIL_P(obj) || obj == Qfalse) {
	out_sizet(0);
    }
    else if (obj == Qtrue) {
	out_sizet(1);
    }
    else if (SYMBOL_P(obj)) {
	out_str(rb_id2name(SYM2ID(obj)));
    }
    else if (FIXNUM_P(obj)) {
	out_sizet(FIX2INT(obj));
    }
    else {
	rb_bug("out_obj: unknown object to output");
    }
}

static void
out_header(void)
{
    int i;

    out_str("tick");
    out_str("type");

    for (i=0; i<RARRAY_LEN(gc_trace_items); i++) {
	out_str(rb_id2name(SYM2ID(RARRAY_AREF(gc_trace_items, i))));
    }

    out_terminate();
}

#ifdef HAVE_GETRUSAGE
struct rusage_cache {
    int cached;
    struct rusage usage;
};

static void
getursage_fill(struct rusage_cache *rusage_cache)
{
    if (!rusage_cache->cached) {
	rusage_cache->cached = 1;
	getrusage(RUSAGE_SELF, &rusage_cache->usage);
    }
}

static double
timeval2double(struct timeval *tv)
{
    return tv->tv_sec * 1000000 + tv->tv_usec;
}

static double
getrusage_timeval(VALUE sym, struct rusage_cache *rusage_cache)
{
    getursage_fill(rusage_cache);

    if (sym == sym_rusage_timeval[0]) {
	return timeval2double(&rusage_cache->usage.ru_utime);
    }
    if (sym == sym_rusage_timeval[1]) {
	return timeval2double(&rusage_cache->usage.ru_stime);
    }

    rb_raise(rb_eRuntimeError, "getrusage_timeval: unknown symbol");
    return 0;
}

static size_t
getrusage_sizet(VALUE sym, struct rusage_cache *rusage_cache)
{
    int i = 0;

    getursage_fill(rusage_cache);

#if HAVE_ST_RU_MAXRSS
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_maxrss;
#endif
#if HAVE_ST_RU_IXRSS
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_ixrss;
#endif
#if HAVE_ST_RU_IDRSS
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_idrss;
#endif
#if HAVE_ST_RU_ISRSS
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_isrss;
#endif
#if HAVE_ST_RU_MINFLT
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_minflt;
#endif
#if HAVE_ST_RU_MAJFLT
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_majflt;
#endif
#if HAVE_ST_RU_NSWAP
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_nswap;
#endif
#if HAVE_ST_RU_INBLOCK
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_inblock;
#endif
#if HAVE_ST_RU_OUBLOCK
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_oublock;
#endif
#if HAVE_ST_RU_MSGSND
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_msgsnd;
#endif
#if HAVE_ST_RU_MSGRCV
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_msgrcv;
#endif
#if HAVE_ST_RU_NSIGNALS
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_nsignals;
#endif
#if HAVE_ST_RU_NVCSW
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_nvcsw;
#endif
#if HAVE_ST_RU_NIVCSW
    if (sym == sym_rusage[i++]) return rusage_cache->usage.ru_nivcsw;
#endif

    rb_raise(rb_eRuntimeError, "getrusage_sizet: unknown symbol");
    return 0;
}
#endif

static void
trace(void *data, int event_index)
{
    const char *type = (const char *)data;
    int i;
#ifdef HAVE_GETRUSAGE
    struct rusage_cache rusage_cache = {0};
#endif

    out_tick();
    out_str(type);

    for (i=0; i<RARRAY_LEN(gc_trace_items); i++) {
	enum gc_key_type type = FIX2INT(RARRAY_AREF(gc_trace_items_types, i));
	VALUE sym = RARRAY_AREF(gc_trace_items, i);

	switch (type) {
	  case GC_STAT_KEY:
	    out_sizet(rb_gc_stat(sym));
	    break;
	  case GC_LATEST_GC_INFO_KEY:
	    out_obj(rb_gc_latest_gc_info(sym));
	    break;
#ifdef HAVE_GETRUSAGE
	  case GC_RUSAGE_TIMEVAL_KEY:
	    out_sizet((size_t)getrusage_timeval(sym, &rusage_cache));
	    break;
	  case GC_RUSAGE_KEY:
	    out_sizet(getrusage_sizet(sym, &rusage_cache));
	    break;
#endif
	  default:
	    rb_bug("xyzzy");
	}
    }

    out_terminate();
}

static VALUE
gc_tracer_stop_logging(VALUE self)
{
    if (logger.enabled) {
	fflush(gc_trace_out);
	if (gc_trace_out != stderr) {
	    fclose(gc_trace_out);
	}
	gc_trace_out = NULL;

	stop_gc_hooks(&logger);
    }

    return Qnil;
}

static VALUE
gc_tracer_start_logging(int argc, VALUE *argv, VALUE self)
{
    if (logger.enabled == Qfalse) {
	int i;

	/* setup with args */
	if (argc == 0) {
	    gc_trace_out = stderr;
	}
	else if (argc == 1) {
	    if (RB_TYPE_P(argv[0], T_STRING)) {
		const char *filename = StringValueCStr(argv[0]);
		if ((gc_trace_out = fopen(filename, "w")) == NULL) {
		    rb_raise(rb_eRuntimeError, "can not open file: %s\n", filename);
		}
	    }
	}
	else {
	    rb_raise(rb_eArgError, "too many arguments");
	}

	out_header();

	for (i=0; i<3; i++) {
	    logger.funcs[i] = trace;
	    logger.args[i] = (void *)event_names[i];
	}

	create_gc_hooks(&logger);
	start_gc_hooks(&logger);

	if (rb_block_given_p()) {
	    rb_ensure(rb_yield, Qnil, gc_tracer_stop_logging, Qnil);
	}
    }

    return Qnil;
}

static enum gc_key_type
item_type(VALUE sym)
{
    int i;
    for (i=0; i<(int)(sizeof(sym_gc_stat)/sizeof(VALUE)); i++) {
	if (sym_gc_stat[i] == sym) return GC_STAT_KEY;
    }
    for (i=0; i<(int)(sizeof(sym_latest_gc_info)/sizeof(VALUE)); i++) {
	if (sym_latest_gc_info[i] == sym) return GC_LATEST_GC_INFO_KEY;
    }
#ifdef HAVE_GETRUSAGE
    for (i=0; i<(int)(sizeof(sym_rusage_timeval)/sizeof(VALUE)); i++) {
	if (sym_rusage_timeval[i] == sym) return GC_RUSAGE_TIMEVAL_KEY;
    }
    for (i=0; i<(int)(sizeof(sym_rusage)/sizeof(VALUE)); i++) {
	if (sym_rusage[i] == sym) return GC_RUSAGE_KEY;
    }
#endif
    rb_raise(rb_eArgError, "Unknown key type");
    return 0; /* unreachable */
}

static VALUE
gc_tracer_setup_logging(VALUE self, VALUE ary)
{
    int i;
    VALUE keys = rb_ary_new(), types = rb_ary_new();

    if (NIL_P(ary)) { /* revert all settings */
	int i;

#define ADD(syms) for (i=0; i<(int)(sizeof(syms)/sizeof(VALUE));i++) { \
    rb_ary_push(gc_trace_items, syms[i]); \
    rb_ary_push(gc_trace_items_types, INT2FIX(item_type(syms[i]))); \
}
	ADD(sym_gc_stat);
	ADD(sym_latest_gc_info);
#ifdef HAVE_GETRUSAGE
	ADD(sym_rusage_timeval);
	ADD(sym_rusage);
#endif
    }
    else {
	if (!RB_TYPE_P(ary, T_ARRAY)) rb_raise(rb_eArgError, "unsupported argument");

	for (i=0; i<RARRAY_LEN(ary); i++) {
	    VALUE sym = RARRAY_AREF(ary, i);
	    enum gc_key_type type;
	    if (!SYMBOL_P(sym)) rb_raise(rb_eArgError, "unsupported type");
	    type = item_type(sym);
	    rb_ary_push(keys, sym);
	    rb_ary_push(types, INT2FIX(type));
	}

	rb_ary_replace(gc_trace_items, keys);
	rb_ary_replace(gc_trace_items_types, types);
    }
    return Qnil;
}

#ifdef HAVE_RB_OBJSPACE_EACH_OBJECTS_WITHOUT_SETUP
/* image logging */

/* secret API */
void rb_objspace_each_objects_without_setup(int (*callback)(void *, void *, size_t, void *), void *data);

static struct gc_hooks objspace_recorder;
static int (*objspace_recorder_color_picker)(VALUE);
static int HEAP_OBJ_LIMIT;

struct objspace_recording_data {
    FILE *fp;
    int width, height;
};

struct picker_description {
    const char *name;
    const int color;
};

static void
set_color(unsigned char *buff, int color)
{
    buff[2] = (color >>  0) & 0xff;
    buff[1] = (color >>  8) & 0xff;
    buff[0] = (color >> 16) & 0xff;
}

const int categorical_color10_colors[] = {
    0x1f77b4,
    0xff7f0e,
    0x2ca02c,
    0xd62728,
    0x9467bd,
    0x8c564b,
    0xe377c2,
    0x7f7f7f,
    0xbcbd22,
    0x17becf
};

static int
categorical_color10(int n)
{
    assert(n < 10);
    return categorical_color10_colors[n];
}

const int categorical_color20_colors[] = {
    0x1f77b4,
    0xaec7e8,
    0xff7f0e,
    0xffbb78,
    0x2ca02c,
    0x98df8a,
    0xd62728,
    0xff9896,
    0x9467bd,
    0xc5b0d5,
    0x8c564b,
    0xc49c94,
    0xe377c2,
    0xf7b6d2,
    0x7f7f7f,
    0xc7c7c7,
    0xbcbd22,
    0xdbdb8d,
    0x17becf,
    0x9edae5,
};

static int
categorical_color20(int n)
{
    assert(n < 20);
    return categorical_color20_colors[n];
}

static struct picker_description object_age_picker_description[] = {
    {"empty slot",   0},
    {"infant slot",  0x1f77b4},
    {"young slot",   0xff7f0e},
    {"old slot",     0x2ca02c},
    {"shady slot",   0xd62728}
};

static int
object_age_picker(VALUE v) {
    if (RB_TYPE_P(v, T_NONE)) {
	return 0;
    }
    else {
	if (!OBJ_PROMOTED(v)) {
	    if (OBJ_WB_PROTECTED(v)) {
		return categorical_color10(0); /* infant */
	    }
	    else {
		return categorical_color10(3); /* shady */
	    }
	}
	else {
	    int is_young = 0;
#ifdef HAVE_RB_OBJ_GC_FLAGS
	    ID ids[8];
	    size_t i, count = rb_obj_gc_flags(v, ids, 8);

	    for (i=0; i<count; i++) {
		if (ids[i] == id_young) {
		    is_young = 1;
		    break;
		}
	    }
#endif
	    if (is_young) {
		return categorical_color10(1); /* young */
	    }
	    else {
		return categorical_color10(2); /* old */
	    }
	}
    }
}

static struct picker_description object_type_picker_description[] = {
    {"RUBY_T_NONE", 0},
    {"RUBY_T_OBJECT", 0x1f77b4},
    {"RUBY_T_CLASS", 0xaec7e8},
    {"RUBY_T_MODULE", 0xff7f0e},
    {"RUBY_T_FLOAT", 0xffbb78},
    {"RUBY_T_STRING", 0x2ca02c},
    {"RUBY_T_REGEXP", 0x98df8a},
    {"RUBY_T_ARRAY", 0xd62728},
    {"RUBY_T_HASH", 0xff9896},
    {"RUBY_T_STRUCT", 0x9467bd},
    {"RUBY_T_BIGNUM", 0xc5b0d5},
    {"RUBY_T_FILE", 0x8c564b},
    {"RUBY_T_DATA", 0xc49c94},
    {"RUBY_T_MATCH", 0xe377c2},
    {"RUBY_T_COMPLEX", 0xf7b6d2},
    {"RUBY_T_RATIONAL", 0x7f7f7f},
    {"RUBY_T_NODE", 0xc7c7c7},
    {"RUBY_T_ICLASS", 0xbcbd22},
    {"RUBY_T_ZOMBIE", 0xdbdb8d},
};

static int
object_type_picker(VALUE v) {
    int type = BUILTIN_TYPE(v);
    int color = 0;
    switch (type) {
      case RUBY_T_NONE:
	return 0;
      case RUBY_T_OBJECT:
      case RUBY_T_CLASS:
      case RUBY_T_MODULE:
      case RUBY_T_FLOAT:
      case RUBY_T_STRING:
      case RUBY_T_REGEXP:
      case RUBY_T_ARRAY:
      case RUBY_T_HASH:
      case RUBY_T_STRUCT:
      case RUBY_T_BIGNUM:
      case RUBY_T_FILE:
      case RUBY_T_DATA:
      case RUBY_T_MATCH:
      case RUBY_T_COMPLEX:
      case RUBY_T_RATIONAL: /* 0x0f */
	color = type - 1;
	break;
      case RUBY_T_NODE:
      case RUBY_T_ICLASS:
      case RUBY_T_ZOMBIE:
	color = type - 12;
	break;
      default:
	rb_bug("object_type_picker: unreachable (type: %d)", type);
    }
    return categorical_color20(color);
}

static int
objspace_recording_i(void *start, void *end, size_t stride, void *data)
{
    struct objspace_recording_data *rd = (struct objspace_recording_data *)data;
    const int size = ((VALUE)end - (VALUE)start) / stride;
    unsigned char *buff = ALLOCA_N(unsigned char, size * 3);
    int i, write_result;

    for (i=0; i<size; i++) {
	VALUE v = (VALUE)start + i * stride;
	set_color(&buff[i*3], (*objspace_recorder_color_picker)(v));
    }
    for (; i<HEAP_OBJ_LIMIT; i++) {
	set_color(&buff[i*3], 0);
    }

    write_result = fwrite(buff, 3, HEAP_OBJ_LIMIT, rd->fp);
    rd->height++;

    if (0) {
	fprintf(stderr, "width: %d, write: %d\n", HEAP_OBJ_LIMIT, write_result);
    }

    return 0;
}

static void
objspace_recording(void *data, int event_index)
{
    /* const char *event_name = (const char *)data; */
    const char *dirname = (const char *)objspace_recorder.data;
    char filename[1024];
    FILE *fp;
    struct objspace_recording_data rd;

    snprintf(filename, 1024, "%s/ppm/%08d.%d.ppm", dirname, (int)rb_gc_count(), event_index);

    if ((fp = fopen(filename, "w")) == NULL) {
	rb_bug("objspace_recording: unable to open file: %s", filename);
    }
    rd.fp = fp;

    /* making dummy header */
    /*           MG width heigt dep */
    fprintf(fp, "P6 wwwww hhhhh 255 ");

    rd.width = HEAP_OBJ_LIMIT;
    rd.height = 0;
    rb_objspace_each_objects_without_setup(objspace_recording_i, &rd);

    /* fill width and height */
    fseek(fp, 3, SEEK_SET);
    fprintf(fp, "%5d %5d", rd.width, rd.height);
    fclose(fp);
}

static VALUE
gc_tracer_stop_objspace_recording(VALUE self)
{
    if (objspace_recorder.enabled) {
	ruby_xfree(objspace_recorder.data);
	stop_gc_hooks(&objspace_recorder);
    }
    return Qnil;
}

static void
puts_color_description(VALUE dirname, struct picker_description desc[], int n)
{
    char buff[0x200];
    FILE *fp;
    int i;

    snprintf(buff, 0x200, "%s/color_description.txt", RSTRING_PTR(dirname));

    if ((fp = fopen(buff, "w")) == NULL) {
	rb_raise(rb_eRuntimeError, "puts_color_description: failed to open file");
    }

    for (i=0; i<n; i++) {
	fprintf(fp, "%s\t#%06x\n", desc[i].name, desc[i].color);
    }

    fclose(fp);
}


static VALUE
gc_tracer_start_objspace_recording(int argc, VALUE *argv, VALUE self)
{
    if (objspace_recorder.enabled == Qfalse) {
	int i;
	VALUE ppmdir;
	VALUE dirname;
	VALUE picker_type = ID2SYM(rb_intern("age"));

	switch (argc) {
	  case 2:
	    picker_type = argv[1];
	  case 1:
	    dirname = argv[0];
	    break;
	  default:
	    rb_raise(rb_eArgError, "expect 1 or 2 arguments, but %d", argc);
	}

	/* setup */
	if (rb_funcall(rb_cFile, rb_intern("directory?"), 1, dirname) != Qtrue) {
	    rb_funcall(rb_cDir, rb_intern("mkdir"), 1, dirname);
	}
	if (rb_funcall(rb_cFile, rb_intern("directory?"), 1, (ppmdir = rb_str_plus(dirname, rb_str_new2("/ppm")))) != Qtrue) {
	    rb_funcall(rb_cDir, rb_intern("mkdir"), 1, ppmdir);
	}

	if (picker_type == ID2SYM(rb_intern("age"))) {
	    objspace_recorder_color_picker = object_age_picker;
	    puts_color_description(dirname, &object_age_picker_description[0], sizeof(object_age_picker_description) / sizeof(struct picker_description));
	}
	else if (picker_type == ID2SYM(rb_intern("type"))) {
	    objspace_recorder_color_picker = object_type_picker;
	    puts_color_description(dirname, &object_type_picker_description[0], sizeof(object_type_picker_description) / sizeof(struct picker_description));
	}
	else {
	    rb_raise(rb_eArgError, "unsupported picker type: %s", rb_id2name(SYM2ID(picker_type)));
	}

	HEAP_OBJ_LIMIT = FIX2INT(rb_hash_aref(
	    rb_const_get(rb_mGC, rb_intern("INTERNAL_CONSTANTS")),
	    ID2SYM(rb_intern("HEAP_OBJ_LIMIT"))));

	for (i=0; i<3; i++) {
	    objspace_recorder.funcs[i] = objspace_recording;
	    objspace_recorder.args[i]  = (void *)event_names[i];
	}

	objspace_recorder.data = ruby_xmalloc(RSTRING_LEN(dirname) + 1);
	strcpy((char *)objspace_recorder.data, RSTRING_PTR(dirname));

	create_gc_hooks(&objspace_recorder);
	start_gc_hooks(&objspace_recorder);

	if (rb_block_given_p()) {
	    rb_ensure(rb_yield, Qnil, gc_tracer_stop_objspace_recording, Qnil);
	}
    }
    else {
	rb_raise(rb_eRuntimeError, "recursive recording is not permitted");
    }

    return Qnil;
}

#endif /* HAVE_RB_OBJSPACE_EACH_OBJECTS_WITHOUT_SETUP */

/**
 * GC::Tracer traces GC/ObjectSpace behavior.
 *
 * == Logging
 *
 * GC::Tracer.start_logging(filename) prints GC/ObjectSpace
 * information such as GC.stat/GC.latest_gc_info and the result
 * of getrlimit() (if supported) into specified `filename' on
 * each GC events.
 *
 * GC events are "start marking", "end of marking" and
 * "end of sweeping". You should need to care about lazy sweep.
 *
 * == ObjectSpace recorder
 *
 * This feature needs latest Ruby versions (2.2, and later).
 */
void
Init_gc_tracer(void)
{
    VALUE mod = rb_define_module_under(rb_mGC, "Tracer");

    /* logging methods */
    rb_define_module_function(mod, "start_logging", gc_tracer_start_logging, -1);
    rb_define_module_function(mod, "stop_logging", gc_tracer_stop_logging, 0);
    rb_define_module_function(mod, "setup_logging", gc_tracer_setup_logging, 1);

    /* recording methods */
#ifdef HAVE_RB_OBJSPACE_EACH_OBJECTS_WITHOUT_SETUP
    rb_define_module_function(mod, "start_objspace_recording", gc_tracer_start_objspace_recording, -1);
    rb_define_module_function(mod, "stop_objspace_recording", gc_tracer_stop_objspace_recording, 0);
#endif

    /* setup default banners */
    setup_gc_trace_symbols();
    start_tick = tick();

    /* warm up */
    rb_gc_latest_gc_info(ID2SYM(rb_intern("gc_by")));
    rb_gc_stat(ID2SYM(rb_intern("count")));
#ifdef HAVE_RB_OBJ_GC_FLAGS
    rb_obj_gc_flags(rb_cObject, NULL, 0);
    id_young = rb_intern("young");
#endif

    gc_trace_items = rb_ary_new();
    gc_trace_items_types = rb_ary_new();
    rb_gc_register_mark_object(gc_trace_items);
    rb_gc_register_mark_object(gc_trace_items_types);

    gc_tracer_setup_logging(Qnil, Qnil);
}
