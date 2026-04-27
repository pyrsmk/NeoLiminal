version 3

task :build do |mode = :debug|
  # Format.
  mode = :debug unless [:debug, :release].include?(mode)
  # Auto-increment versions.
  case mode
  when :debug
    content = File.read("Source/AppVersion.cpp")
    content.gsub!(/static const int kBuild = (\d+);/) do
      "static const int kBuild = #{$1.to_i + 1};"
    end
    File.write("Source/AppVersion.cpp", content)
  when :release
    content = File.read("CMakeLists.txt")
    content.gsub!(/project\(LiminalShit VERSION (\d+)\.(\d+)\)/) do
      "project(LiminalShit VERSION #{$1}.#{$2.to_i + 1})"
    end
    File.write("CMakeLists.txt", content)
    content = File.read("Source/AppVersion.cpp")
    content.gsub!(/static const int kBuild = \d+;/, "static const int kBuild = 1;")
    File.write("Source/AppVersion.cpp", content)
  end
  # Build.
  run "cmake -B build -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_OSX_ARCHITECTURES=arm64'"
  run "cmake --build build --config #{mode.capitalize}"
end

task :install do
  run "rm -rf ~/Library/Audio/Plug-Ins/VST3/LiminalShit.vst3", quiet: true
  run "cp -r build/LiminalShit_artefacts/Debug/VST3/LiminalShit.vst3 ~/Library/Audio/Plug-Ins/VST3/"
end
