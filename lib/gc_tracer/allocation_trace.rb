require 'gc_tracer'

GC::Tracer.start_allocation_tracing

at_exit{
  puts "file\tline\tcount\ttotal_age\tmax_age\tmin_age"
  GC::Tracer.stop_allocation_tracing.sort_by{|k, v| k}.each{|k, v|
    puts (k+v).join("\t")
  }
}
