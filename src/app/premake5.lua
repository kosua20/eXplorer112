-- On Linux We have to query the dependencies of gtk+3 for NFD, and convert them to a list of libraries, we do this on the host for now.
if os.ishost("linux") then
	gtkList, code = os.outputof("pkg-config --libs libnotify gtk+-3.0")
	gtkLibs = string.explode(string.gsub(gtkList, "-l", ""), " ")
end

newoption {
	 trigger     = "env_vulkan_sdk",
	 description = "Force the use of the Vulkan SDK at the location defined by the VULKAN_SDK environment variable"
}

-- Vulkan viewer
project("eXplorer112")
	kind("ConsoleApp")

	language("C++")
	cppdialect("C++17")

	-- Compiler flags
	filter("toolset:not msc*")
		buildoptions({ "-Wall", "-Wextra", "-Wno-unknown-pragmas" })
	filter("toolset:msc*")
		buildoptions({ "-W3", "-wd4068"})
		-- Ignore missing PDBs.
		linkoptions({ "/IGNORE:4099"})
	filter({})
	-- visual studio filters
	filter("action:vs*")
		defines({ "_CRT_SECURE_NO_WARNINGS" })  
	filter({})

	-- System headers are used to support angled brackets in Xcode.
	sysincludedirs({ "../libs", "libs/", "libs/glfw/include/" })

	-- Vulkan configuration
	if _OPTIONS["env_vulkan_sdk"] then
		sysincludedirs({ "$(VULKAN_SDK)/include" })
		libdirs({ "$(VULKAN_SDK)/lib" })
	else
		filter("system:windows")
			sysincludedirs({ "$(VULKAN_SDK)/include" })
			libdirs({ "$(VULKAN_SDK)/lib" })

		filter("system:macosx or linux")
			sysincludedirs({ "/usr/local/include/" })
			libdirs({ "/usr/local/lib" })
	end
	filter({})

	filter("system:macosx")
		defines({"VK_USE_PLATFORM_MACOS_MVK"})

	filter("system:windows")
		defines({"VK_USE_PLATFORM_WIN32_KHR"})

	filter("system:linux")
		defines({"VK_USE_PLATFORM_XCB_KHR"})

	filter({})

	includedirs({"../", "./"})

	-- Linking other libraries
	links({"sr_gui", "glfw3"})

	-- Libraries for each platform.
	filter("system:macosx")
		links({"Cocoa.framework", "IOKit.framework", "CoreVideo.framework", "AppKit.framework", "pthread"})
		
	filter("system:linux")
		-- We have to query the dependencies of gtk+3 for NFD, and convert them to a list of libraries.
		links({"X11", "Xi", "Xrandr", "Xxf86vm", "Xinerama", "Xcursor", "Xext", "Xrender", "Xfixes", "xcb", "Xau", "Xdmcp", "rt", "m", "pthread", "dl", gtkLibs})
	
	filter("system:windows")
		links({"comctl32", "runtimeobject"})
		
	-- Vulkan dependencies
	filter("system:macosx or linux")
		links({"glslang", "MachineIndependent", "GenericCodeGen", "OGLCompiler", "SPIRV", "SPIRV-Tools-opt", "SPIRV-Tools","OSDependent", "spirv-cross-core", "spirv-cross-cpp" })
	
	filter({"system:windows", "configurations:Dev"})
		links({"glslangd", "OGLCompilerd", "SPIRVd", "OSDependentd", "MachineIndependentd", "GenericCodeGend", "SPIRV-Tools-optd", "SPIRV-Toolsd", "spirv-cross-cored", "spirv-cross-cppd"})
	filter({"system:windows", "configurations:Release" })
		links({"glslang", "OGLCompiler", "SPIRV", "OSDependent", "MachineIndependent", "GenericCodeGen", "SPIRV-Tools-opt", "SPIRV-Tools", "spirv-cross-core", "spirv-cross-cpp"})

	filter({})

	-- common files
	files({"**.cpp", "**.hpp" , "../core/**", 
		"../libs/**.hpp", "../libs/*/*.cpp", "../libs/**.h", "../libs/*/*.c", 
		"libs/**.hpp", "libs/*/*.cpp", "libs/**.h", "libs/*/*.c",
		"premake5.lua"})

	-- Shaders
	files({"../../resources/shaders/**"});
		
	-- Remove compiled libs and hidden files.
	removefiles { "libs/sr_gui/**" }
	removefiles { "libs/glfw/**" }
	removefiles({"**.DS_STORE", "**.thumbs"})



-- Libraries for the viewer
group("Dependencies")

	-- Include NFD and GLFW premake files.
	include("src/app/libs/sr_gui/premake5.lua")
	include("src/app/libs/glfw/premake5.lua")	
