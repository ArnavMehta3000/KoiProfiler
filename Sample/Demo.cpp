// Interactive demo: every profiler setting, live, against a workload shaped from the UI

// Lists capacity overflows in the UI instead of stopping, so capacities can be shrunk to see what happens
static void ReportCapacityOverflow(const char* message);
#define KOI_PROF_ASSERT(condition, message)  \
	do                                       \
	{                                        \
		if (!(condition))                    \
		{                                    \
			ReportCapacityOverflow(message); \
		}                                    \
	} while (false)

// Hooks could forward to another profiler (e.g. Tracy's ZoneScopedN); the demo just counts them
static int g_scopeHookCalls = 0;
#define KOI_PROFILE_HOOK(name) ++g_scopeHookCalls
#define KOI_PROFILE_DYNAMIC_HOOK(name) ++g_scopeHookCalls
#define KOI_PROFILE_FUNCTION_HOOK() ++g_scopeHookCalls

// Enables koi::prof::stats; without it the KOI_ macros only run their hooks
#define KOI_PROFILER_ENABLE_STATS
#include "Common.h"

#if defined(KOI_PROFILER_NO_SOURCE_LOCATION) || defined(KOI_PROFILER_NO_TABLE) || defined(KOI_PROFILER_NO_MARKERS) || defined(KOI_PROFILER_NO_INTERACTION) \
	|| defined(KOI_PROFILER_NO_GRID) || defined(KOI_PROFILER_NO_COUNTER_HISTORY) || defined(KOI_PROFILER_NO_BUDGETS) || defined(KOI_PROFILER_NO_OVERLAY) \
	|| defined(KOI_PROFILER_NO_EDITORS) || defined(KOI_PROFILER_NO_SETTINGS)
	#error "The demo shows every feature, so build it without KOI_PROFILER_MINIMAL or the KOI_PROFILER_NO_* macros"
#endif

// Shaped like a GPU timer query result. TaskTraits lets LoadFrameData take it as is.
struct GpuTimestamp
{
	const char* Pass    = "";
	double      BeginMs = 0.0;
	double      EndMs   = 0.0;
	uint32_t    Color   = 0;
};

template <>
struct koi::prof::TaskTraits<GpuTimestamp>
{
	static koi::prof::ProfilerTask ToTask(const GpuTimestamp& query) { return { query.BeginMs / 1000.0, query.EndMs / 1000.0, query.Pass, query.Color }; }
};

namespace
{
	using Clock = std::chrono::steady_clock;

	struct OverflowLog
	{
		const char* LastMessage = nullptr;
		int         Count       = 0;
	};
	OverflowLog g_overflows;

	using sample::AddTask;
	using sample::NameHashColor;
	using sample::SimulateWork;
	using sample::WarmColor;

	[[nodiscard]] float MillisecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
	}

	bool SliderSize(const char* label, size_t& value, int minValue, int maxValue)
	{
		int intValue = int(value);
		if (ImGui::SliderInt(label, &intValue, minValue, maxValue, "%d", ImGuiSliderFlags_Logarithmic))
		{
			value = size_t(intValue);
			return true;
		}
		return false;
	}

	void HelpMarker(const char* text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		ImGui::SetItemTooltip("%s", text);
	}

	// One simulated piece of work. stats doesn't copy scope names, so they live as long as the demo.
	struct SimTask
	{
		char     Name[24]    = {};
		bool     Active      = false;
		bool     Nested      = false; // CPU only: runs inside the scope above, folding into it
		float    CostMs      = 1.0f;
		float    WaveMs      = 0.0f;  // sine amplitude on top of the cost
		float    WaveSpeed   = 1.0f;
		float    SpikeChance = 0.0f;  // per frame
		float    SpikeMs     = 5.0f;
		uint32_t Color       = 0;     // 0 uses the palette
		float    LastMs      = 0.0f;
	};

	[[nodiscard]] SimTask MakeTask(const char* name, float costMs, float waveMs = 0.0f, float waveSpeed = 1.0f, uint32_t color = 0)
	{
		SimTask task;
		std::snprintf(task.Name, sizeof(task.Name), "%s", name);
		task.Active    = true;
		task.CostMs    = costMs;
		task.WaveMs    = waveMs;
		task.WaveSpeed = waveSpeed;
		task.Color     = color;
		return task;
	}

	struct Workload
	{
		const char*              Label = "";
		bool                     IsCpu = false; // real work timed with KOI_PROFILE; others are made up
		std::array<SimTask, 10>  Tasks;
	};

	[[nodiscard]] Workload MakeWorkload(const char* label, bool isCpu, std::initializer_list<SimTask> tasks)
	{
		Workload workload;
		workload.Label = label;
		workload.IsCpu = isCpu;
		std::copy(tasks.begin(), tasks.end(), workload.Tasks.begin());
		for (size_t taskIndex = tasks.size(); taskIndex < workload.Tasks.size(); taskIndex++)
		{
			std::snprintf(workload.Tasks[taskIndex].Name, sizeof(SimTask::Name), "%s task %d", label, int(taskIndex + 1));
		}
		return workload;
	}

	// Moving average of a per-frame cost
	struct CostSample
	{
		float LastMs    = 0.0f;
		float AverageMs = 0.0f;

		void Add(float ms)
		{
			LastMs = ms;
			AverageMs += (ms - AverageMs) * 0.05f;
		}
	};

	// Receives over-budget frames and names through SetBudgetCallback
	struct BudgetLog
	{
		struct Entry
		{
			char Text[96] = {};
			int  Frame    = 0;
		};

		koi::prof::ProfilerWindow* Window        = nullptr; // names the track an event came from
		std::array<Entry, 10>      Entries;
		size_t                     Head          = 0;
		size_t                     Count         = 0;
		int                        Total         = 0;
		bool                       IncludeFrames = false;

		void operator()(const koi::prof::BudgetEvent& event)
		{
			if (event.Name.empty() && !IncludeFrames)
			{
				return;
			}
			const int track = Window ? Window->FindTrack(*event.Graph) : -1;
			const char* label = track >= 0 ? Window->GetTrack(size_t(track)).Label : "Standalone";
			const double timeMs = double(event.Time) * 1000.0;
			const double budgetMs = double(event.Budget) * 1000.0;

			Entry& entry = Entries[Head];
			entry.Frame = ImGui::GetFrameCount();
			if (event.Name.empty())
			{
				std::snprintf(entry.Text, sizeof(entry.Text), "%s  frame  %.2f / %.2f ms", label, timeMs, budgetMs);
			}
			else
			{
				std::snprintf(entry.Text, sizeof(entry.Text), "%s  %.*s  %.2f / %.2f ms  (%u calls)", label, int(event.Name.size()), event.Name.data(),
					timeMs, budgetMs, event.Calls);
			}
			Head  = (Head + 1) % Entries.size();
			Count = std::min(Count + 1, Entries.size());
			Total++;
		}
	};

	enum TrackId : size_t
	{
		TrackCpu,
		TrackGpu,
		TrackJobs,
		TrackSelf,
		TrackIdCount,
	};
	constexpr const char* kTrackLabels[TrackIdCount] = { "CPU", "GPU", "Jobs", "Profiler" };
	constexpr size_t kNoTrack = SIZE_MAX;

	enum PaletteGenerator : int
	{
		GeneratorGoldenRatio,
		GeneratorGoldenRatioEditable,
		GeneratorWarm,
		GeneratorNameHash,
	};

	class DemoApp
	{
	public:
		DemoApp()
		{
			m_workloads[0] = MakeWorkload("CPU", true,
			{
				MakeTask("Physics", 1.5f, 1.0f, 1.3f),
				MakeTask("Scripts", 1.0f, 0.5f, 0.7f),
				MakeTask("AI", 0.5f),
				MakeTask("Audio", 0.4f),
				MakeTask("Render submit", 1.2f, 0.4f, 3.0f),
			});
			m_workloads[0].Tasks[2].Nested = true;

			m_workloads[1] = MakeWorkload("GPU", false,
			{
				MakeTask("Shadows", 1.8f, 0.6f, 0.9f),
				MakeTask("GBuffer", 2.5f),
				MakeTask("Lighting", 2.0f, 1.0f, 1.7f),
				MakeTask("Post process", 1.0f),
				MakeTask("Debug overlay", 0.4f, 0.0f, 1.0f, IM_COL32(255, 0, 255, 255)), // explicit color skips the palette
				MakeTask("Present", 0.2f),
			});

			m_workloads[2] = MakeWorkload("Jobs", false,
			{
				MakeTask("Streaming", 1.0f, 0.8f, 0.5f),
				MakeTask("Decompress", 0.6f, 0.3f, 2.3f),
				MakeTask("Navmesh", 0.3f),
			});
			m_workloads[2].Tasks[2].SpikeChance = 0.02f;
			m_workloads[2].Tasks[2].SpikeMs     = 3.0f;

			// Stats capacities, allocated once
			koi::prof::stats::Reserve(64, 16);

			// The default palette colors tasks without a color; names can be pinned
			koi::prof::DefaultPalette().SetColor("Present", IM_COL32(200, 200, 200, 255));

			// The standalone graph has its own palette, style and auto scale
			m_jobsPalette.Generator             = &WarmColor;
			m_jobsGraph.Palette                 = &m_jobsPalette;
			m_jobsGraph.AutoScale.Percentile    = 0.99f;
			m_jobsGraph.BudgetTime              = 0.004f;
			m_jobsGraph.Style.BudgetColor       = IM_COL32(255, 200, 0, 255);
			m_jobsGraph.Style.BudgetThickness   = 2.0f;
			m_jobsGraph.Style.FrameWidth        = 2;
			m_jobsGraph.Style.FrameSpacing      = 0;
			m_jobsGraph.Style.LegendWidth       = 200.0f;
			m_jobsGraph.SetBudgetCallback(m_budgetLog);

			m_build.Graph.FramesCount      = 300;
			m_build.Graph.MaxTasksPerFrame = 32;
			m_build.Graph.MaxUniqueNames   = 64;
			RebuildProfiler();
		}

		// Loads the frame that just ended into the graphs
		void BeginFrame(double time)
		{
			const Clock::time_point statsStart = Clock::now();
			koi::prof::stats::NewFrame();
			m_statsCost.Add(MillisecondsSince(statsStart));
			m_scopeHookCalls = g_scopeHookCalls;
			g_scopeHookCalls = 0;

			if (m_rebuildRequested)
			{
				m_rebuildRequested = false;
				RebuildProfiler();
			}

			// Real GPU timings would come from timer queries
			BuildGpuTimestamps(m_workloads[1], time, m_gpuTasks);
			BuildTimings(m_workloads[2], time, m_jobTasks);

			// The profiler's own cost, in its own track
			m_selfTasks.clear();
			AddTask(m_selfTasks, "Stats", m_statsCost.LastMs);
			AddTask(m_selfTasks, "Load", m_loadCost.LastMs);
			AddTask(m_selfTasks, "Render", m_renderCost.LastMs);

			const Clock::time_point loadStart = Clock::now();
			const std::vector<koi::prof::Counter>& counters = koi::prof::stats::GetLastCounters();
			LoadTrack(TrackCpu, koi::prof::stats::GetLastFrame());
			LoadTrack(TrackGpu, m_gpuTasks);
			LoadTrack(TrackJobs, m_jobTasks);
			LoadTrack(TrackSelf, m_selfTasks);
			m_profiler->LoadCounters(counters.data(), counters.size()); // ignored while paused
			m_jobsGraph.LoadFrameData(m_jobTasks);
			m_loadCost.Add(MillisecondsSince(loadStart));
		}

		// This frame's CPU work, timed with scopes
		void RunWorkload(double time)
		{
			if (m_hitchPending)
			{
				// Markers apply to the next loaded frame, which is this one for the CPU track
				m_hitchPending = false;
				m_profiler->AddMarker("Hitch", IM_COL32(255, 96, 96, 255));
				m_jobsGraph.AddMarker("Hitch", IM_COL32(255, 96, 96, 255));
				KOI_PROFILE("Hitch");
				SimulateWork(30.0);
			}

			std::array<SimTask, 10>& tasks = m_workloads[0].Tasks;
			size_t taskIndex = 0;
			while (taskIndex < tasks.size())
			{
				SimTask& task = tasks[taskIndex];
				const size_t index = taskIndex++;
				if (!task.Active)
				{
					continue;
				}

				KOI_PROFILE_DYNAMIC(task.Name); // names are edited in the UI, so they can't be hashed at compile time
				SimulateWork(Evaluate(task, index, time));

				// Nested tasks below run inside this scope, so only this one is recorded
				while (taskIndex < tasks.size() && (!tasks[taskIndex].Active || tasks[taskIndex].Nested))
				{
					SimTask& child = tasks[taskIndex];
					const size_t childIndex = taskIndex++;
					if (child.Active)
					{
						KOI_PROFILE_DYNAMIC(child.Name);
						SimulateWork(Evaluate(child, childIndex, time));
					}
				}
			}

			// The same scope several times in a row merges into one task, with its calls counted
			for (int batch = 0; batch < m_particleBatches; batch++)
			{
				KOI_PROFILE("Particles");
				SimulateWork(0.04);
			}

			// Counter units come from the value's type
			KOI_PROFILE_VALUE("Frame memory", koi::prof::Bytes((48.0 + 6.0 * std::sin(time * 0.4)) * 1024.0 * 1024.0));
			KOI_PROFILE_VALUE("Streaming pool", koi::prof::Percent(55.0 + 30.0 * std::sin(time * 0.25)));
			KOI_PROFILE_VALUE("Input latency", std::chrono::duration<double, std::milli>(3.0 + 1.5 * std::sin(time * 2.1)));
			KOI_PROFILE_COUNT("Physics bodies", int64_t(200.0 + 150.0 * std::sin(time * 1.3)));
			for (int drawIndex = 0; drawIndex < m_drawCalls; drawIndex++)
			{
				KOI_PROFILE_COUNT("Draw calls", 1);
			}
		}

		void RenderUi()
		{
			// F3 toggles the overlay
			if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
			{
				m_profiler->Overlay.Visible = !m_profiler->Overlay.Visible;
				m_profiler->MarkSettingsChanged();
			}

			UpdateLayout();
			RenderDemoWindow();

			const Clock::time_point renderStart = Clock::now();
			if (m_showProfiler && m_embedProfiler)
			{
				const ImGuiWindowFlags flags = PlaceWindow(m_layout.Profiler);
				if (ImGui::Begin("Host window", &m_showProfiler, flags))
				{
					ImGui::TextUnformatted("A window of your own, with the profiler in a child region:");
					ImGui::BeginChild("Profiler", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
					m_profiler->RenderContents();
					ImGui::EndChild();
				}
				ImGui::End();
			}
			else if (m_showProfiler)
			{
				m_profiler->Render(&m_showProfiler, PlaceWindow(m_layout.Profiler));
			}
			m_profiler->RenderOverlay();
			m_renderCost.Add(MillisecondsSince(renderStart));

			if (m_showJobsWindow)
			{
				const ImGuiWindowFlags flags = PlaceWindow(m_layout.Jobs);
				if (ImGui::Begin("Job system", &m_showJobsWindow, flags))
				{
					// Zero size fills the window; zero max frame time uses auto scale
					m_jobsGraph.Render();
				}
				ImGui::End();
			}
		}

		[[nodiscard]] bool IsVSync() const { return m_vsync; }

	private:
		struct WindowRect
		{
			ImVec2 Position;
			ImVec2 Size;
		};

		// Demo panel left, profiler top right, job system below; follows the main viewport every frame
		struct Layout
		{
			WindowRect Demo;
			WindowRect Profiler;
			WindowRect Jobs;
		};

		struct BuildSettings
		{
			std::array<bool, TrackIdCount> IncludeTrack = { true, true, false, true };
			koi::prof::GraphConfig         Graph;
			size_t                         MaxCounters  = 16;
		};

	private:
		void UpdateLayout()
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const ImVec2 origin = viewport->WorkPos + ImVec2(gap, gap);
			const ImVec2 area = viewport->WorkSize - ImVec2(gap * 2.0f, gap * 2.0f);

			const float demoWidth = std::floor(std::max(area.x * 0.27f, ImGui::GetFontSize() * 28.0f));
			const float rightX = origin.x + demoWidth + gap;
			const float rightWidth = std::max(area.x - demoWidth - gap, 1.0f);
			const float profilerHeight = std::floor((area.y - gap) * 0.7f);
			m_layout.Demo     = { origin, ImVec2(demoWidth, area.y) };
			m_layout.Profiler = { ImVec2(rightX, origin.y), ImVec2(rightWidth, profilerHeight) };
			m_layout.Jobs     = { ImVec2(rightX, origin.y + profilerHeight + gap), ImVec2(rightWidth, std::max(area.y - profilerHeight - gap, 1.0f)) };
		}

		// Locked windows are placed every frame and can't be moved or resized
		[[nodiscard]] ImGuiWindowFlags PlaceWindow(const WindowRect& rect) const
		{
			const ImGuiCond condition = m_lockLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			ImGui::SetNextWindowPos(rect.Position, condition);
			ImGui::SetNextWindowSize(rect.Size, condition);
			return m_lockLayout ? ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize : ImGuiWindowFlags_None;
		}

		[[nodiscard]] float Evaluate(SimTask& task, size_t index, double time)
		{
			float ms = task.CostMs + task.WaveMs * float(std::sin(time * double(task.WaveSpeed) + double(index)));
			if (task.SpikeChance > 0.0f && Random01() < task.SpikeChance)
			{
				ms += task.SpikeMs;
			}
			task.LastMs = std::max(ms, 0.0f);
			return task.LastMs;
		}

		[[nodiscard]] float Random01()
		{
			m_random ^= m_random << 13;
			m_random ^= m_random >> 17;
			m_random ^= m_random << 5;
			return float(m_random & 0xFFFFFFu) / float(0x1000000);
		}

		void BuildTimings(Workload& workload, double time, std::vector<koi::prof::ProfilerTask>& tasks)
		{
			tasks.clear();
			for (size_t taskIndex = 0; taskIndex < workload.Tasks.size(); taskIndex++)
			{
				SimTask& task = workload.Tasks[taskIndex];
				if (task.Active)
				{
					AddTask(tasks, task.Name, Evaluate(task, taskIndex, time), task.Color);
				}
			}
		}

		// GPU passes as timer query results, one after another
		void BuildGpuTimestamps(Workload& workload, double time, std::vector<GpuTimestamp>& queries)
		{
			queries.clear();
			double cursorMs = 0.0;
			for (size_t taskIndex = 0; taskIndex < workload.Tasks.size(); taskIndex++)
			{
				SimTask& task = workload.Tasks[taskIndex];
				if (task.Active)
				{
					const double lengthMs = double(Evaluate(task, taskIndex, time));
					queries.push_back({ task.Name, cursorMs, cursorMs + lengthMs, task.Color });
					cursorMs += lengthMs;
				}
			}
		}

		// Any range of ProfilerTask, or of a type with TaskTraits
		template <typename Range>
		void LoadTrack(TrackId id, const Range& tasks)
		{
			if (m_trackIndices[id] != kNoTrack)
			{
				m_profiler->LoadFrameData(m_trackIndices[id], tasks);
			}
		}

		// Capacities are fixed at construction, so changing them rebuilds the window. Settings carry over by track label
		void RebuildProfiler()
		{
			koi::prof::WindowConfig config;
			config.Tracks.clear();
			for (size_t id = 0; id < TrackIdCount; id++)
			{
				m_trackIndices[id] = kNoTrack;
				if (m_build.IncludeTrack[id])
				{
					m_trackIndices[id] = config.Tracks.size();
					config.Tracks.push_back({ kTrackLabels[id], m_build.Graph });
				}
			}
			config.MaxCounters = m_build.MaxCounters;
			config.BudgetFps   = 120;
			config.Counters    = koi::prof::CounterDisplay::Average; // "Physics bodies" changes every frame

			auto profiler = std::make_unique<koi::prof::ProfilerWindow>(config);
			if (m_profiler)
			{
				profiler->ProfilerFlags   = m_profiler->ProfilerFlags;
				profiler->Layout          = m_profiler->Layout;
				profiler->SharedScale     = m_profiler->SharedScale;
				profiler->Counters        = m_profiler->Counters;
				profiler->BackgroundAlpha = m_profiler->BackgroundAlpha;
				profiler->AutoPauseMs     = m_profiler->AutoPauseMs;
				profiler->SetPaused(m_profiler->IsPaused());
				profiler->SetAutoScale(m_profiler->IsAutoScale());
				profiler->SetBudgetFps(m_profiler->GetBudgetFps());
				profiler->SetUnit(m_profiler->Unit);
			}

			for (size_t trackIndex = 0; trackIndex < profiler->GetTrackCount(); trackIndex++)
			{
				koi::prof::ProfilerTrack& track = profiler->GetTrack(trackIndex);
				if (const koi::prof::ProfilerTrack* previous = FindTrack(track.Label))
				{
					track.Visible            = previous->Visible;
					track.HeightWeight       = previous->HeightWeight;
					track.View               = previous->View;
					track.Graph.Style        = previous->Graph.Style;
					track.Graph.AutoScale    = previous->Graph.AutoScale;
					track.Graph.Palette      = previous->Graph.Palette;
					track.Graph.BudgetTime   = previous->Graph.BudgetTime;
					for (size_t budgetIndex = 0; budgetIndex < previous->Graph.GetNameBudgetCount(); budgetIndex++)
					{
						const koi::prof::NameBudget budget = previous->Graph.GetNameBudget(budgetIndex);
						if (budget.Share > 0.0f)
						{
							track.Graph.SetBudgetShare(budget.Name, budget.Share);
						}
						else if (budget.Time > 0.0f)
						{
							track.Graph.SetBudget(budget.Name, std::chrono::duration<float>(budget.Time));
						}
					}
				}
				else
				{
					ApplyTrackDefaults(track);
				}
			}
			m_budgetLog.Window = profiler.get();
			profiler->SetBudgetCallback(m_budgetLog);
			m_profiler = std::move(profiler);
		}

		[[nodiscard]] const koi::prof::ProfilerTrack* FindTrack(const char* label) const
		{
			if (!m_profiler)
			{
				return nullptr;
			}
			for (size_t trackIndex = 0; trackIndex < m_profiler->GetTrackCount(); trackIndex++)
			{
				const koi::prof::ProfilerTrack& track = m_profiler->GetTrack(trackIndex);
				if (std::string_view(track.Label) == label)
				{
					return &track;
				}
			}
			return nullptr;
		}

		// Each track's graph has its own style, auto scale, palette and budget
		void ApplyTrackDefaults(koi::prof::ProfilerTrack& track)
		{
			using namespace std::chrono_literals;
			const std::string_view label = track.Label;
			track.Graph.Style.UseColoredLegendText = true;
			track.Graph.Style.BackgroundColor      = IM_COL32(0, 0, 0, 96);
			if (label == kTrackLabels[TrackCpu])
			{
				// Per-name budgets: a fixed time or a share of the frame budget
				track.Graph.SetBudget("Physics", 2ms);
				track.Graph.SetBudgetShare("Scripts", 0.15f);
			}
			else if (label == kTrackLabels[TrackGpu])
			{
				track.Graph.Style.BudgetColor = IM_COL32(255, 128, 32, 255);
				track.Graph.SetBudget("Lighting", 2500us);
			}
			else if (label == kTrackLabels[TrackJobs])
			{
				track.Graph.Palette = &m_jobsPalette;
			}
			else if (label == kTrackLabels[TrackSelf])
			{
				track.HeightWeight      = 0.5f;
				track.Graph.Style.Unit  = koi::prof::TimeUnit::Microseconds;
				track.Graph.BudgetTime  = 0.0f;
			}
		}

		void RenderDemoWindow()
		{
			const ImGuiWindowFlags flags = PlaceWindow(m_layout.Demo);
			if (!ImGui::Begin("KoiProfiler Demo", nullptr, flags))
			{
				ImGui::End();
				return;
			}
			ImGui::PushItemWidth(ImGui::GetFontSize() * -12.0f);

			ImGui::TextWrapped("Every setting of KoiProfiler, live. Right-click a graph for its options, F3 toggles the overlay.");
			ImGui::Checkbox("Lock layout", &m_lockLayout);
			ImGui::SetItemTooltip("Keeps the windows in place and sized to the application window. Unlock to move them freely.");
			if (g_overflows.Count > 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%d capacity overflows", g_overflows.Count);
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset"))
				{
					g_overflows = {};
				}
				if (g_overflows.LastMessage)
				{
					ImGui::TextWrapped("Last: %s", g_overflows.LastMessage);
				}
			}

			if (ImGui::CollapsingHeader("Help"))
			{
				ImGui::BulletText("Workload shapes the simulated CPU, GPU and job timings.");
				ImGui::BulletText("Profiler window holds the window's own settings, the same as its Options popup.");
				ImGui::BulletText("Tracks and capacities rebuilds the window with other tracks or sizes.");
				ImGui::BulletText("Palette changes how task colors are picked.");
				ImGui::BulletText("Profiler cost shows what the profiler itself costs each frame.");
				ImGui::SeparatorText("In the profiler");
				ImGui::BulletText("Hover a frame to list its tasks. Hover a task or legend entry to highlight its name.");
				ImGui::BulletText("Click a frame to pause on it, then scroll over the graph or drag Frames back to step.");
				ImGui::BulletText("Right-click a graph for the options, even with the controls hidden.");
				ImGui::BulletText("The Table button next to a track's label swaps its graph for per-name stats.");
				ImGui::BulletText("Hover a counter for its min, average, max and recent history.");
				ImGui::BulletText("Names over their own budget turn red in the legend, tooltip, table and overlay.");
				ImGui::BulletText("A scope repeated in a row merges into one task; the tooltip shows x N and the table counts Calls.");
				ImGui::BulletText("Counters take their unit (bytes, time, percent) from the value's type.");
				ImGui::BulletText("Budget events lists what SetBudgetCallback reports.");
				ImGui::BulletText("Edit budgets under Options > track > Name budgets.");
				ImGui::BulletText("Settings changed in the UI are saved to imgui.ini.");
			}
			// Sections reuse labels like "CPU", so each gets its own ID scope
			if (ImGui::CollapsingHeader("Workload", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushID("Workload");
				RenderWorkloadSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Profiler window", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushID("Window");
				RenderWindowSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Tracks and capacities"))
			{
				ImGui::PushID("Capacities");
				RenderCapacitiesSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Palette"))
			{
				ImGui::PushID("Palette");
				RenderPaletteSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Standalone graph"))
			{
				ImGui::PushID("Standalone");
				RenderStandaloneSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Overlay"))
			{
				ImGui::PushID("Overlay");
				RenderOverlaySection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Budget events"))
			{
				ImGui::PushID("Budget");
				RenderBudgetSection();
				ImGui::PopID();
			}
			if (ImGui::CollapsingHeader("Profiler cost", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushID("Cost");
				RenderCostSection();
				ImGui::PopID();
			}

			ImGui::PopItemWidth();
			ImGui::End();
		}

		void RenderOverlaySection()
		{
			ImGui::TextWrapped("RenderOverlay draws a compact summary in a corner, like Unreal's stat unit. Times turn yellow near their budget and red over it.");
			if (koi::prof::ShowOverlayEditor(m_profiler->Overlay))
			{
				m_profiler->MarkSettingsChanged(); // changed outside the window's options, so request a save
			}
		}

		void RenderBudgetSection()
		{
			ImGui::TextWrapped("SetBudgetCallback reports every loaded frame over the frame budget, and every name over its own budget. Edit name budgets under Options > track > Name budgets.");
			ImGui::Checkbox("Include frame budget", &m_budgetLog.IncludeFrames);
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear"))
			{
				m_budgetLog.Head  = 0;
				m_budgetLog.Count = 0;
				m_budgetLog.Total = 0;
			}
			ImGui::Text("%d events", m_budgetLog.Total);
			for (size_t entryIndex = 0; entryIndex < m_budgetLog.Count; entryIndex++)
			{
				const size_t capacity = m_budgetLog.Entries.size();
				const BudgetLog::Entry& entry = m_budgetLog.Entries[(m_budgetLog.Head + capacity - 1 - entryIndex) % capacity];
				ImGui::TextDisabled("%6d", entry.Frame);
				ImGui::SameLine();
				ImGui::TextUnformatted(entry.Text);
			}
		}

		void RenderWorkloadSection()
		{
			ImGui::Checkbox("VSync", &m_vsync);
			ImGui::SameLine();
			if (ImGui::Button("Hitch (30 ms)"))
			{
				m_hitchPending = true;
			}
			ImGui::SetItemTooltip("One CPU frame with a 30 ms scope, marked in every graph. Set Auto-pause in the options to stop on it.");
			ImGui::SameLine();
			bool autoPause = m_profiler->AutoPauseMs > 0.0f;
			if (ImGui::Checkbox("Auto-pause", &autoPause))
			{
				m_profiler->AutoPauseMs = autoPause ? 25.0f : 0.0f;
			}
			ImGui::SetItemTooltip("ProfilerWindow::AutoPauseMs, set to 25 ms here");

			ImGui::InputText("##MarkerName", m_markerName, sizeof(m_markerName));
			ImGui::SameLine();
			if (ImGui::Button("Add marker"))
			{
				m_profiler->AddMarker(m_markerName);
			}
			ImGui::SetItemTooltip("AddMarker marks the next frame every track loads");

			ImGui::SliderInt("Draw calls", &m_drawCalls, 0, 5000, "%d", ImGuiSliderFlags_Logarithmic);
			HelpMarker("Counted with KOI_PROFILE_COUNT one at a time");
			ImGui::SliderInt("Particle batches", &m_particleBatches, 0, 32);
			HelpMarker("KOI_PROFILE(\"Particles\") this many times in a row. They merge into one task, and the tooltip and table count the calls.");
			ImGui::Text("Scope hook calls last frame: %d", m_scopeHookCalls);
			HelpMarker("The demo defines the hook to count scopes. Point it at Tracy's ZoneScopedN to feed both profilers.");

			for (Workload& workload : m_workloads)
			{
				if (ImGui::TreeNode(workload.Label))
				{
					if (workload.IsCpu)
					{
						ImGui::TextDisabled("Real work timed with KOI_PROFILE_DYNAMIC, plus the Particles, Input and UI scopes");
					}
					else if (&workload == &m_workloads[1])
					{
						ImGui::TextDisabled("Made-up timer query results, loaded as GpuTimestamp through TaskTraits");
					}
					else
					{
						ImGui::TextDisabled("Made-up timings loaded with LoadFrameData");
					}
					RenderWorkloadTasks(workload);
					ImGui::TreePop();
				}
			}
		}

		void RenderWorkloadTasks(Workload& workload)
		{
			for (size_t taskIndex = 0; taskIndex < workload.Tasks.size(); taskIndex++)
			{
				SimTask& task = workload.Tasks[taskIndex];
				ImGui::PushID(int(taskIndex));
				ImGui::Checkbox("##Active", &task.Active);
				ImGui::SetItemTooltip("Active");
				ImGui::SameLine();
				const bool open = task.Active
					? ImGui::TreeNode("##Task", "%s  %.2f ms%s", task.Name, double(task.LastMs), task.Nested && workload.IsCpu ? "  (nested)" : "")
					: ImGui::TreeNode("##Task", "%s", task.Name);
				if (open)
				{
					ImGui::SliderFloat("Cost", &task.CostMs, 0.0f, 10.0f, "%.2f ms");
					ImGui::SliderFloat("Wave", &task.WaveMs, 0.0f, 5.0f, "%.2f ms");
					HelpMarker("Amplitude of a sine added to the cost");
					ImGui::SliderFloat("Wave speed", &task.WaveSpeed, 0.0f, 5.0f);
					ImGui::SliderFloat("Spike chance", &task.SpikeChance, 0.0f, 0.2f, "%.3f");
					ImGui::SliderFloat("Spike", &task.SpikeMs, 0.0f, 30.0f, "%.1f ms");
					if (workload.IsCpu)
					{
						ImGui::BeginDisabled(taskIndex == 0);
						ImGui::Checkbox("Nested", &task.Nested);
						ImGui::EndDisabled();
						HelpMarker("Runs inside the scope above. Only outermost scopes are recorded, so its time folds into that one.");
					}
					else
					{
						bool explicitColor = task.Color != 0;
						if (ImGui::Checkbox("Explicit color", &explicitColor))
						{
							task.Color = explicitColor ? IM_COL32(255, 255, 255, 255) : 0;
						}
						HelpMarker("ProfilerTask::Color, which skips the palette");
						if (explicitColor)
						{
							ImGui::SameLine();
							ImVec4 color = ImGui::ColorConvertU32ToFloat4(task.Color);
							if (ImGui::ColorEdit4("##Color", &color.x, ImGuiColorEditFlags_NoInputs))
							{
								task.Color = std::max(ImGui::ColorConvertFloat4ToU32(color), 1u);
							}
						}
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		void RenderWindowSection()
		{
			ImGui::Checkbox("Show", &m_showProfiler);
			ImGui::SameLine();
			ImGui::Checkbox("Embed in a host window", &m_embedProfiler);
			HelpMarker("RenderContents draws into a window of your own instead of Render beginning one");

			// The window's controls, through the API
			bool paused = m_profiler->IsPaused();
			if (ImGui::Checkbox("Paused", &paused))
			{
				m_profiler->SetPaused(paused);
			}
			ImGui::SameLine();
			bool autoScale = m_profiler->IsAutoScale();
			if (ImGui::Checkbox("Auto scale", &autoScale))
			{
				m_profiler->SetAutoScale(autoScale);
			}

			ImGui::SeparatorText("Options");
			m_profiler->ShowOptionsEditor();
		}

		void RenderCapacitiesSection()
		{
			ImGui::TextWrapped("Capacities are allocated once, when the window is built. Changing them builds a new window; settings carry over by track label.");
			for (size_t id = 0; id < TrackIdCount; id++)
			{
				if (id > 0)
				{
					ImGui::SameLine();
				}
				ImGui::Checkbox(kTrackLabels[id], &m_build.IncludeTrack[id]);
			}
			SliderSize("Frames", m_build.Graph.FramesCount, 10, 2000);
			SliderSize("Tasks per frame", m_build.Graph.MaxTasksPerFrame, 1, 128);
			SliderSize("Unique names", m_build.Graph.MaxUniqueNames, 1, 256);
			SliderSize("Name length", m_build.Graph.MaxNameLength, 1, 64);
			SliderSize("Counters", m_build.MaxCounters, 1, 64);
			if (ImGui::Button("Rebuild profiler window"))
			{
				m_rebuildRequested = true;
			}

			ImGui::SeparatorText("Usage");
			if (ImGui::BeginTable("Usage", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Track");
				ImGui::TableSetupColumn("Names");
				ImGui::TableSetupColumn("Tasks per frame");
				ImGui::TableSetupColumn("Vertices");
				ImGui::TableHeadersRow();
				for (size_t trackIndex = 0; trackIndex < m_profiler->GetTrackCount(); trackIndex++)
				{
					const koi::prof::ProfilerGraph& graph = m_profiler->GetTrack(trackIndex).Graph;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(m_profiler->GetTrack(trackIndex).Label);
					ImGui::TableNextColumn();
					UsageBar(graph.GetUsedNameCount(), graph.GetConfig().MaxUniqueNames);
					ImGui::TableNextColumn();
					UsageBar(graph.GetMaxTaskCount(), graph.GetConfig().MaxTasksPerFrame);
					ImGui::TableNextColumn();
					ImGui::Text("%d", graph.GetLastVertexCount());
				}
				ImGui::EndTable();
			}
			ImGui::Text("Default palette: %d / %d names", int(koi::prof::DefaultPalette().GetCount()), int(koi::prof::DefaultPalette().GetCapacity()));
			ImGui::Text("Scopes last frame: %d, counters: %d", int(koi::prof::stats::GetLastFrame().size()), int(koi::prof::stats::GetLastCounters().size()));
		}

		static void UsageBar(size_t used, size_t capacity)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%d / %d", int(used), int(capacity));
			ImGui::ProgressBar(float(used) / float(std::max<size_t>(capacity, 1)), ImVec2(-FLT_MIN, 0.0f), text);
		}

		void RenderPaletteSection()
		{
			koi::prof::ColorPalette& palette = koi::prof::DefaultPalette();
			ImGui::TextWrapped("Tasks without an explicit color get one from their graph's palette, by name. Every track shares the default palette unless given its own, so a name has the same color everywhere.");

			if (ImGui::Combo("Generator", &m_paletteGenerator, "Golden ratio\0Golden ratio (editable)\0Warm (custom)\0By name hash (custom)\0"))
			{
				palette.Generator         = m_paletteGenerator == GeneratorWarm ? &WarmColor : m_paletteGenerator == GeneratorNameHash ? &NameHashColor : &koi::prof::GoldenRatioColor;
				palette.GeneratorUserData = m_paletteGenerator == GeneratorGoldenRatioEditable ? &m_goldenRatioParams : nullptr;
				palette.Regenerate(); // keeps pinned colors
			}
			koi::prof::ShowPaletteEditor(palette);

			ImGui::SeparatorText("Pin a color");
			ImGui::InputText("Name", m_pinName, sizeof(m_pinName));
			ImGui::ColorEdit4("Color", &m_pinColor.x, ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			if (ImGui::Button("Pin"))
			{
				palette.SetColor(m_pinName, ImGui::ColorConvertFloat4ToU32(m_pinColor));
			}
		}

		void RenderStandaloneSection()
		{
			ImGui::TextWrapped("A ProfilerGraph used on its own, drawn with graph.Render() in the Job system window.");
			ImGui::Checkbox("Show job system window", &m_showJobsWindow);
			bool ownPalette = m_jobsGraph.Palette == &m_jobsPalette;
			if (ImGui::Checkbox("Own warm palette", &ownPalette))
			{
				m_jobsGraph.Palette = ownPalette ? &m_jobsPalette : &koi::prof::DefaultPalette();
			}
			koi::prof::ShowGraphEditor(m_jobsGraph);
		}

		void RenderCostSection()
		{
			ImGui::TextWrapped("Measured around the library calls. Also drawn in the Profiler track, in microseconds.");
			if (ImGui::BeginTable("Cost", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Step");
				ImGui::TableSetupColumn("Last");
				ImGui::TableSetupColumn("Average");
				ImGui::TableHeadersRow();
				CostRow("stats::NewFrame", m_statsCost);
				CostRow("LoadFrameData + LoadCounters", m_loadCost);
				CostRow("Render", m_renderCost);
				ImGui::EndTable();
			}
			const float totalMs = m_statsCost.AverageMs + m_loadCost.AverageMs + m_renderCost.AverageMs;
			const float frameMs = ImGui::GetIO().DeltaTime * 1000.0f;
			ImGui::Text("Total %.1f us, %.2f%% of a %.2f ms frame", double(totalMs) * 1000.0, frameMs > 0.0f ? double(totalMs / frameMs) * 100.0 : 0.0, double(frameMs));
		}

		static void CostRow(const char* label, const CostSample& cost)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::Text("%.1f us", double(cost.LastMs) * 1000.0);
			ImGui::TableNextColumn();
			ImGui::Text("%.1f us", double(cost.AverageMs) * 1000.0);
		}

	private:
		std::array<Workload, 3>                    m_workloads;
		std::unique_ptr<koi::prof::ProfilerWindow> m_profiler;
		std::array<size_t, TrackIdCount>           m_trackIndices = {};
		BuildSettings                              m_build;

		koi::prof::ColorPalette                    m_jobsPalette{ 16 };
		koi::prof::ProfilerGraph                   m_jobsGraph{ koi::prof::GraphConfig{ 200, 8, 8, 32 } };
		koi::prof::GoldenRatioParams               m_goldenRatioParams;

		std::vector<GpuTimestamp>                  m_gpuTasks;
		std::vector<koi::prof::ProfilerTask>       m_jobTasks;
		std::vector<koi::prof::ProfilerTask>       m_selfTasks;
		CostSample                                 m_statsCost;
		CostSample                                 m_loadCost;
		CostSample                                 m_renderCost;
		BudgetLog                                  m_budgetLog;

		char                                       m_pinName[32]      = "Present";
		char                                       m_markerName[32]   = "Marker";
		int                                        m_scopeHookCalls   = 0;
		ImVec4                                     m_pinColor         = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		int                                        m_paletteGenerator = GeneratorGoldenRatio;
		int                                        m_drawCalls        = 120;
		int                                        m_particleBatches  = 8;
		uint32_t                                   m_random           = 0x12345678u;
		bool                                       m_showProfiler     = true;
		bool                                       m_embedProfiler    = false;
		bool                                       m_showJobsWindow   = true;
		bool                                       m_vsync            = true;
		bool                                       m_hitchPending     = false;
		bool                                       m_rebuildRequested = false;
		bool                                       m_lockLayout       = true;
		Layout                                     m_layout;
	};
}

static void ReportCapacityOverflow(const char* message)
{
	g_overflows.LastMessage = message;
	g_overflows.Count++;
}

// Scope named after the function
static void PollInput()
{
	KOI_PROFILE_FUNCTION();
	glfwPollEvents();
}

int main()
{
	sample::App app;
	if (!sample::InitApp(app, "Koi Profiler Demo", "imgui.ini"))
	{
		return 1;
	}

	DemoApp demo;
	while (sample::IsRunning(app))
	{
		// Load last frame's scopes and counters into the graphs
		const double time = glfwGetTime();
		demo.BeginFrame(time);

		PollInput();
		demo.RunWorkload(time);
		{
			KOI_PROFILE("UI");
			sample::NewImGuiFrame();
			demo.RenderUi();
			ImGui::Render();
		}

		app.VSync = demo.IsVSync();
		sample::Present(app);
	}

	sample::ShutdownApp(app);
	return 0;
}
