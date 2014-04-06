require 'erb'
require 'fileutils'

dir = ARGV.shift || 'objspace_records'
Dir.mkdir("#{dir}/png") unless File.directory?("#{dir}/png")
first_gc_count = nil
last_gc_count = 0

Dir.glob("#{dir}/ppm/*"){|file|
  cmd = "pnmtopng #{file} > #{dir}/png/#{File.basename(file)}.png"
  system(cmd)
  if /(\d{8})\.\d/ =~ file
    c = $1.to_i
    first_gc_count = c if first_gc_count == nil || first_gc_count > c
    last_gc_count = c if last_gc_count < c
  end
}

color_description = {}
File.read("#{dir}/color_description.txt").each_line{|line|
  desc, color = *line.chomp.split(/\t/)
  color_description[desc] = color
}

color_description_js = color_description.map{|(k, v)|
  "    {desc: \"#{k}\", \t\"color\": \"#{v}\"}"
}.join(",\n")

html_file = "#{dir}/viewer.html"

open(html_file, 'w'){|f|
  f.puts ERB.new(File.read(File.join(__dir__, "../lib/gc_tracer/viewer.html.erb"))).result(binding)
}

unless File.exist?("#{dir}/jquery-2.1.0.min.js")
  FileUtils.cp(File.join(__dir__, "../public/jquery-2.1.0.min.js"), "#{dir}/jquery-2.1.0.min.js")
end

puts "Success: see #{html_file}"

