#pragma once

// Shared sample code: window, ImGui setup, frame presentation and made-up timings

#if defined(_WIN32)
	#define NOMINMAX
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif
#if defined(__APPLE__)
	#include <OpenGL/gl.h>
#else
	#include <GL/gl.h>
#endif

#include "KoiProfiler.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <memory>

namespace sample
{
	using Clock = std::chrono::steady_clock;

	struct App
	{
		GLFWwindow* Window   = nullptr;
		float       DpiScale = 1.0f;
		bool        VSync    = true; // applied on present
	};

	// Opens a centered window over most of the work area, with ImGui scaled to the monitor DPI
	inline bool InitApp(App& app, const char* title, const char* iniFilename)
	{
		if (!glfwInit())
		{
			return false;
		}
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

		int areaX = 0;
		int areaY = 0;
		int areaWidth = 1280;
		int areaHeight = 720;
		if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
		{
			glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaWidth, &areaHeight);
			float scaleY = 1.0f;
			glfwGetMonitorContentScale(monitor, &app.DpiScale, &scaleY);
		}
		const int windowWidth = std::max(int(float(areaWidth) * 0.85f), 640);
		const int windowHeight = std::max(int(float(areaHeight) * 0.85f), 480);

		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // shown once positioned
		app.Window = glfwCreateWindow(windowWidth, windowHeight, title, nullptr, nullptr);
		if (!app.Window)
		{
			glfwTerminate();
			return false;
		}
		glfwSetWindowPos(app.Window, areaX + (areaWidth - windowWidth) / 2, areaY + (areaHeight - windowHeight) / 2);
		glfwShowWindow(app.Window);
		glfwMakeContextCurrent(app.Window);
		glfwSwapInterval(app.VSync ? 1 : 0);

		ImGui::CreateContext();
		ImGui::GetIO().IniFilename = iniFilename;
		ImGui::StyleColorsDark();
		ImGui::GetStyle().ScaleAllSizes(app.DpiScale);
		ImGui::GetStyle().FontScaleDpi = app.DpiScale;
		ImGui_ImplGlfw_InitForOpenGL(app.Window, true);
		ImGui_ImplOpenGL3_Init("#version 330");
		return true;
	}

	[[nodiscard]] inline bool IsRunning(const App& app)
	{
		return !glfwWindowShouldClose(app.Window);
	}

	inline void NewImGuiFrame()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	// Draws ImGui's output and swaps buffers
	inline void Present(App& app)
	{
		static bool appliedVSync = true;
		if (app.VSync != appliedVSync)
		{
			appliedVSync = app.VSync;
			glfwSwapInterval(app.VSync ? 1 : 0);
		}

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(app.Window, &width, &height);
		glViewport(0, 0, width, height);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(app.Window);
	}

	inline void ShutdownApp(App& app)
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		glfwDestroyWindow(app.Window);
		glfwTerminate();
	}

	// Busy-waits as dummy work
	inline void SimulateWork(double lengthMs)
	{
		const auto end = Clock::now() + std::chrono::duration<double, std::milli>(lengthMs);
		while (Clock::now() < end)
		{
		}
	}

	// Appends a task starting where the previous one ended, like GPU query results
	inline void AddTask(std::vector<koi::prof::ProfilerTask>& tasks, const char* name, double lengthMs, uint32_t color = 0)
	{
		koi::prof::ProfilerTask& task = tasks.emplace_back();
		task.StartTime = tasks.size() > 1 ? tasks[tasks.size() - 2].EndTime : 0.0;
		task.EndTime   = task.StartTime + lengthMs / 1000.0;
		task.Name      = name;
		task.Color     = color;
	}

	// Custom ColorGenerator: golden-ratio steps within warm hues
	inline uint32_t WarmColor(size_t index, std::string_view, void*)
	{
		const float hue = std::fmod(float(index) * 0.618034f, 1.0f) * 0.15f;
		ImVec4 rgb{ 0.0f, 0.0f, 0.0f, 1.0f };
		ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 0.95f, rgb.x, rgb.y, rgb.z);
		return ImGui::ColorConvertFloat4ToU32(rgb);
	}

	// Custom ColorGenerator: hue from the name alone, independent of order
	inline uint32_t NameHashColor(size_t, std::string_view name, void*)
	{
		const float hue = float(koi::prof::HashName(name) & 0xFFFFu) / 65535.0f;
		ImVec4 rgb{ 0.0f, 0.0f, 0.0f, 1.0f };
		ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.9f, rgb.x, rgb.y, rgb.z);
		return ImGui::ColorConvertFloat4ToU32(rgb);
	}

	// Xorshift, so every run looks the same
	struct Random
	{
		uint32_t State = 0x12345678u;

		[[nodiscard]] float Next01()
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			return float(State & 0xFFFFFFu) / float(0x1000000);
		}

		[[nodiscard]] float Range(float minValue, float maxValue) { return minValue + (maxValue - minValue) * Next01(); }
	};
}
