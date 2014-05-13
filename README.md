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
- ObjectSpace recorder

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

In the stored file (filename), you can get tab separated values of:

* `GC.stat()`
* `GC.latest_gc_info()`
* `getrusage()` (if supported by system)

at each events, there are one of:

* GC starting time
* End of marking time
* End of sweeping time

For one GC, you can get all three lines.

### ObjectSpace recorder

You can records objspace snapshots on each events.  Snapshots are stored 
in the [PPM (P6) format] (http://en.wikipedia.org/wiki/Netpbm_format).

```ruby
require 'gc_tracer'
GC::Tracer.start_objspace_recording(dirname) do
  # do something
end
```

All PPM images are stored in dirname/ppm/.

If you have netpbm package and pnmtopng command, 
bin/objspace_recorder_convert.rb converts all ppm images into png files. 
Converted png images stored into dirname/png/.

To view converted images, "dirname/viewer.html" is created.
You can view all converted png images with "dirname/viewer.html" file in animation.

This feature is supported only latest Ruby versions (2.2, and later).

#### Example

```ruby
require 'gc_tracer'
GC::Tracer.start_objspace_recording("objspace_recorded_type_example", :type){
  n =  1_000
  m = 10_000

  n.times{
    ary = []
    m.times{
      ary << ''
    }
  }
}
```

This program takes all snapshot of type information at each GC events.

- :age (default) - take snapshots of age information (empty/young/old/shady)
- :type - take snapshots of type information (T_???)

You can see an [age example] (http://www.atdot.net/~ko1/gc_tracer/objspace_recorded_age_example/viewer.html) and
a [type example] (http://www.atdot.net/~ko1/gc_tracer/objspace_recorded_type_example/viewer.html).

## Contributing

1. Fork it ( http://github.com/ko1/gc_tracer/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request


## Credit

Originally created by Koichi Sasada.
Thank you Yuki Torii who helps me to make gem package.
