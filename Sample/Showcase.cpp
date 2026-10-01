// Hands-off gallery of profiler configurations beside a made-up game viewport with an overlay in each corner
// Timings are generated; every few seconds an event (hitch, shader compile, level load, GC) is marked in every graph

#include "Common.h"

namespace
{
	using namespace std::chrono_literals;

	enum Source : size_t
	{
		SourceCpu,
		SourceGpu,
		SourceJobs,
		SourceRender,
		SourceCount,
	};
	constexpr const char* kSourceLabels[SourceCount] = { "CPU", "GPU", "Jobs", "Render thread" };

	// Task time drifting with two slow waves plus smoothed noise
	struct Channel
	{
		const char* Name;
		float       BaseMs;
		float       WaveMs;
		float       WaveSpeed;
		float       NoiseMs;
		uint32_t    Color = 0;    // 0 uses the palette
		int         Calls = 1;    // consecutive calls the time is split over, merged into one task
		float       Noise = 0.0f; // smoothed state
	};

	// An event that disrupts a few frames, with an extra task on one or two tracks
	struct EventKind
	{
		const char* Name;
		uint32_t    Color;
		Source      Track;
		const char* TaskName;
		float       MinMs;
		float       MaxMs;
		int         MinFrames;
		int         MaxFrames;
		Source      SecondTrack; // SourceCount when none
		const char* SecondTask;
		float       SecondMs;
	};

	constexpr EventKind kEvents[] = {
		{ "Hitch",           IM_COL32(255, 96, 96, 255),   SourceCpu, "Hitch",          18.0f, 35.0f, 1, 1, SourceCount,  nullptr,           0.0f },
		{ "Shader compile",  IM_COL32(255, 170, 60, 255),  SourceCpu, "Shader compile", 8.0f,  16.0f, 2, 3, SourceGpu,    "PSO compile",     4.0f },
		{ "Level load",      IM_COL32(90, 170, 255, 255),  SourceCpu, "Level load",     25.0f, 45.0f, 3, 5, SourceJobs,   "Streaming burst", 12.0f },
		{ "Garbage collect", IM_COL32(200, 120, 255, 255), SourceCpu, "GC",             5.0f,  11.0f, 1, 2, SourceRender, "Resource flush",  3.0f },
	};

	// Generated timings for every source, with periodic events
	class Workload
	{
	public:
		Workload()
		{
			m_channels[SourceCpu] = {
				{ "Input",         0.30f, 0.10f, 0.7f, 0.05f },
				{ "Physics",       1.60f, 0.70f, 0.9f, 0.15f },
				{ "Animation",     0.90f, 0.30f, 1.7f, 0.08f },
				{ "Scripts",       1.10f, 0.60f, 0.5f, 0.20f },
				{ "AI",            0.60f, 0.30f, 1.1f, 0.10f },
				{ "Audio",         0.35f, 0.05f, 2.0f, 0.03f },
				{ "Render submit", 1.30f, 0.40f, 1.3f, 0.10f },
				{ "UI",            0.50f, 0.15f, 0.8f, 0.05f },
			};
			m_channels[SourceGpu] = {
				{ "Shadows",      1.60f, 0.50f, 0.6f, 0.10f },
				{ "GBuffer",      2.20f, 0.30f, 0.9f, 0.08f },
				{ "SSAO",         0.70f, 0.10f, 1.5f, 0.05f },
				{ "Lighting",     2.00f, 0.90f, 0.7f, 0.12f },
				{ "Volumetrics",  0.80f, 0.40f, 0.4f, 0.06f },
				{ "Post process", 1.00f, 0.10f, 1.2f, 0.05f },
				{ "UI",           0.30f, 0.05f, 1.0f, 0.02f, IM_COL32(220, 220, 220, 255) }, // explicit color skips the palette
				{ "Present",      0.20f, 0.02f, 2.0f, 0.02f },
			};
			m_channels[SourceJobs] = {
				{ "Streaming",  1.20f, 0.90f, 0.3f, 0.10f },
				{ "Decompress", 0.70f, 0.30f, 1.9f, 0.08f, 0, 4 },
				{ "Navmesh",    0.40f, 0.20f, 0.8f, 0.05f },
				{ "Particles",  0.90f, 0.40f, 1.4f, 0.10f },
			};
			m_channels[SourceRender] = {
				{ "Visibility",    0.80f, 0.30f, 0.6f, 0.06f },
				{ "Sorting",       0.50f, 0.20f, 1.0f, 0.04f },
				{ "Command lists", 1.80f, 0.60f, 0.8f, 0.12f, 0, 6 },
				{ "Submit",        0.60f, 0.10f, 1.6f, 0.05f },
			};
			for (std::vector<koi::prof::ProfilerTask>& tasks : m_tasks)
			{
				tasks.reserve(16);
			}
		}

		// Builds this frame's tasks. Returns the event starting this frame, if any.
		const EventKind* Update(double time)
		{
			const EventKind* started = nullptr;
			if (time >= m_nextEventTime)
			{
				m_event         = &kEvents[std::min(size_t(m_random.Next01() * float(std::size(kEvents))), std::size(kEvents) - 1)];
				m_eventFrames   = m_event->MinFrames + int(m_random.Next01() * float(m_event->MaxFrames - m_event->MinFrames + 1));
				m_eventMs       = m_random.Range(m_event->MinMs, m_event->MaxMs);
				m_nextEventTime = time + double(m_random.Range(2.5f, 6.0f));
				started = m_event;
			}

			for (size_t source = 0; source < SourceCount; source++)
			{
				std::vector<koi::prof::ProfilerTask>& tasks = m_tasks[source];
				tasks.clear();
				for (size_t channelIndex = 0; channelIndex < m_channels[source].size(); channelIndex++)
				{
					Channel& channel = m_channels[source][channelIndex];
					channel.Noise += (m_random.Range(-1.0f, 1.0f) - channel.Noise) * 0.2f;
					const double phase = double(channelIndex) * 1.7 + double(source) * 0.9;
					const double wave = 0.7 * std::sin(time * double(channel.WaveSpeed) + phase) + 0.3 * std::sin(time * double(channel.WaveSpeed) * 2.7 + phase * 1.3);
					const double milliseconds = double(channel.BaseMs) + double(channel.WaveMs) * wave + double(channel.NoiseMs * channel.Noise);
					for (int call = 0; call < channel.Calls; call++)
					{
						sample::AddTask(tasks, channel.Name, std::max(milliseconds, 0.02) / double(channel.Calls), channel.Color);
					}
				}
			}

			if (m_eventFrames > 0)
			{
				m_eventFrames--;
				sample::AddTask(m_tasks[m_event->Track], m_event->TaskName, double(m_eventMs * m_random.Range(0.8f, 1.2f)));
				if (m_event->SecondTrack != SourceCount)
				{
					sample::AddTask(m_tasks[m_event->SecondTrack], m_event->SecondTask, double(m_event->SecondMs * m_random.Range(0.7f, 1.3f)));
				}
			}

			const float seconds = float(time);
			// MakeCounter takes the unit from the value's type
			m_counters[0] = koi::prof::MakeCounter("Draw calls", int(1800.0f + 400.0f * std::sin(seconds * 0.7f) + m_random.Range(-30.0f, 30.0f)));
			m_counters[1] = koi::prof::MakeCounter("Entities", int(1200.0f + 150.0f * std::sin(seconds * 0.15f)));
			m_counters[2] = koi::prof::MakeCounter("Particles", int(8000.0f + 6000.0f * std::max(std::sin(seconds * 0.9f), 0.0f)));
			m_counters[3] = koi::prof::MakeCounter("GPU memory", koi::prof::Bytes((2.6 + 0.3 * std::sin(time * 0.2)) * 1024.0 * 1024.0 * 1024.0));
			m_counters[4] = koi::prof::MakeCounter("Streaming pool", koi::prof::Percent(60.0 + 25.0 * std::sin(time * 0.35)));
			m_counters[5] = koi::prof::MakeCounter("Present wait", std::chrono::duration<double, std::milli>(1.2 + 0.8 * std::sin(time * 1.1)));
			return started;
		}

		[[nodiscard]] const std::vector<koi::prof::ProfilerTask>& GetTasks(Source source) const { return m_tasks[source]; }
		[[nodiscard]] const std::array<koi::prof::Counter, 6>& GetCounters() const { return m_counters; }

	private:
		std::array<std::vector<Channel>, SourceCount>                 m_channels;
		std::array<std::vector<koi::prof::ProfilerTask>, SourceCount> m_tasks;
		std::array<koi::prof::Counter, 6>                             m_counters;
		sample::Random                                                m_random;
		const EventKind*                                              m_event         = nullptr;
		double                                                        m_nextEventTime = 2.0;
		float                                                         m_eventMs       = 0.0f;
		int                                                           m_eventFrames   = 0;
	};

	// Sky, sun and hills standing in for a game frame under the overlays
	void DrawScene(ImDrawList* drawList, ImVec2 min, ImVec2 max, float time)
	{
		const ImVec2 size = max - min;
		drawList->PushClipRect(min, max, true);
		drawList->AddRectFilledMultiColor(min, max, IM_COL32(14, 18, 44, 255), IM_COL32(14, 18, 44, 255), IM_COL32(118, 62, 108, 255), IM_COL32(118, 62, 108, 255));

		// Same stars every frame, each twinkling at its own pace
		sample::Random stars;
		for (int starIndex = 0; starIndex < 90; starIndex++)
		{
			const ImVec2 position(min.x + stars.Next01() * size.x, min.y + stars.Next01() * size.y * 0.6f);
			const float twinkle = 0.5f + 0.5f * std::sin(time * stars.Range(0.5f, 2.5f) + float(starIndex));
			drawList->AddCircleFilled(position, 1.0f + stars.Next01(), IM_COL32(255, 255, 255, int(40.0f + 160.0f * twinkle)));
		}

		const ImVec2 sun(min.x + size.x * 0.68f, min.y + size.y * (0.5f + 0.03f * std::sin(time * 0.2f)));
		const float sunRadius = std::min(size.x, size.y) * 0.08f;
		for (int glow = 4; glow > 0; glow--)
		{
			drawList->AddCircleFilled(sun, sunRadius * (1.0f + 0.45f * float(glow)), IM_COL32(255, 170, 110, 18));
		}
		drawList->AddCircleFilled(sun, sunRadius, IM_COL32(255, 214, 150, 255));

		// Three hill layers; nearer ones scroll faster
		struct HillLayer
		{
			float    Base;
			float    Height;
			float    Frequency;
			float    Speed;
			uint32_t Color;
		};
		constexpr HillLayer kLayers[] = {
			{ 0.62f, 0.06f, 3.0f, 0.03f, IM_COL32(58, 38, 86, 255) },
			{ 0.72f, 0.07f, 5.0f, 0.06f, IM_COL32(38, 26, 64, 255) },
			{ 0.83f, 0.05f, 8.0f, 0.12f, IM_COL32(22, 16, 40, 255) },
		};
		constexpr int kSegments = 64;
		for (const HillLayer& layer : kLayers)
		{
			auto heightAt = [&](float u)
			{
				const float x = u * layer.Frequency + time * layer.Speed * layer.Frequency;
				return min.y + size.y * (layer.Base - layer.Height * (std::sin(x) * 0.7f + std::sin(x * 2.3f + 1.0f) * 0.3f));
			};
			for (int segment = 0; segment < kSegments; segment++)
			{
				const float u0 = float(segment) / float(kSegments);
				const float u1 = float(segment + 1) / float(kSegments);
				const float x0 = min.x + size.x * u0;
				const float x1 = min.x + size.x * u1;
				drawList->AddQuadFilled(ImVec2(x0, heightAt(u0)), ImVec2(x1, heightAt(u1)), ImVec2(x1, max.y), ImVec2(x0, max.y), layer.Color);
			}
		}
		drawList->PopClipRect();
	}

	struct GalleryWindow
	{
		const char*                                Title = "";
		std::unique_ptr<koi::prof::ProfilerWindow> Profiler;
		std::vector<Source>                        Sources;          // timing source per track
		bool                                       Counters = false; // loads counters too
	};

	class Showcase
	{
	public:
		Showcase()
		{
			m_pastelParams.Saturation    = 0.45f;
			m_pastelParams.SaturationAlt = 0.35f;
			m_pastelParams.Value         = 0.95f;
			m_pastelPalette.GeneratorUserData = &m_pastelParams;
			m_warmPalette.Generator = &sample::WarmColor;
			m_hashPalette.Generator = &sample::NameHashColor;
			m_windows.reserve(6);

			// Classic: CPU over GPU, colored legend, name budgets, counters and the full overlay
			{
				koi::prof::WindowConfig config;
				config.BudgetFps = 120;
				config.Counters  = koi::prof::CounterDisplay::Average;
				GalleryWindow& window = AddWindow("Frame breakdown", { SourceCpu, SourceGpu }, config, true);
				for (size_t trackIndex = 0; trackIndex < window.Profiler->GetTrackCount(); trackIndex++)
				{
					koi::prof::GraphStyle& style = window.Profiler->GetTrack(trackIndex).Graph.Style;
					style.UseColoredLegendText = true;
					style.BackgroundColor      = IM_COL32(0, 0, 0, 80);
				}
				koi::prof::ProfilerGraph& cpu = window.Profiler->GetTrack(0).Graph;
				cpu.SetBudget("Physics", 2ms);
				cpu.SetBudgetShare("Scripts", 0.15f);
				window.Profiler->GetTrack(1).Graph.SetBudget("Lighting", 2500us);
				window.Profiler->GetTrack(1).Graph.Style.BudgetColor = IM_COL32(255, 128, 32, 255);

				koi::prof::OverlaySettings& overlay = window.Profiler->Overlay;
				overlay.Corner   = koi::prof::OverlayCorner::TopRight;
				overlay.TopNames = 3;
			}

			// Three tracks side by side, shared scale, pastel palette. Overlay: frame line only.
			{
				koi::prof::WindowConfig config;
				config.Layout        = koi::prof::TrackLayout::SideBySide;
				config.SharedScale   = true;
				config.ProfilerFlags = koi::prof::ProfilerWindowFlags_NoCounters;
				GalleryWindow& window = AddWindow("Side by side", { SourceCpu, SourceGpu, SourceRender }, config, false);
				for (size_t trackIndex = 0; trackIndex < window.Profiler->GetTrackCount(); trackIndex++)
				{
					koi::prof::ProfilerGraph& graph = window.Profiler->GetTrack(trackIndex).Graph;
					graph.Palette            = &m_pastelPalette;
					graph.Style.FrameWidth   = 2;
					graph.Style.FrameSpacing = 0;
					graph.Style.LegendWidth  = 170.0f;
				}

				koi::prof::OverlaySettings& overlay = window.Profiler->Overlay;
				overlay.Corner      = koi::prof::OverlayCorner::TopLeft;
				overlay.ShowTracks  = false;
				overlay.GraphWidth  = 10.0f;
				overlay.GraphHeight = 1.5f;
			}

			// Tabs with the CPU track as a table. Overlay: top five names per track, no graphs.
			{
				koi::prof::WindowConfig config;
				config.Layout = koi::prof::TrackLayout::Tabs;
				GalleryWindow& window = AddWindow("Tabs and table", { SourceCpu, SourceGpu, SourceJobs }, config, false);
				window.Profiler->GetTrack(0).View = koi::prof::TrackView::Table;
				window.Profiler->GetTrack(0).Graph.SetBudget("Physics", 2ms);

				koi::prof::OverlaySettings& overlay = window.Profiler->Overlay;
				overlay.Corner     = koi::prof::OverlayCorner::BottomLeft;
				overlay.ShowFrame  = false;
				overlay.ShowGraphs = false;
				overlay.TopNames   = 5;
			}

			// One track in microseconds, warm colors, fixed grid. Overlay: tall graphs only.
			{
				koi::prof::WindowConfig config;
				config.Unit      = koi::prof::TimeUnit::Microseconds;
				config.BudgetFps = 0;
				GalleryWindow& window = AddWindow("Jobs in microseconds", { SourceJobs }, config, false);
				koi::prof::ProfilerGraph& graph = window.Profiler->GetTrack(0).Graph;
				graph.Palette            = &m_warmPalette;
				graph.BudgetTime         = 0.004f;
				graph.Style.FrameWidth   = 5;
				graph.Style.FrameSpacing = 2;
				graph.Style.GridStep     = 0.0005f;
				graph.Style.BudgetColor  = IM_COL32(255, 200, 0, 255);
				graph.AutoScale.IncludeBudget = true;

				koi::prof::OverlaySettings& overlay = window.Profiler->Overlay;
				overlay.Corner          = koi::prof::OverlayCorner::BottomRight;
				overlay.ShowFrame       = false;
				overlay.TopNames        = 0;
				overlay.GraphWidth      = 14.0f;
				overlay.GraphHeight     = 3.0f;
				overlay.ColorByBudget   = false;
				overlay.BackgroundAlpha = 0.85f;
			}

			// Graphs only: no header, controls, labels or legend; colors from name hashes
			{
				koi::prof::WindowConfig config;
				config.ProfilerFlags = koi::prof::ProfilerWindowFlags_NoHeader | koi::prof::ProfilerWindowFlags_NoControls | koi::prof::ProfilerWindowFlags_NoCounters | koi::prof::ProfilerWindowFlags_NoTrackLabels;
				config.MinGraphHeight = 10;
				GalleryWindow& window = AddWindow("Minimal strip", { SourceCpu, SourceGpu, SourceJobs, SourceRender }, config, false, 900);
				for (size_t trackIndex = 0; trackIndex < window.Profiler->GetTrackCount(); trackIndex++)
				{
					koi::prof::ProfilerGraph& graph = window.Profiler->GetTrack(trackIndex).Graph;
					graph.Palette               = &m_hashPalette;
					graph.Style.FrameWidth      = 1;
					graph.Style.FrameSpacing    = 0;
					graph.Style.LegendWidth     = 0.0f;
					graph.Style.ShowGridLabels  = false;
					graph.Style.BackgroundColor = IM_COL32(8, 8, 16, 255);
				}
				window.Profiler->Overlay.Visible = false;
			}

			// Budget shares for every CPU name, counters with history, strong highlighting
			{
				koi::prof::WindowConfig config;
				config.BudgetFps = 144;
				GalleryWindow& window = AddWindow("Budgets and counters", { SourceCpu }, config, true);
				koi::prof::ProfilerGraph& graph = window.Profiler->GetTrack(0).Graph;
				for (const char* name : { "Input", "Physics", "Animation", "Scripts", "AI", "Audio", "Render submit", "UI" })
				{
					graph.SetBudgetShare(name, 0.12f);
				}
				graph.Style.HighlightDimAlpha = 0.15f;
				graph.Style.LegendWidth       = 300.0f;
				window.Profiler->Overlay.Visible = false;
			}
		}

		void Update(double time)
		{
			// Markers land on the event's first frame
			if (const EventKind* event = m_workload.Update(time))
			{
				for (GalleryWindow& window : m_windows)
				{
					window.Profiler->AddMarker(event->Name, event->Color);
				}
			}

			for (GalleryWindow& window : m_windows)
			{
				for (size_t trackIndex = 0; trackIndex < window.Sources.size(); trackIndex++)
				{
					window.Profiler->LoadFrameData(trackIndex, m_workload.GetTasks(window.Sources[trackIndex]));
				}
				if (window.Counters)
				{
					const std::array<koi::prof::Counter, 6>& counters = m_workload.GetCounters();
					window.Profiler->LoadCounters(counters.data(), counters.size());
				}
			}
		}

		void Render(double time)
		{
			// Gallery on the left, two windows wide; game viewport on the right
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const ImVec2 origin = viewport->WorkPos + ImVec2(gap, gap);
			const ImVec2 area = viewport->WorkSize - ImVec2(gap * 2.0f, gap * 2.0f);
			const float galleryWidth = std::floor(area.x * 0.6f);
			const ImVec2 cellSize(std::floor((galleryWidth - gap) * 0.5f), std::floor((area.y - gap * 2.0f) / 3.0f));
			const ImGuiWindowFlags fixedFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

			for (size_t windowIndex = 0; windowIndex < m_windows.size(); windowIndex++)
			{
				const size_t column = windowIndex % 2;
				const size_t row = windowIndex / 2;
				ImGui::SetNextWindowPos(origin + ImVec2((cellSize.x + gap) * float(column), (cellSize.y + gap) * float(row)), ImGuiCond_Always);
				ImGui::SetNextWindowSize(cellSize, ImGuiCond_Always);
				m_windows[windowIndex].Profiler->Render(nullptr, fixedFlags);
			}

			ImGui::SetNextWindowPos(origin + ImVec2(galleryWidth + gap, 0.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(area.x - galleryWidth - gap, area.y), ImGuiCond_Always);
			if (ImGui::Begin("Game viewport  -  an overlay in every corner", nullptr, fixedFlags | ImGuiWindowFlags_NoScrollbar))
			{
				// On this window's draw list: over the scene, under windows in front
				const ImVec2 sceneMin = ImGui::GetCursorScreenPos();
				const ImVec2 sceneMax = sceneMin + ImGui::GetContentRegionAvail();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				DrawScene(drawList, sceneMin, sceneMax, float(time));
				for (GalleryWindow& window : m_windows)
				{
					window.Profiler->RenderOverlay(drawList, sceneMin, sceneMax);
				}
				ImGui::Dummy(sceneMax - sceneMin);
			}
			ImGui::End();
		}

	private:
		// Not saved, so each run shows these configurations
		GalleryWindow& AddWindow(const char* title, std::initializer_list<Source> sources, koi::prof::WindowConfig config, bool counters, size_t framesCount = 300)
		{
			koi::prof::GraphConfig graphConfig;
			graphConfig.FramesCount      = framesCount;
			graphConfig.MaxTasksPerFrame = 24;
			graphConfig.MaxUniqueNames   = 48;

			config.Title        = title;
			config.SettingsName = "";
			config.Flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
			config.Tracks.clear();
			for (const Source source : sources)
			{
				config.Tracks.push_back({ kSourceLabels[source], graphConfig });
			}

			GalleryWindow& window = m_windows.emplace_back();
			window.Title    = title;
			window.Profiler = std::make_unique<koi::prof::ProfilerWindow>(config);
			window.Sources  = sources;
			window.Counters = counters;
			return window;
		}

	private:
		Workload                     m_workload;
		std::vector<GalleryWindow>   m_windows;
		koi::prof::GoldenRatioParams m_pastelParams;
		koi::prof::ColorPalette      m_pastelPalette{ 64 };
		koi::prof::ColorPalette      m_warmPalette{ 64 };
		koi::prof::ColorPalette      m_hashPalette{ 64 };
	};

	void ApplyStyle(float dpiScale)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding    = 6.0f * dpiScale;
		style.ChildRounding     = 4.0f * dpiScale;
		style.FrameRounding     = 3.0f * dpiScale;
		style.PopupRounding     = 4.0f * dpiScale;
		style.TabRounding       = 4.0f * dpiScale;
		style.WindowBorderSize  = 0.0f;
		style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.07f, 0.07f, 0.10f, 1.0f);
		style.Colors[ImGuiCol_TitleBg]       = ImVec4(0.10f, 0.10f, 0.15f, 1.0f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.26f, 1.0f);
	}
}

int main()
{
	sample::App app;
	if (!sample::InitApp(app, "Koi Profiler Showcase", nullptr))
	{
		return 1;
	}
	ApplyStyle(app.DpiScale);

	Showcase showcase;
	while (sample::IsRunning(app))
	{
		glfwPollEvents();
		const double time = glfwGetTime();
		showcase.Update(time);

		sample::NewImGuiFrame();
		showcase.Render(time);
		ImGui::Render();
		sample::Present(app);
	}

	sample::ShutdownApp(app);
	return 0;
}
