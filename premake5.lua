
newoption {
	 trigger     = "skip_viewer",
	 description = "Do not generate the project for the viewer."
}

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

	startproject("eXporter")


	-- Projects
	group("Projects")

	-- Command line exporter
	project("eXporter")
		kind("ConsoleApp")

		language("C++")
		cppdialect("C++17")

		-- Compiler flags
		filter("toolset:not msc*")
			buildoptions({ "-Wall", "-Wextra", "-Wno-unknown-pragmas" })
		filter("toolset:msc*")
			buildoptions({ "-W3", "-wd4068"})
		filter({})
		-- visual studio filters
		filter("action:vs*")
			defines({ "_CRT_SECURE_NO_WARNINGS" })  
		filter({})

		-- Common include dirs
		-- System headers are used to support angled brackets in Xcode.
		includedirs({"src/"})
		sysincludedirs({ "src/libs" })

		-- common files
		files({"src/tool/**", "src/core/**", "src/libs/**.hpp", "src/libs/*/*.cpp", "src/libs/**.h", "src/libs/*/*.c", "premake5.lua"})
		removefiles({"**.DS_STORE", "**.thumbs"})

	-- Optional viewer project
	if (not _OPTIONS["skip_viewer"]) then
		include("src/app/premake5.lua")
	end

newaction {
   trigger     = "clean",
   description = "Clean the build directory",
   execute     = function ()
      print("Cleaning...")
      os.rmdir("./build")
      print("Done.")
   end
}
