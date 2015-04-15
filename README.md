# GC::Tracer

Trace Garbage Collector activities and output statistics information.

This gem only supports MRI 2.1.0 and later.

[![Build Status](https://travis-ci.org/ko1/gc_tracer.svg)](https://travis-ci.org/ko1/gc_tracer)

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

If `ENV['GC_TRACER_LOGFILE']` is given, then this value and `pid` (concatenated with '-') is used as filename.

If filename is not given, then all of logs puts onto `stderr`.

### Setup

In the stored file (filename), you can get tab separated values of:

* `GC.stat()`
* `GC.latest_gc_info()`
* `getrusage()` (if supported by system)
* Custom fields (described below)

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

### Custom fields

You can add custom fields.

```ruby
GC::Tracer.start_logging(custom_fields: [:name1, :name2, ...]) do
  # All fields are cleared by zero.

  # You can increment values of each field.
  GC::Tracer.custom_field_increment(:name1)
  # It is equivalent to
  #   GC::Tracer.custom_field_set(:name1, GC::Tracer.custom_field_get(:name1))

  # You can also decrement values
  GC::Tracer.custom_field_decrement(:name1)

  # Now, you can specify only Fixnum as field value.
  GC::Tracer.custom_field_set(:name2, 123)

  # You can specify an index instead of field name (faster than actual name)
  GC::Tracer.custom_field_increment(0) # :name1
end
```

Custom fields are printed as last columns.

### Custom events

You can add custom events by your own. Calling
`GC::Tracer.custom_event_logging(event_name)` in your program will puts
new statistics line into the logging file.

For example, the following program insert 1000 custom event lines into
logging file (stderr for this type).

```ruby
GC::Tracer.start_logging(events: %i(start), gc_stat: false) do
  1_000.times{|i|
    1_000.times{''}
    GC::Tracer.custom_event_logging("custom_#{i}")
  }
end
```

This method is useful to trace where the GC events occur in your
application.


## Rack middleware

You can insert Rack middleware to record and view GC Tracer log.

```ruby
require 'rack'
require 'sinatra'
require 'rack/gc_tracer'

use Rack::GCTracerMiddleware, view_page_path: '/gc_tracer', filename: 'logging_file_name'

get '/' do
  'foo'
end
```

In this case, you can access two pages.

* http://host/gc_tracer - HTML table style page
* http://host/gc_tracer/text - plain text page

This Rack middleware supports one custom field *access* to count accesses number.

The following pages are demonstration Rails app on Heroku environment.

* http://protected-journey-7206.herokuapp.com/gc_tracer
* http://protected-journey-7206.herokuapp.com/gc_tracer/text

Source code of this demo app is https://github.com/ko1/tracer_demo_rails_app.
You only need to modify like https://github.com/ko1/tracer_demo_rails_app/blob/master/config.ru to use it on Rails.

You can pass two options.

* filename: File name of GC Tracer log
* view_page_path: You can view GC tracer log with this path *if a logging filename is given* (by an env val or a filename keyword argument). You may not use this option on production.

And also you can pass all options of `GC::Tracer.start_logging`.

## Contributing

1. Fork it ( http://github.com/ko1/gc_tracer/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request


## Credit

Originally created by Koichi Sasada.
Thank you Yuki Torii who helps me to make gem package.
