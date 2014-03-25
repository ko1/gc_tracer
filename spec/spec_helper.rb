require 'rubygems'
require 'bundler/setup'
require 'gc_tracer'

RSpec.configure do |config|
  config.mock_framework = :rspec
end