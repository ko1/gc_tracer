/*
 * gc_tracer adds GC::Tracer module.
 *
 * By Koichi Sasada
 * created at Wed Feb 26 10:52:59 2014.
 */

#include <ruby/ruby.h>

void Init_gc_tracer_logging(VALUE m_gc_tracer); /* in gc_logging.c */

void
Init_gc_tracer(void)
{
    VALUE mod = rb_define_module_under(rb_mGC, "Tracer");
    Init_gc_tracer_logging(mod);
}
