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
GC::Tracer.start_logging(filename)
# do something
GC::Tracer.stop_logging
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

### Allocation tracing

You can trace allocation information and you can get aggregated information.

```ruby
require 'gc_tracer'
require 'pp'

pp GC::Tracer.start_allocation_tracing{
  50_000.times{|i|
    i.to_s
    i.to_s
    i.to_s
  }
}
```

will show

```
{["test.rb", 6]=>[50000, 44290, 0, 6],
 ["test.rb", 7]=>[50000, 44289, 0, 5],
 ["test.rb", 8]=>[50000, 44295, 0, 6]}
```

In this case, 50,000 objects are created at `test.rb:6'. 44,290 is total 
age of objects created at this line. Average age of object created at 
this line is 50000/44290 = 0.8858. 0 is minimum age and 6 is maximum age.

Simply you can require `gc_tracer/allocation_trace' to start allocation 
tracer and output the aggregated information into stdot at the end of 
program.

```ruby
require 'gc_tracer/allocation_trace'

# Run your program here
50_000.times{|i|
  i.to_s
  i.to_s
  i.to_s
}
```

and you will see:

```
file    line    count   total_age       max_age min_age
.../lib/ruby/2.2.0/rubygems/core_ext/kernel_require.rb 55      18      23      1       6
.../gc_tracer/lib/gc_tracer/allocation_trace.rb    5       2       12      6       6
.../gc_tracer/lib/gc_tracer/allocation_trace.rb    6       2       0       0       0
test.rb 0       1       0       0       0
test.rb 5       50000   41574   0       5
test.rb 6       50000   41566   0       4
test.rb 7       50000   41574   0       5
```

(tab separated colums)

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

#### Examaple

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
