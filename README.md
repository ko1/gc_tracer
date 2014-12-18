# GC::Tracer

Trace Garbage Collector activities and output statistics information.

This gem only supports MRI 2.1.0 and later.

## Installation

Add this line to your application's Gemfile:

    gem 'gc_tracer'

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install gc_tracer

## Usage

gc_tracer gem adds GC::Tracer module. GC::Tracer module has the following features.

- Logging GC statistics information
- ObjectSpace recorder (not supported yet)

### Logging

You can get GC statistics information in block form like this:

```ruby
require 'gc_tracer'
GC::Tracer.start_logging(filename) do
  # do something
end
```

This code is equivalent to the following code.

```ruby
require 'gc_tracer'
begin
  GC::Tracer.start_logging(filename)
  # do something
ensure
  GC::Tracer.stop_logging
end
```

### Setup

In the stored file (filename), you can get tab separated values of:

* `GC.stat()`
* `GC.latest_gc_info()`
* `getrusage()` (if supported by system)

at each events, there are one of:

* GC starting time (start)
* End of marking time (end_mark)
* End of sweeping time (end_sweep)
* GC enter (enter)
* GC exit (exit)
* newobj (newobj)
* freeobj (freeobj)

For one GC, you can get all three lines.

You can specify events by event name symbols you want to show.

```ruby
require 'gc_tracer'
begin
  GC::Tracer.start_logging(filename, events: %i(enter exit))
  # do something
ensure
  GC::Tracer.stop_logging
end
```

Default events are "start", "end_mark" and "end_sweep". You can specify 
what kind of information you want to collect.

```ruby
require 'gc_tracer'
begin
  GC::Tracer.start_logging(filename, gc_stat: false, gc_latest_gc_info: false, rusage: false)
  # do something
ensure
  GC::Tracer.stop_logging
end
```

Above example means that no details information are not needed. Default 
setting is "gc_stat: true, gc_latest_gc_info: true, rusage: false".

You can specify tick (time stamp) type with keyword parameter 
"tick_type". You can choose one of the tick type in :hw_counter, :time 
and :nano_time (if platform supports clock_gettime()).

See lib/gc_tracer.rb for more details.

### Custom events

You can add custom events by your own. Calling 
`GC::Tracer.custom_event_logging(event_name)` in your program will puts 
new statistics line into the logging file.

For example, the following program insert 1000 custom event lines into 
logging file (stderr for this type).

```ruby GC::Tracer.start_logging(events: %i(start), gc_stat: false) do
  1_000.times{|i|
    1_000.times{''}
    GC::Tracer.custom_event_logging("custom_#{i}")
  }
end
```

This method is useful to trace where the GC events occur in your 
application.


## Contributing

1. Fork it ( http://github.com/ko1/gc_tracer/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request


## Credit

Originally created by Koichi Sasada.
Thank you Yuki Torii who helps me to make gem package.
