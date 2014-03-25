require 'spec_helper'
require 'tmpdir'

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
end
