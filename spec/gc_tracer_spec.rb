require 'spec_helper'
require 'tmpdir'
require 'fileutils'

describe GC::Tracer do
  shared_examples "logging_test" do
    it 'should output correctly into file' do
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
    dirname = "gc_tracer_objspace_recorder_spec.#{$$}"

    it 'should output snapshots correctly into directory' do
      begin
        GC::Tracer.start_objspace_recording(dirname){
          count.times{
            GC.start
          }
        }
        expect(Dir.glob("#{dirname}/ppm/*.ppm").size).to be >= count * 3
      rescue NoMethodError
        pending "start_objspace_recording requires MRI >= 2.2"
      ensure
        FileUtils.rm_rf(dirname) if File.directory?(dirname)
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

  describe 'GC::Tracer.start_allocation_tracing' do
    it 'should includes allocation information' do
      line = __LINE__ + 2
      result = GC::Tracer.start_allocation_tracing do
        Object.new
      end

      expect(result.length).to be >= 1
      expect(result[[__FILE__, line]]).to eq [1, 0, 0, 0]
    end

    it 'should run twice' do
      line = __LINE__ + 2
      result = GC::Tracer.start_allocation_tracing do
        Object.new
      end

      expect(result.length).to be >= 1
      expect(result[[__FILE__, line]]).to eq [1, 0, 0, 0]
    end
  end
end
