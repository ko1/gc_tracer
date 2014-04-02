dir = ARGV.shift || 'records'
Dir.mkdir("#{dir}/png") unless File.directory?("#{dir}/png")

Dir.glob("#{dir}/ppm/*"){|file|
  cmd = "pnmtopng #{file} > #{dir}/png/#{File.basename(file)}.png"
  system(cmd)
}
