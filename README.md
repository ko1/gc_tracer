# GcTracer

gc-tracer gem add GC::Tracer module which trace GC activities and output 
all statistics information.

This gem only supports MRI 2.1.0 and later.

## Installation

Add this line to your application's Gemfile:

    gem 'gc_tracer'

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install gc_tracer

## Usage

You can get GC statistics information like that:

    require 'gc_tracer'
    GC::Tracer.start_logging(filename) do
      # do something
    end

This code is equivalent to the following code.

    require 'gc_tracer'
    GC::Tracer.start_logging(filename)
    # do something
    GC::Tracer.stop_logging

In the stored file, you can get tab separated values about GC.stat() and 
GC.latest_gc_info() at each events.  Events are "GC starting time", "end 
of marking time" and "end of sweeping time".  For one GC, three lines 
you can get.


## Contributing

1. Fork it ( http://github.com/ko1/gc_tracer/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request


## Credit

Originally created by Koichi Sasada.
Thank you Yuki Torii who helps me to make gem package.
