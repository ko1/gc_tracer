require "gc_tracer/version"
require 'gc_tracer/gc_tracer'

module GC
  module Tracer
    def self.env_logging_filename
      if fn = ENV['GC_TRACER_LOGFILE']
        "#{fn}-#{Process.pid}"
      else
        nil
      end
    end

    def self.start_logging(filename_opt = nil,
                           filename: nil,
                           # event filter
                           events: %i(start end_mark end_sweep),
                           # tick type (:hw_counter, :time, :user_time, :system_time)
                           tick_type: :time,
                           # collect information
                           gc_stat: true,
                           gc_latest_gc_info: true,
                           rusage: false,
                           custom_fields: nil
                         )
      # setup
      raise "do not specify two fienames" if filename && filename_opt
      setup_logging_out(filename_opt || filename || env_logging_filename)
      setup_logging_events(*events)

      self.setup_logging_gc_stat = gc_stat
      self.setup_logging_gc_latest_gc_info = gc_latest_gc_info
      self.setup_logging_rusage = rusage
      self.setup_logging_tick_type = tick_type
      self.setup_logging_custom_fields = custom_fields

      if block_given?
        begin
          start_logging_
          yield
        ensure
          stop_logging
        end
      else
        start_logging_
      end
    end
  end
end
