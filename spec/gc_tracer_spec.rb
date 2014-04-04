require 'spec_helper'
require 'tmpdir'
require 'fileutils'

describe GC::Tracer do
  shared_examples "logging_test" do
    it do
      Dir.mktmpdir('gc_tracer'){|dir|
        logfile = "#{dir}/logging"
        GC::Tracer.start_logging(logfile){
          count.times{
            GC.start
          }
        }
        expect(File.exist?(logfile)).to be_true
        expect(File.read(logfile).split(/\n/).length).to be >= count * 3 + 1
      }
    end
  end

  context "1 GC.start" do
    let(:count){1}
    it_behaves_like "logging_test"
  end

  context "2 GC.start" do
    let(:count){2}
    it_behaves_like "logging_test"
  end

  shared_examples "objspace_recorder_test" do
    DIRNAME = "gc_tracer_objspace_recorder_spec.#{$$}"

    it do
      begin
        GC::Tracer.start_objspace_recording(DIRNAME){
          count.times{
            GC.start
          }
        }
        expect(Dir.glob("#{DIRNAME}/ppm/*.ppm").size).to be >= count * 3
      rescue NoMethodError
        pending "start_objspace_recording requires MRI >= 2.2"
      ensure
        FileUtils.rm_rf(DIRNAME) if File.directory?(DIRNAME)
      end
    end
  end

  context "1 GC.start" do
    let(:count){1}
    it_behaves_like "objspace_recorder_test"
  end

  context "2 GC.start" do
    let(:count){2}
    it_behaves_like "objspace_recorder_test"
  end
end
