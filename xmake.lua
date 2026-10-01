set_project("KoiProfiler")
set_version("1.0.0")
set_languages("c++17")
add_rules("mode.debug", "mode.release")

add_requires("imgui", { configs = { glfw = true, opengl3 = true } })

-- Both samples share Sample/Common.h
local function sample_target(name, file)
	target(name)
		set_kind("binary")
		add_files(file)
		add_includedirs(".")
		add_headerfiles("KoiProfiler.h", "Sample/Common.h")
		add_packages("imgui")
		if is_plat("windows") then
			add_syslinks("opengl32")
		elseif is_plat("linux") then
			add_syslinks("GL")
		elseif is_plat("macosx") then
			add_frameworks("OpenGL")
		end
	target_end()
end

-- Every setting, live
sample_target("Demo", "Sample/Demo.cpp")

-- Hands-off gallery of configurations
sample_target("Showcase", "Sample/Showcase.cpp")
