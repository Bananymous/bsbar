workspace "bsbar"
    configurations { "Debug", "Release" }

project "bsbar"
    kind "ConsoleApp"
    language "C++"
	cppdialect "C++20"
    targetdir "bin/%{cfg.buildcfg}"

    files {
		"src/Battery.cpp",
		"src/Block.cpp",
		"src/Common.cpp",
		"src/Config.cpp",
		"src/Custom.cpp",
		"src/DateTime.cpp",
        "src/main.cpp",
		"src/Network.cpp",
		"src/PulseAudio.cpp",
		"src/Temperature.cpp",
    }

    includedirs {
		"src",
		"vendor",
	}
	
	links {
		"pulse"
	}

    filter "configurations:Debug"  
        symbols "On"

    filter "configurations:Release"
        optimize "On"