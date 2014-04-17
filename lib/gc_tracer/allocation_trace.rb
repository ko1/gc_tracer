require 'gc_tracer'

GC::Tracer.setup_allocation_tracing(%i{path line})
GC::Tracer.start_allocation_tracing

at_exit{
  results = GC::Tracer.stop_allocation_tracing
  puts GC::Tracer.header_of_allocation_tracing.join("\t")
  results.sort_by{|k, v| k}.each{|k, v|
    puts (k+v).join("\t")
  }
}
