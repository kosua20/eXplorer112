

workspace("eXporter112")
	-- Configuration.
	configurations({ "Release", "Dev"})
	location("build")
	targetdir ("build/%{prj.name}/%{cfg.longname}")
	debugdir ("build/%{prj.name}/%{cfg.longname}")
	architecture("x64")
	systemversion("latest")

	-- Configuration specific settings.
	filter("configurations:Release")
		defines({ "NDEBUG" })
		optimize("On")

	filter("configurations:Dev")
		defines({ "DEBUG" })
		symbols("On")

	filter({})

	project("eXporter")
		kind("ConsoleApp")

		language("C++")
		cppdialect("C++17")

		-- Compiler flags
		filter("toolset:not msc*")
			buildoptions({ "-Wall", "-Wextra" })
		filter("toolset:msc*")
			buildoptions({ "-W3"})
		filter({})
		-- visual studio filters
		filter("action:vs*")
			defines({ "_CRT_SECURE_NO_WARNINGS" })  
		filter({})

		-- Common include dirs
		-- System headers are used to support angled brackets in Xcode.
		includedirs({"src/"})
		sysincludedirs({ "libs/", "src/libs" })

		-- common files
		files({"src/tool/**", "src/core/**", "src/libs/**.hpp", "src/libs/*/*.cpp", "src/libs/**.h", "src/libs/*/*.c", "premake5.lua"})
		removefiles({"**.DS_STORE", "**.thumbs"})

	project("vIewer")
		kind("ConsoleApp")

		language("C++")
		cppdialect("C++17")

		-- Compiler flags
		filter("toolset:not msc*")
			buildoptions({ "-Wall", "-Wextra" })
		filter("toolset:msc*")
			buildoptions({ "-W3"})
		filter({})
		-- visual studio filters
		filter("action:vs*")
			defines({ "_CRT_SECURE_NO_WARNINGS" })  
		filter({})

		-- Common include dirs
		-- System headers are used to support angled brackets in Xcode.
		includedirs({"src/"})
		sysincludedirs({ "libs/", "src/libs" })

		-- common files
		files({"src/app/**", "src/core/**", "src/libs/**.hpp", "src/libs/*/*.cpp", "src/libs/**.h", "src/libs/*/*.c", "premake5.lua"})
		removefiles({"**.DS_STORE", "**.thumbs"})

	
	startproject("eXporter")

		

newaction {
   trigger     = "clean",
   description = "Clean the build directory",
   execute     = function ()
      print("Cleaning...")
      os.rmdir("./build")
      print("Done.")
   end
}
