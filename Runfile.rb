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
    content.gsub!(/project\(NeoLiminal VERSION (\d+)\.(\d+)\)/) do
      "project(NeoLiminal VERSION #{$1}.#{$2.to_i + 1})"
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
  run "cp -r build/NeoLiminal_artefacts/Debug/VST3/NeoLiminal.vst3 Lib/"
  run "cp -r build/NeoLiminal_artefacts/Debug/AU/NeoLiminal.component Lib/"
  run "rm -rf ~/Library/Audio/Plug-Ins/VST3/NeoLiminal.vst3", quiet: true
  run "cp -r Lib/NeoLiminal.vst3 ~/Library/Audio/Plug-Ins/VST3/"
end
