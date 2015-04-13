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

  describe 'custom fields' do
    describe 'manipulate values' do
      around 'open' do |example|
        Dir.mktmpdir('gc_tracer'){|dir|
          logfile = "#{dir}/logging"
          GC::Tracer.start_logging(logfile, custom_fields: %i(a b c)) do
            example.run
          end
        }
      end

      it 'should raise error for unknown name or index' do
        expect{GC::Tracer.custom_field_increment(:xyzzy)}.to raise_error RuntimeError
        expect{GC::Tracer.custom_field_increment(42)}.to raise_error RuntimeError
      end

      it 'should count increments' do
        GC::Tracer.custom_field_increment(:a)
        GC::Tracer.custom_field_increment(:a)
        GC::Tracer.custom_field_increment(:a)
        GC::Tracer.custom_field_increment(0)
        GC::Tracer.custom_field_increment(0)
        GC::Tracer.custom_field_increment(0)

        expect(GC::Tracer.custom_field_get(:a)).to be 6
        expect(GC::Tracer.custom_field_get(0)).to be 6
      end

      it 'should count decments' do
        GC::Tracer.custom_field_decrement(:b)
        GC::Tracer.custom_field_decrement(:b)
        GC::Tracer.custom_field_decrement(:b)
        GC::Tracer.custom_field_decrement(1)
        GC::Tracer.custom_field_decrement(1)
        GC::Tracer.custom_field_decrement(1)

        expect(GC::Tracer.custom_field_get(:b)).to be -6
        expect(GC::Tracer.custom_field_get(1)).to be -6
      end

      it 'should set values' do
        GC::Tracer.custom_field_set(:c, 42)
        expect(GC::Tracer.custom_field_get(:c)).to be 42
        GC::Tracer.custom_field_set(2, 43)
        expect(GC::Tracer.custom_field_get(:c)).to be 43
      end
    end

    describe 'output custome fields' do
      it 'should output custome fields' do
        Dir.mktmpdir('gc_tracer'){|dir|
          logfile = "#{dir}/logging"
          GC::Tracer.start_logging(logfile, custom_fields: %i(a b c)) do
            GC::Tracer.custom_field_increment(:a)
            GC::Tracer.custom_field_decrement(:b)
            GC::Tracer.custom_event_logging("out")
          end

          expect(File.exist?(logfile)).to be_true
          log = File.read(logfile)
          expect(log.lines[0]).to match /a\tb\tc/
          log.each_line{|line|
            if /out/ =~ line
              expect(line).to match /1\t-1\t0/
            end
          }
        }
      end
    end
  end
  

=begin not supported now.
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
=end
end
