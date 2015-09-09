/*
 * GC::Tracer::*_logging methods
 */

#include <ruby/ruby.h>
#include <ruby/debug.h>

#include <time.h>

#ifdef HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include "gc_tracer.h"

#if defined(__GNUC__) && defined(__i386__)
typedef unsigned long long tick_t;
#define PRItick "llu"

static inline tick_t
tick(void)
{
    unsigned long long int x;
    __asm__ __volatile__ ("rdtsc" : "=A" (x));
    return x;
}

#elif defined(__GNUC__) && defined(__x86_64__)
typedef unsigned long long tick_t;
#define PRItick "llu"

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
#define PRItick "llu"

static inline tick_t
tick(void)
{
    return __rdtsc();
}

#else /* use clock */
typedef clock_t tick_t;
static inline tick_t
#define PRItick "llu"

tick(void)
{
    return clock();
}
#endif

#ifdef RUBY_INTERNAL_EVENT_GC_ENTER
#define MAX_HOOKS 7
#else
#define MAX_HOOKS 5
#endif

#define MAX_CUSTOM_FIELDS 128

static VALUE tracer_hooks[MAX_HOOKS];
static VALUE tracer_acceptable_events[MAX_HOOKS];

typedef void (*out_time_func_t)(FILE *);

struct gc_logging {
    struct config {
	out_time_func_t out_time_func;
	int log_gc_stat;
	int log_gc_latest_gc_info;
	int log_rusage;
	int log_custom_fields_count;
    } config;

    int enabled;
    VALUE enable_hooks[MAX_HOOKS]; /* Symbols */
    int enable_hooks_num; /* 0 to MAX_HOOKS */

    ID custom_field_names[MAX_CUSTOM_FIELDS];
    long  custom_field_values[MAX_CUSTOM_FIELDS];

    FILE *out;
    const char *event;

#if HAVE_GETRUSAGE
    int filled_rusage;
    struct rusage rusage;
#endif
} trace_logging;

static void logging_start_i(VALUE tpval, struct gc_logging *logging);

#define TRACE_FUNC(name) trace_func_##name

#define DEFINE_TRACE_FUNC(name) \
  static void TRACE_FUNC(name)(VALUE tpval, void *data) { \
      struct gc_logging *logging = (struct gc_logging *)data; \
      logging->event = #name; \
      logging_start_i(tpval, logging);\
  }

DEFINE_TRACE_FUNC(start);
DEFINE_TRACE_FUNC(end_mark);
DEFINE_TRACE_FUNC(end_sweep);
DEFINE_TRACE_FUNC(newobj);
DEFINE_TRACE_FUNC(freeobj);
#ifdef RUBY_INTERNAL_EVENT_GC_ENTER
DEFINE_TRACE_FUNC(enter);
DEFINE_TRACE_FUNC(exit);
#endif

/* the following code is only for internal tuning. */

/* Source code to use RDTSC is quoted and modified from
 * http://www.mcs.anl.gov/~kazutomo/rdtsc.html
 * written by Kazutomo Yoshii <kazutomo@mcs.anl.gov>
 */

static void
out_str(FILE *out, const char *str)
{
    fprintf(out, "%s\t", str);
}

static void
out_terminate(FILE *out)
{
    fprintf(out, "\n");
}

static void
out_flush(FILE *out)
{
    fflush(out);
}

static void
out_sizet(FILE *out, size_t size)
{
    fprintf(out, "%lu\t", (unsigned long)size);
}

static void
out_long(FILE *out, long l)
{
    fprintf(out, "%ld\t", l);
}

static double
timeval2double(struct timeval *tv)
{
    return tv->tv_sec * 1000000LL + tv->tv_usec;
}

static void
fill_rusage(struct gc_logging *logging)
{
    if (logging->filled_rusage == 0) {
	logging->filled_rusage = 1;
	getrusage(RUSAGE_SELF, &logging->rusage);
    }
}

static void
out_time_none(FILE *out)
{
    out_sizet(out, 0);
}

static void
out_time_hw_counter(FILE *out)
{
    out_sizet(out, (size_t)tick());
}

static void
out_time_time(FILE *out)
{
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);

    if (tv.tv_sec != 0) {
	fprintf(out, "%lu%06lu\t", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
    }
    else {
	fprintf(out, "%lu\t", (unsigned long)tv.tv_usec);
    }
}

#ifdef HAVE_CLOCK_GETTIME
static void
out_time_nano_time(FILE *out)
{
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    if (ts.tv_sec != 0) {
	fprintf(out, "%lu%09lu\t", (unsigned long)ts.tv_sec, (unsigned long)ts.tv_nsec);
    }
    else {
	fprintf(out, "%lu\t", (unsigned long)ts.tv_nsec);
    }
}
#endif

static void
out_obj(FILE *out, VALUE obj)
{
    if (NIL_P(obj) || obj == Qfalse) {
	out_sizet(out, 0);
    }
    else if (obj == Qtrue) {
	out_sizet(out, 1);
    }
    else if (SYMBOL_P(obj)) {
	out_str(out, rb_id2name(SYM2ID(obj)));
    }
    else if (FIXNUM_P(obj)) {
	out_sizet(out, FIX2INT(obj));
    }
    else {
	rb_bug("out_obj: unknown object to output");
    }
}

static void
out_gc_stat(struct gc_logging *logging)
{
    int i;
    for (i=0; i<(int)(sizeof(sym_gc_stat)/sizeof(VALUE)); i++) {
	VALUE sym = sym_gc_stat[i];
	out_sizet(logging->out, rb_gc_stat(sym));
    }
}

static void
out_gc_latest_gc_info(struct gc_logging *logging)
{
    int i;
    for (i=0; i<(int)(sizeof(sym_latest_gc_info)/sizeof(VALUE)); i++) {
	VALUE sym = sym_latest_gc_info[i];
	out_obj(logging->out, rb_gc_latest_gc_info(sym));
    }
}

static void
out_rusage(struct gc_logging *logging)
{
    fill_rusage(logging);

    out_sizet(logging->out, (size_t)timeval2double(&logging->rusage.ru_utime));
    out_sizet(logging->out, (size_t)timeval2double(&logging->rusage.ru_stime));

#if HAVE_ST_RU_MAXRSS
    out_sizet(logging->out, logging->rusage.ru_maxrss);
#endif
#if HAVE_ST_RU_IXRSS
    out_sizet(logging->out, logging->rusage.ru_ixrss);
#endif
#if HAVE_ST_RU_IDRSS
    out_sizet(logging->out, logging->rusage.ru_idrss);
#endif
#if HAVE_ST_RU_ISRSS
    out_sizet(logging->out, logging->rusage.ru_isrss);
#endif
#if HAVE_ST_RU_MINFLT
    out_sizet(logging->out, logging->rusage.ru_minflt);
#endif
#if HAVE_ST_RU_MAJFLT
    out_sizet(logging->out, logging->rusage.ru_majflt);
#endif
#if HAVE_ST_RU_NSWAP
    out_sizet(logging->out, logging->rusage.ru_nswap);
#endif
#if HAVE_ST_RU_INBLOCK
    out_sizet(logging->out, logging->rusage.ru_inblock);
#endif
#if HAVE_ST_RU_OUBLOCK
    out_sizet(logging->out, logging->rusage.ru_oublock);
#endif
#if HAVE_ST_RU_MSGSND
    out_sizet(logging->out, logging->rusage.ru_msgsnd);
#endif
#if HAVE_ST_RU_MSGRCV
    out_sizet(logging->out, logging->rusage.ru_msgrcv);
#endif
#if HAVE_ST_RU_NSIGNALS
    out_sizet(logging->out, logging->rusage.ru_nsignals);
#endif
#if HAVE_ST_RU_NVCSW
    out_sizet(logging->out, logging->rusage.ru_nvcsw);
#endif
#if HAVE_ST_RU_NIVCSW
    out_sizet(logging->out, logging->rusage.ru_nivcsw);
#endif

}

static void
out_stat(struct gc_logging *logging, const char *event)
{
#if HAVE_GETRUSAGE
    logging->filled_rusage = 0;
#endif

    out_str(logging->out, event);
    (logging->config.out_time_func)(logging->out);

    if (logging->config.log_gc_stat) out_gc_stat(logging);
    if (logging->config.log_gc_latest_gc_info) out_gc_latest_gc_info(logging);
#if HAVE_GETRUSAGE
    if (logging->config.log_rusage) out_rusage(logging);
#endif
    if (logging->config.log_custom_fields_count > 0) {
	int i;
	for (i=0; i<logging->config.log_custom_fields_count; i++) {
	    out_long(logging->out, logging->custom_field_values[i]);
	}
    }

    out_terminate(logging->out);
}

static void
logging_start_i(VALUE tpval, struct gc_logging *logging)
{
    out_stat(logging, logging->event);
}

static void
out_header_each(struct gc_logging *logging, VALUE *syms, int n)
{
    int i;
    for (i=0; i<n; i++) {
	out_obj(logging->out, syms[i]);
    }
}

static void
out_header(struct gc_logging *logging)
{
    out_str(logging->out, "type");
    out_str(logging->out, "tick");

    if (logging->config.log_gc_stat)           out_header_each(logging, sym_gc_stat, sizeof(sym_gc_stat)/sizeof(VALUE));
    if (logging->config.log_gc_latest_gc_info) out_header_each(logging, sym_latest_gc_info, sizeof(sym_latest_gc_info)/sizeof(VALUE));
#if HAVE_GETRUSAGE
    if (logging->config.log_rusage) {
	out_header_each(logging, sym_rusage_timeval, sizeof(sym_rusage_timeval)/sizeof(VALUE));
	out_header_each(logging, sym_rusage, sizeof(sym_rusage)/sizeof(VALUE));
    }
#endif
    if (logging->config.log_custom_fields_count > 0) {
	int i;
	for (i=0; i<logging->config.log_custom_fields_count; i++) {
	    out_str(logging->out, rb_id2name(logging->custom_field_names[i]));
	}
    }
    out_terminate(logging->out);
}

static void
close_output(FILE *out)
{
    fflush(out);
    if (out != stderr) {
	fclose(out);
    }
}

static void
create_gc_hooks(void)
{
    int i = 0;
    struct gc_logging *logging = &trace_logging;

    tracer_hooks[0] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_START,     TRACE_FUNC(start),     logging);
    tracer_hooks[1] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_MARK,  TRACE_FUNC(end_mark),  logging);
    tracer_hooks[2] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_SWEEP, TRACE_FUNC(end_sweep), logging);
    tracer_hooks[3] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ,       TRACE_FUNC(newobj),    logging);
    tracer_hooks[4] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_FREEOBJ,      TRACE_FUNC(freeobj),   logging);

#ifdef RUBY_INTERNAL_EVENT_GC_ENTER
    tracer_hooks[5] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_ENTER,     TRACE_FUNC(enter),     logging);
    tracer_hooks[6] = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_EXIT,      TRACE_FUNC(exit),      logging);
#endif

    /* mark for GC */
    for (i=0; i<MAX_HOOKS; i++) rb_gc_register_mark_object(tracer_hooks[i]);

    tracer_acceptable_events[0] = ID2SYM(rb_intern("start"));
    tracer_acceptable_events[1] = ID2SYM(rb_intern("end_mark"));
    tracer_acceptable_events[2] = ID2SYM(rb_intern("end_sweep"));
    tracer_acceptable_events[3] = ID2SYM(rb_intern("newobj"));
    tracer_acceptable_events[4] = ID2SYM(rb_intern("freeobj"));
#ifdef RUBY_INTERNAL_EVENT_GC_ENTER
    tracer_acceptable_events[5] = ID2SYM(rb_intern("enter"));
    tracer_acceptable_events[6] = ID2SYM(rb_intern("exit"));
#endif
}

static void
enable_gc_hooks(struct gc_logging *logging)
{
    int i;
    for (i=0; i<logging->enable_hooks_num; i++) {
	rb_tracepoint_enable(logging->enable_hooks[i]);
    }
}

static void
disable_gc_hooks(struct gc_logging *logging)
{
    int i;
    for (i=0; i<logging->enable_hooks_num; i++) {
	rb_tracepoint_disable(logging->enable_hooks[i]);
    }
}

static VALUE
gc_tracer_setup_logging_out(VALUE self, VALUE filename)
{
    struct gc_logging *logging = &trace_logging;

    if (NIL_P(filename)) {
	logging->out = stderr;
    }
    else {
	filename = rb_check_string_type(filename);
	{
	    const char *fname = StringValueCStr(filename);
	    FILE *out;

	    if ((out = fopen(fname, "w")) == NULL) {
		rb_raise(rb_eRuntimeError, "can not open file: %s\n", fname);
	    }
	    logging->out = out;
	}
    }

    return self;
}

static VALUE
gc_tracer_setup_logging_events(int argc, VALUE *argv, VALUE self)
{
    struct gc_logging *logging = &trace_logging;
    int i, n;
    int event_bits = 0;

    if (argc == 0) {
	/* deafult: start, end_mark, end_sweep */
	for (i=0; i<3; i++) {
	    event_bits = 0x07; /* 0b0111 */
	}
    }
    else {
	int j;
	for (i=0; i<argc; i++) {
	    for (j=0; j<MAX_HOOKS; j++) {
		if (tracer_acceptable_events[j] == argv[i]) {
		    event_bits |= 0x01 << j;
		    goto found;
		}
	    }
	    rb_raise(rb_eArgError, "unsupported GC trace event: %"PRIsVALUE, argv[i]);
	  found:;
	}
    }

    for (n=0, i=0; i<MAX_HOOKS; i++) {
	if (event_bits & (0x01 << i)) {
	    logging->enable_hooks[n++] = tracer_hooks[i];
	}
    }
    logging->enable_hooks_num = n;

    return self;
}

static VALUE
gc_tracer_setup_logging_tick_type(VALUE self, VALUE sym)
{
    struct gc_logging *logging = &trace_logging;
    static const char *names[] = {
	"none",
	"hw_counter",
	"time"
#ifdef HAVE_CLOCK_GETTIME
	  ,
	"nano_time"
#endif
    };
    static const out_time_func_t funcs[] = {
	out_time_none,
	out_time_hw_counter,
	out_time_time
#ifdef HAVE_CLOCK_GETTIME
	  ,
	out_time_nano_time
#endif
    };
    int i;

    for (i=0; i<(int)(sizeof(funcs)/sizeof(out_time_func_t)); i++) {
	if (sym == ID2SYM(rb_intern(names[i]))) {
	    logging->config.out_time_func = funcs[i];
	    return self;
	}
    }

    rb_raise(rb_eArgError, "unknown tick type: %"PRIsVALUE, sym);
    return self; /* unreachable */
}

static VALUE
gc_tracer_setup_logging_gc_stat(VALUE self, VALUE b)
{
    struct gc_logging *logging = &trace_logging;


    if (RTEST(b)) {
	logging->config.log_gc_stat = Qtrue;
    }
    else {
	logging->config.log_gc_stat = Qfalse;
    }

    return self;
}

static VALUE
gc_tracer_setup_logging_gc_latest_gc_info(VALUE self, VALUE b)
{
    struct gc_logging *logging = &trace_logging;


    if (RTEST(b)) {
	logging->config.log_gc_latest_gc_info = Qtrue;
    }
    else {
	logging->config.log_gc_latest_gc_info = Qfalse;
    }

    return self;
}

static VALUE
gc_tracer_setup_logging_rusage(VALUE self, VALUE b)
{
    struct gc_logging *logging = &trace_logging;


    if (RTEST(b)) {
	logging->config.log_rusage = Qtrue;
    }
    else {
	logging->config.log_rusage = Qfalse;
    }

    return self;
}

static VALUE
gc_tracer_setup_logging_custom_fields(VALUE self, VALUE b)
{
    struct gc_logging *logging = &trace_logging;

    if (RTEST(b)) {
	VALUE ary = rb_check_array_type(b);
	int i;

	logging->config.log_custom_fields_count = RARRAY_LEN(ary);

	for (i=0; i<RARRAY_LEN(ary); i++) {
	    VALUE name = RARRAY_AREF(ary, i);
	    ID field_id = rb_to_id(name);

	    if (i >= MAX_CUSTOM_FIELDS) {
		rb_raise(rb_eRuntimeError, "Number of custome fields exceeds %d", MAX_CUSTOM_FIELDS);
	    }

	    logging->custom_field_names[i] = field_id;
	    logging->custom_field_values[i] = 0;
	}
    }
    else {
	logging->config.log_custom_fields_count = 0;
    }

    return self;
}

static long *
custom_field_value_place(VALUE name)
{
    struct gc_logging *logging = &trace_logging;
    int index;

    if (FIXNUM_P(name)) {
	index = FIX2INT(name);
	if (index >= logging->config.log_custom_fields_count) {
	    rb_raise(rb_eRuntimeError, "Only %d custom fields are available, but %d was specified",
		     logging->config.log_custom_fields_count, index);
	}
    }
    else {
	int i;
	ID nid = rb_to_id(name);
	for (i=0; i<logging->config.log_custom_fields_count; i++) {
	    if (nid == logging->custom_field_names[i]) {
		index = i;
		goto found;
	    }
	}
	rb_raise(rb_eRuntimeError, "Unkown custom fileds is specified.");
    }
  found:
    return &logging->custom_field_values[index];
}

static VALUE
gc_tracer_custom_field_increment(VALUE self, VALUE name)
{
    long *valp = custom_field_value_place(name);
    (*valp)++;
    return self;
}

static VALUE
gc_tracer_custom_field_decrement(VALUE self, VALUE name)
{
    long *valp = custom_field_value_place(name);
    (*valp)--;
    return self;
}

static VALUE
gc_tracer_custom_field_get(VALUE self, VALUE name)
{
    long *valp = custom_field_value_place(name);
    return LONG2NUM(*valp);
}

static VALUE
gc_tracer_custom_field_set(VALUE self, VALUE name, VALUE val)
{
    long *valp = custom_field_value_place(name);
    long lval = NUM2LONG(val);
    *valp = lval;
    return val;
}

static VALUE
gc_tracer_start_logging(int argc, VALUE *argv, VALUE self)
{
    struct gc_logging *logging = &trace_logging;

    if (logging->enabled == 0) {
	logging->enabled = 1;
	out_header(logging);
	enable_gc_hooks(logging);
    }

    return self;
}

static VALUE
gc_tracer_stop_logging(VALUE self)
{
    struct gc_logging *logging = &trace_logging;

    if (logging->enabled) {
	close_output(logging->out);
	disable_gc_hooks(logging);
	logging->enabled = 0;
    }

    return self;
}

static VALUE
gc_tracer_flush_logging(VALUE self)
{
    struct gc_logging *logging = &trace_logging;

    if (logging->enabled) {
	out_flush(logging->out);
    }
    else {
	rb_raise(rb_eRuntimeError, "GC tracer is not enabled.");
    }
    return self;
}

static VALUE
gc_tracer_custom_event_logging(VALUE self, VALUE event_str)
{
    struct gc_logging *logging = &trace_logging;
    const char *str = StringValueCStr(event_str);

    if (logging->enabled) {
	out_stat(logging, str);
    }
    else {
	rb_raise(rb_eRuntimeError, "GC tracer is not enabled.");
    }
    return self;
}

void
Init_gc_tracer_logging(VALUE mod)
{
    rb_define_module_function(mod, "start_logging_", gc_tracer_start_logging, 0);
    rb_define_module_function(mod, "stop_logging", gc_tracer_stop_logging, 0);
    rb_define_module_function(mod, "flush_logging", gc_tracer_flush_logging, 0);

    /* setup */
    rb_define_module_function(mod, "setup_logging_out", gc_tracer_setup_logging_out, 1);
    rb_define_module_function(mod, "setup_logging_events", gc_tracer_setup_logging_events, -1);
    rb_define_module_function(mod, "setup_logging_tick_type=", gc_tracer_setup_logging_tick_type, 1);
    rb_define_module_function(mod, "setup_logging_gc_stat=", gc_tracer_setup_logging_gc_stat, 1);
    rb_define_module_function(mod, "setup_logging_gc_latest_gc_info=", gc_tracer_setup_logging_gc_latest_gc_info, 1);
    rb_define_module_function(mod, "setup_logging_rusage=", gc_tracer_setup_logging_rusage, 1);

    /* custom fields */
    rb_define_module_function(mod, "setup_logging_custom_fields=", gc_tracer_setup_logging_custom_fields, 1);
    rb_define_module_function(mod, "custom_field_increment", gc_tracer_custom_field_increment, 1);
    rb_define_module_function(mod, "custom_field_decrement", gc_tracer_custom_field_decrement, 1);
    rb_define_module_function(mod, "custom_field_set", gc_tracer_custom_field_set, 2);
    rb_define_module_function(mod, "custom_field_get", gc_tracer_custom_field_get, 1);

    /* custom event */
    rb_define_module_function(mod, "custom_event_logging", gc_tracer_custom_event_logging, 1);

    /* setup data */
    setup_gc_trace_symbols();
    create_gc_hooks();
    /* warm up */
    rb_gc_latest_gc_info(ID2SYM(rb_intern("gc_by")));
    rb_gc_stat(ID2SYM(rb_intern("count")));


    /* default setup */
    gc_tracer_setup_logging_out(Qnil, Qnil);
    gc_tracer_setup_logging_events(0, 0, Qnil);
    gc_tracer_setup_logging_gc_stat(Qnil, Qtrue);
    gc_tracer_setup_logging_gc_latest_gc_info(Qnil, Qtrue);
    gc_tracer_setup_logging_rusage(Qnil, Qfalse);
    trace_logging.config.out_time_func = out_time_time;
}
