require 'mkmf'

# auto generation script
open("gc_tracer.h", 'w'){|f|
  f.puts '#include "ruby/ruby.h"'
  f.puts "static VALUE sym_gc_stat[#{GC.stat.keys.size}];"
  f.puts "static VALUE sym_latest_gc_info[#{GC.latest_gc_info.keys.size}];"

  f.puts "static void"
  f.puts "setup_gc_trace_symbols(void)"
  f.puts "{"
    GC.stat.keys.each.with_index{|k, i|
      f.puts "    sym_gc_stat[#{i}] = ID2SYM(rb_intern(\"#{k}\"));"
    }
    GC.latest_gc_info.keys.each.with_index{|k, i|
      f.puts "    sym_latest_gc_info[#{i}] = ID2SYM(rb_intern(\"#{k}\"));"
    }
  f.puts "}"
}

create_makefile('gc_tracer/gc_tracer')
