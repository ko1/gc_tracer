require "bundler/gem_tasks"
require "rake/extensiontask"
require 'rspec/core/rake_task'

spec = Gem::Specification.load('gc_tracer.gemspec')

Rake::ExtensionTask.new("gc_tracer", spec){|ext|
  ext.lib_dir = "lib/gc_tracer"
}

RSpec::Core::RakeTask.new('spec' => 'compile')

task default: :spec

task :run => 'compile' do
  ruby 'test.rb'
end

task :gdb => 'compile' do
  system('gdb --args ruby test.rb')
end

