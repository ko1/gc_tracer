#
# Rack middleware
#

require 'gc_tracer'

module Rack
  class GCTracerMiddleware
    def initialize app, view_page_path: nil, logging_filename: nil, **kw
      @app = app
      @view_page_path = view_page_path
      @logging_filename = logging_filename || GC::Tracer.env_logging_filename
      @has_view_page = @logging_filename && @view_page_path
      GC::Tracer.start_logging @logging_filename, custom_fields: %i(accesses), **kw
    end

    def call env
      if @has_view_page && env["PATH_INFO"] == @has_view_page
        GC::Tracer.flush_logging
        [200, {"Content-Type" => "text/plain"}, [open(@logging_filename).read]]
      else
        GC::Tracer.custom_field_increment(0)
        @app.call(env)
      end
    end
  end
end

