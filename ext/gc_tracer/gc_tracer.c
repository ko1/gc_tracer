/*
 * gc_tracer adds GC::Tracer module.
 *
 * By Koichi Sasada
 * created at Wed Feb 26 10:52:59 2014.
 */

#include "ruby/ruby.h"
#include "ruby/debug.h"
#include "gc_tracer.h"
#include "stdio.h"

static VALUE gc_start, gc_end_mark, gc_end_sweep;
static VALUE gc_trace_enabled;
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
trace(const char *type)
{
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

static void
gc_start_i(VALUE tpval, void *data)
{
    trace("gc_start");
}

static void
gc_end_mark_i(VALUE tpval, void *data)
{
    trace("gc_end_m");
}

static void
gc_end_sweep_i(VALUE tpval, void *data)
{
    trace("gc_end_s");
}

static VALUE
gc_tracer_stop_logging(VALUE self)
{
    if (gc_start != 0 && gc_trace_enabled != Qfalse) {
	gc_trace_enabled = Qfalse;

	fflush(gc_trace_out);
	if (gc_trace_out != stderr) {
	    fclose(gc_trace_out);
	}
	gc_trace_out = NULL;

	rb_tracepoint_disable(gc_start);
	rb_tracepoint_disable(gc_end_mark);
	rb_tracepoint_disable(gc_end_sweep);
    }

    return Qnil;
}

static VALUE
gc_tracer_start_logging(int argc, VALUE *argv, VALUE self)
{
    if (gc_start == 0) {
	gc_start = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_START, gc_start_i, 0);
	gc_end_mark = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_MARK, gc_end_mark_i, 0);
	gc_end_sweep = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_END_SWEEP, gc_end_sweep_i, 0);

	/* mark for GC */
	rb_gc_register_mark_object(gc_start);
	rb_gc_register_mark_object(gc_end_mark);
	rb_gc_register_mark_object(gc_end_sweep);
    }

    if (gc_trace_enabled == Qfalse) {
	gc_trace_enabled = Qtrue;

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

	rb_tracepoint_enable(gc_start);
	rb_tracepoint_enable(gc_end_mark);
	rb_tracepoint_enable(gc_end_sweep);

	out_header();

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

void
Init_gc_tracer(void)
{
    VALUE mod = rb_define_module_under(rb_mGC, "Tracer");

    /* logging methods */
    rb_define_module_function(mod, "start_logging", gc_tracer_start_logging, -1);
    rb_define_module_function(mod, "stop_logging", gc_tracer_stop_logging, 0);
    rb_define_module_function(mod, "setup_logging", gc_tracer_setup_logging, 1);

    /* setup default banners */
    setup_gc_trace_symbols();
    start_tick = tick();
    rb_gc_latest_gc_info(ID2SYM(rb_intern("gc_by"))); /* warm up */

    gc_trace_items = rb_ary_new();
    gc_trace_items_types = rb_ary_new();
    rb_gc_register_mark_object(gc_trace_items);
    rb_gc_register_mark_object(gc_trace_items_types);

    gc_tracer_setup_logging(Qnil, Qnil);
}
