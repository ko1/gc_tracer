require 'mkmf'

# auto generation script
rusage_members = []

if  try_link(%q{
      #include "ruby/ruby.h"
      void rb_objspace_each_objects_without_setup(int (*callback)(void *, void *, size_t, void *), void *data);
      int main(int argc, char *argv[]){
        rb_objspace_each_objects_without_setup(0, 0);
        return 0;
      }
    })
  #
  $defs << "-DHAVE_RB_OBJSPACE_EACH_OBJECTS_WITHOUT_SETUP"
end

if have_header('sys/time.h') && have_header('sys/resource.h') && have_func('getrusage')
  %w(ru_maxrss
     ru_ixrss
     ru_idrss
     ru_isrss
     ru_minflt
     ru_majflt
     ru_nswap
     ru_inblock
     ru_oublock
     ru_msgsnd
     ru_msgrcv
     ru_nsignals
     ru_nvcsw
     ru_nivcsw).each{|member|
       if have_struct_member('struct rusage', member, %w(sys/time.h sys/resource.h))
         rusage_members << member
       end
     }
end

open("gc_tracer.h", 'w'){|f|
  f.puts '#include "ruby/ruby.h"'
  f.puts "static VALUE sym_gc_stat[#{GC.stat.keys.size}];"
  f.puts "static VALUE sym_latest_gc_info[#{GC.latest_gc_info.keys.size}];"
  unless rusage_members.empty?
    f.puts "static VALUE sym_rusage_timeval[2];"
    f.puts "static VALUE sym_rusage[#{rusage_members.length}];" if rusage_members.length > 0
  end

  f.puts "static void"
  f.puts "setup_gc_trace_symbols(void)"
  f.puts "{"
    #
    GC.stat.keys.each.with_index{|k, i|
      f.puts "    sym_gc_stat[#{i}] = ID2SYM(rb_intern(\"#{k}\"));"
    }
    GC.latest_gc_info.keys.each.with_index{|k, i|
      f.puts "    sym_latest_gc_info[#{i}] = ID2SYM(rb_intern(\"#{k}\"));"
    }
    f.puts "    sym_rusage_timeval[0] = ID2SYM(rb_intern(\"ru_utime\"));"
    f.puts "    sym_rusage_timeval[1] = ID2SYM(rb_intern(\"ru_stime\"));"
    rusage_members.each.with_index{|k, i|
      f.puts "    sym_rusage[#{i}] = ID2SYM(rb_intern(\"#{k}\"));"
    }
    #
  f.puts "}"
}

create_makefile('gc_tracer/gc_tracer')
