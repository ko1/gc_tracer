# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'gc_tracer/version'

Gem::Specification.new do |spec|
  spec.name          = "gc_tracer"
  spec.version       = GC::Tracer::VERSION
  spec.authors       = ["Koichi Sasada"]
  spec.email         = ["ko1@atdot.net"]
  spec.summary       = %q{gc_tracer gem adds GC::Tracer module.}
  spec.description   = %q{gc_tracer gem adds GC::Tracer module.}
  spec.homepage      = "https://github.com/ko1/gc_tracer"
  spec.license       = "MIT"

  spec.extensions    = %w[ext/gc_tracer/extconf.rb]
  spec.required_ruby_version = '>= 2.1.0'

  spec.files         = `git ls-files -z`.split("\x0")
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ["lib"]

  spec.add_development_dependency "bundler", "~> 1.5"
  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
  spec.add_development_dependency "rspec", "~> 3.0"
end
