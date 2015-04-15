#
# Rack middleware
#

require 'gc_tracer'

module Rack
  class GCTracerMiddleware
    def initialize app, view_page_path: nil, filename: nil, logging_filename: nil, **kw
      @app = app
      @view_page_path = view_page_path
      @logging_filename = filename || logging_filename || GC::Tracer.env_logging_filename

      if @logging_filename && view_page_path
        @view_page_pattern = /\A#{view_page_path}/
      else
        @view_page_pattern = nil
      end

      GC::Tracer.start_logging @logging_filename, rusage: true, custom_fields: %i(accesses), **kw
    end

    def make_page
      header = data = nil
      open(@logging_filename){|f|
        header = f.gets
        data = f.readlines
      }
      headers = "<tr>" + header.split(/\s+/).map{|e| "<th>#{e}</th>"}.join("\n") + "</tr>"
      data = data.map{|line|
        "<tr>" + line.split(/\s+/).map{|e| "<td>#{e}</td>"}.join + "</td>"
      }.join("\n")
      "<table>#{headers}\n#{data}</table>"
    end

    def call env
      if @view_page_pattern && @view_page_pattern =~ env["PATH_INFO"]
        GC::Tracer.flush_logging
        p env["PATH_INFO"]
        if env["PATH_INFO"] == @view_page_path + "/text"
          [200, {"Content-Type" => "text/plain"}, [open(@logging_filename).read]]
        else
          [200, {"Content-Type" => "text/html"}, [make_page]]
        end
      else
        GC::Tracer.custom_field_increment(0)
        @app.call(env)
      end
    end
  end
end

