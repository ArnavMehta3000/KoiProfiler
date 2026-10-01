#pragma once

#define KOI_PROFILER_VERSION_MAJOR  1
#define KOI_PROFILER_VERSION_MINOR  0
#define KOI_PROFILER_VERSION_PATCH  0
#define KOI_PROFILER_VERSION        10000 // major * 10000 + minor * 100 + patch, for #if checks
#define KOI_PROFILER_VERSION_STRING "1.0.0"

#if defined(_MSVC_LANG) && _MSVC_LANG < 201703L
	#error "KoiProfiler requires C++17 (/std:c++17 or later)"
#elif !defined(_MSVC_LANG) && __cplusplus < 201703L
	#error "KoiProfiler requires C++17 (-std=c++17 or later)"
#endif

// Compile features out by defining any of these before including:
//   KOI_PROFILER_NO_SOURCE_LOCATION  no file, line and function on scopes
//   KOI_PROFILER_NO_TABLE            no per-name stats table
//   KOI_PROFILER_NO_MARKERS          no frame markers
//   KOI_PROFILER_NO_INTERACTION      no tooltip, highlighting or frame selection
//   KOI_PROFILER_NO_GRID             no grid lines
//   KOI_PROFILER_NO_COUNTER_HISTORY  no counter plot on hover
//   KOI_PROFILER_NO_BUDGETS          no per-name budgets
//   KOI_PROFILER_NO_OVERLAY          no overlay
//   KOI_PROFILER_NO_EDITORS          no Show*Editor functions or options popup
//   KOI_PROFILER_NO_SETTINGS         no imgui.ini support (skips imgui_internal.h)
//   KOI_PROFILER_MINIMAL             all of the above
#ifdef KOI_PROFILER_MINIMAL
	#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		#define KOI_PROFILER_NO_SOURCE_LOCATION
	#endif
	#ifndef KOI_PROFILER_NO_TABLE
		#define KOI_PROFILER_NO_TABLE
	#endif
	#ifndef KOI_PROFILER_NO_MARKERS
		#define KOI_PROFILER_NO_MARKERS
	#endif
	#ifndef KOI_PROFILER_NO_INTERACTION
		#define KOI_PROFILER_NO_INTERACTION
	#endif
	#ifndef KOI_PROFILER_NO_GRID
		#define KOI_PROFILER_NO_GRID
	#endif
	#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
		#define KOI_PROFILER_NO_COUNTER_HISTORY
	#endif
	#ifndef KOI_PROFILER_NO_BUDGETS
		#define KOI_PROFILER_NO_BUDGETS
	#endif
	#ifndef KOI_PROFILER_NO_OVERLAY
		#define KOI_PROFILER_NO_OVERLAY
	#endif
	#ifndef KOI_PROFILER_NO_EDITORS
		#define KOI_PROFILER_NO_EDITORS
	#endif
	#ifndef KOI_PROFILER_NO_SETTINGS
		#define KOI_PROFILER_NO_SETTINGS
	#endif
#endif

#ifndef IMGUI_DEFINE_MATH_OPERATORS
	#define IMGUI_DEFINE_MATH_OPERATORS // must come before the first imgui.h include
#endif
#include "imgui.h"
#ifndef KOI_PROFILER_NO_SETTINGS
	#include "imgui_internal.h" // imgui.ini settings handler
#endif
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Fires when a fixed capacity runs out
#ifndef KOI_PROF_ASSERT
	#define KOI_PROF_ASSERT(condition, message) IM_ASSERT((condition) && (message))
#endif

namespace koi::prof
{
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
	// Static storage, so a task only carries a pointer
	struct SourceLocation
	{
		const char* File     = nullptr;
		const char* Function = nullptr;
		uint32_t    Line     = 0;
	};
#endif

	struct ProfilerTask
	{
		double                StartTime = 0.0;     // seconds since frame start
		double                EndTime   = 0.0;
		std::string_view      Name;                // copied by LoadFrameData
		uint32_t              Color     = 0;       // 0 uses the palette
		uint32_t              NameHash  = 0;       // HashName(Name), set at compile time by KOI_PROFILE; 0 hashes on load
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		const SourceLocation* Location  = nullptr; // optional, must outlive the history
#endif

		[[nodiscard]] constexpr double GetLength() const { return EndTime - StartTime; }
	};

	// Maps your own task type to a ProfilerTask, so LoadFrameData takes it directly:
	//     template <> struct koi::prof::TaskTraits<MySample>
	//     {
	//         static koi::prof::ProfilerTask ToTask(const MySample& s) { return { s.Start, s.End, s.Name }; }
	//     };
	template <typename T>
	struct TaskTraits;

	template <>
	struct TaskTraits<ProfilerTask>
	{
		static constexpr const ProfilerTask& ToTask(const ProfilerTask& task) { return task; }
	};

	enum class CounterUnit
	{
		None,
		Bytes,
		Time,    // seconds, shown in the window's time unit
		Percent, // 0 to 100
	};

	struct Counter
	{
		std::string_view Name;
		double           Value = 0.0;
		CounterUnit      Unit  = CounterUnit::None;
	};

	// Counter values with a unit, e.g. KOI_PROFILE_VALUE("Texture memory", koi::prof::Bytes(size))
	struct Bytes
	{
		template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
		constexpr explicit Bytes(T value) : Value(double(value)) {}

		double Value;
	};

	struct Percent
	{
		template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
		constexpr explicit Percent(T value) : Value(double(value)) {}

		double Value; // 0 to 100
	};

	// Turns a counter value into a double and a unit. Covers numbers, Bytes, Percent and std::chrono durations.
	// Specialize it for your own types with a static Unit and a static ToValue.
	template <typename T, typename = void>
	struct CounterTraits;

	template <typename T>
	struct CounterTraits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
	{
		static constexpr CounterUnit Unit = CounterUnit::None;
		static constexpr double ToValue(T value) { return double(value); }
	};

	template <>
	struct CounterTraits<Bytes>
	{
		static constexpr CounterUnit Unit = CounterUnit::Bytes;
		static constexpr double ToValue(Bytes value) { return value.Value; }
	};

	template <>
	struct CounterTraits<Percent>
	{
		static constexpr CounterUnit Unit = CounterUnit::Percent;
		static constexpr double ToValue(Percent value) { return value.Value; }
	};

	template <typename Rep, typename Period>
	struct CounterTraits<std::chrono::duration<Rep, Period>>
	{
		static constexpr CounterUnit Unit = CounterUnit::Time;
		static constexpr double ToValue(std::chrono::duration<Rep, Period> value) { return std::chrono::duration<double>(value).count(); }
	};

	template <typename T>
	[[nodiscard]] constexpr Counter MakeCounter(std::string_view name, const T& value)
	{
		return { name, CounterTraits<T>::ToValue(value), CounterTraits<T>::Unit };
	}

	// The plain number behind a counter value, for hooks: TracyPlot(name, koi::prof::CounterValue(value))
	template <typename T>
	[[nodiscard]] constexpr double CounterValue(const T& value)
	{
		return CounterTraits<T>::ToValue(value);
	}

	// FNV-1a
	[[nodiscard]] constexpr uint32_t HashName(std::string_view name)
	{
		uint32_t hash = 2166136261u;
		for (const char c : name)
		{
			hash ^= uint8_t(c);
			hash *= 16777619u;
		}
		return hash;
	}

	namespace internal
	{
		// Fixed-capacity name storage; longer names are truncated
		class NameTable
		{
		public:
			static constexpr uint32_t kInvalidId = UINT32_MAX;

			NameTable(size_t capacity, size_t maxLength)
				: m_entries(capacity)
				, m_chars(capacity * maxLength)
				, m_maxLength(maxLength)
			{
			}

			// Checking hintId first makes same-order lookups skip the search. nameHash 0 hashes the name here.
			[[nodiscard]] uint32_t Find(std::string_view name, uint32_t hintId = kInvalidId, uint32_t nameHash = 0) const
			{
				const std::string_view key = name.substr(0, m_maxLength);
				if (hintId < m_count && Get(hintId) == key)
				{
					return hintId;
				}

				const uint32_t hash = KeyHash(name, nameHash);
				for (uint32_t id = 0; id < m_count; id++)
				{
					if (m_entries[id].Hash == hash && Get(id) == key)
					{
						return id;
					}
				}
				return kInvalidId;
			}

			// kInvalidId when full
			[[nodiscard]] uint32_t Add(std::string_view name, uint32_t nameHash = 0)
			{
				if (m_count == m_entries.size())
				{
					return kInvalidId;
				}
				const uint32_t id = m_count++;
				Assign(id, name, nameHash);
				return id;
			}

			// Renames an existing id
			void Assign(uint32_t id, std::string_view name, uint32_t nameHash = 0)
			{
				const std::string_view key = name.substr(0, m_maxLength);
				m_entries[id] = { KeyHash(name, nameHash), uint32_t(key.size()) };
				if (!key.empty())
				{
					std::memcpy(&m_chars[id * m_maxLength], key.data(), key.size());
				}
			}

			[[nodiscard]] std::string_view Get(uint32_t id) const
			{
				return std::string_view(m_chars.data() + id * m_maxLength, m_entries[id].Length);
			}

			[[nodiscard]] uint32_t GetCount() const { return m_count; }

		private:
			// A precomputed hash only matches when the name wasn't truncated
			[[nodiscard]] uint32_t KeyHash(std::string_view name, uint32_t nameHash) const
			{
				return nameHash != 0 && name.size() <= m_maxLength ? nameHash : HashName(name.substr(0, m_maxLength));
			}

		private:
			struct Entry
			{
				uint32_t Hash   = 0;
				uint32_t Length = 0;
			};

		private:
			std::vector<Entry> m_entries;
			std::vector<char>  m_chars;
			size_t             m_maxLength = 0;
			uint32_t           m_count     = 0;
		};
	}

	enum class TimeUnit
	{
		Milliseconds,
		Microseconds,
	};

	namespace internal
	{
		struct TimeUnitInfo
		{
			double      PerSecond;
			int         Decimals;
			const char* Suffix;
			const char* WidestValue; // for aligning value columns
		};

		[[nodiscard]] inline const TimeUnitInfo& GetTimeUnitInfo(TimeUnit unit)
		{
			static constexpr TimeUnitInfo kInfos[] = {
				{ 1000.0,    2, "ms", "00.00" },
				{ 1000000.0, 0, "us", "00000" },
			};
			return kInfos[size_t(unit)];
		}

		inline void FormatTime(char* buffer, size_t size, double seconds, TimeUnit unit)
		{
			const TimeUnitInfo& info = GetTimeUnitInfo(unit);
			std::snprintf(buffer, size, "%.*f %s", info.Decimals, seconds * info.PerSecond, info.Suffix);
		}

#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		// File name without its directory
		[[nodiscard]] inline const char* GetFileName(const char* path)
		{
			const char* name = path;
			for (const char* c = path; *c != '\0'; c++)
			{
				if (*c == '/' || *c == '\\')
				{
					name = c + 1;
				}
			}
			return name;
		}
#endif

		[[nodiscard]] inline uint32_t ScaleAlpha(uint32_t color, float scale)
		{
			const uint32_t alpha = uint32_t(float((color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) * scale);
			return (color & ~IM_COL32_A_MASK) | (std::min(alpha, 255u) << IM_COL32_A_SHIFT);
		}

#ifndef KOI_PROFILER_NO_BUDGETS
		inline constexpr uint32_t kOverBudgetColor = IM_COL32(255, 80, 80, 255);
#endif

#ifndef KOI_PROFILER_NO_EDITORS
		// 0 means follow the ImGui style; a checkbox toggles that
		inline bool EditOptionalColor(const char* label, uint32_t& color, uint32_t initialColor)
		{
			ImGui::PushID(label);
			bool followStyle = color == 0;
			bool changed = ImGui::Checkbox("##FollowStyle", &followStyle);
			ImGui::SetItemTooltip("Follow the ImGui style");
			if (changed)
			{
				color = followStyle ? 0 : initialColor;
			}
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (followStyle)
			{
				ImGui::TextDisabled("%s (style)", label);
			}
			else
			{
				ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
				if (ImGui::ColorEdit4(label, &rgba.x, ImGuiColorEditFlags_AlphaPreviewHalf))
				{
					color = std::max(ImGui::ColorConvertFloat4ToU32(rgba), 1u); // 0 is reserved for following the style
					changed = true;
				}
			}
			ImGui::PopID();
			return changed;
		}

		inline bool EditColor(const char* label, uint32_t& color)
		{
			ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
			if (ImGui::ColorEdit4(label, &rgba.x, ImGuiColorEditFlags_AlphaPreviewHalf))
			{
				color = ImGui::ColorConvertFloat4ToU32(rgba);
				return true;
			}
			return false;
		}
#endif
	}

	// Color for the index-th distinct name
	using ColorGenerator = uint32_t (*)(size_t index, std::string_view name, void* userData);

	struct GoldenRatioParams
	{
		float HueStart      = 0.1f;
		float HueStep       = 0.618034f;
		float Saturation    = 0.75f;
		float SaturationAlt = 0.55f; // used by every 2nd color
		float Value         = 0.92f;
		float ValueAlt      = 0.75f; // used by every 3rd color
		float Alpha         = 1.0f;
	};

	// Default generator: golden-ratio hue steps keep colors far apart | userData may point to a GoldenRatioParams
	[[nodiscard]] inline uint32_t GoldenRatioColor(size_t index, std::string_view, void* userData)
	{
		static constexpr GoldenRatioParams kDefaultParams{};
		const GoldenRatioParams& params = userData ? *static_cast<const GoldenRatioParams*>(userData) : kDefaultParams;

		const float hue = std::fmod(params.HueStart + float(index) * params.HueStep, 1.0f);
		const float sat = (index % 2) ? params.SaturationAlt : params.Saturation;
		const float val = (index % 3 == 2) ? params.ValueAlt : params.Value;

		ImVec4 rgb{ 0.0f, 0.0f, 0.0f, params.Alpha };
		ImGui::ColorConvertHSVtoRGB(hue, sat, val, rgb.x, rgb.y, rgb.z);
		return ImGui::ColorConvertFloat4ToU32(rgb);
	}

	// Stable color per name, keyed by hash
	class ColorPalette
	{
	public:
		explicit ColorPalette(size_t capacity = 256)
			: m_entries(capacity)
		{
		}

		[[nodiscard]] uint32_t GetColor(std::string_view name)
		{
			const uint32_t hash = HashName(name);
			if (const Entry* entry = Find(hash))
			{
				return entry->Color;
			}

			if (m_count == m_entries.size())
			{
				// Overflowing names share one color so they stay stable
				KOI_PROF_ASSERT(false, "ColorPalette is full, construct it with a larger capacity");
				return Generator(m_nextIndex, name, GeneratorUserData);
			}
			const uint32_t color = Generator(m_nextIndex++, name, GeneratorUserData);
			m_entries[m_count++] = { hash, color, false };
			return color;
		}

		// Pins a name to a color
		void SetColor(std::string_view name, uint32_t color)
		{
			m_generation++;
			const uint32_t hash = HashName(name);
			if (Entry* entry = Find(hash))
			{
				entry->Color  = color;
				entry->Pinned = true;
				return;
			}

			if (m_count == m_entries.size())
			{
				KOI_PROF_ASSERT(false, "ColorPalette is full, construct it with a larger capacity");
				return;
			}
			m_entries[m_count++] = { hash, color, true };
		}

		// Forgets generated colors, keeps pinned ones
		void Regenerate()
		{
			m_generation++;
			m_nextIndex = 0;
			size_t keptCount = 0;
			for (size_t entryIndex = 0; entryIndex < m_count; entryIndex++)
			{
				if (m_entries[entryIndex].Pinned)
				{
					m_entries[keptCount++] = m_entries[entryIndex];
				}
			}
			m_count = keptCount;
		}

		// Forgets all colors
		void Clear()
		{
			m_generation++;
			m_count     = 0;
			m_nextIndex = 0;
		}

		[[nodiscard]] size_t GetCapacity() const { return m_entries.size(); }
		[[nodiscard]] size_t GetCount() const { return m_count; }

		// Bumped when handed-out colors may have changed
		[[nodiscard]] uint32_t GetGeneration() const { return m_generation; }

	private:
		struct Entry
		{
			uint32_t Hash   = 0;
			uint32_t Color  = 0;
			bool     Pinned = false;
		};

	private:
		[[nodiscard]] Entry* Find(uint32_t hash)
		{
			for (size_t entryIndex = 0; entryIndex < m_count; entryIndex++)
			{
				if (m_entries[entryIndex].Hash == hash)
				{
					return &m_entries[entryIndex];
				}
			}
			return nullptr;
		}

	public:
		ColorGenerator Generator         = &GoldenRatioColor;
		void*          GeneratorUserData = nullptr;

	private:
		std::vector<Entry> m_entries;
		size_t             m_count      = 0;
		size_t             m_nextIndex  = 0;
		uint32_t           m_generation = 0;
	};

	// Shared by default, so a name has the same color on every graph
	[[nodiscard]] inline ColorPalette& DefaultPalette()
	{
		static ColorPalette palette;
		return palette;
	}

	// Graph capacities, allocated once in the constructor
	struct GraphConfig
	{
		size_t FramesCount      = 300;
		size_t MaxTasksPerFrame = 64;
		size_t MaxUniqueNames   = 128; // names that leave the history are recycled
		size_t MaxNameLength    = 48;  // longer names are truncated
#ifndef KOI_PROFILER_NO_MARKERS
		size_t MaxMarkers       = 16;  // oldest is replaced when full
		size_t MaxMarkerLength  = 32;  // longer marker names are truncated
#endif
#ifndef KOI_PROFILER_NO_BUDGETS
		size_t MaxBudgets       = 16;  // names with their own budget
#endif
	};

	struct GraphStyle
	{
		int      FrameWidth           = 3;
		int      FrameSpacing         = 1;
		float    MinTaskHeight        = 1.0f;   // pixels; shorter tasks aren't drawn
		float    LegendWidth          = 260.0f; // pixels; 0 hides the legend
		bool     UseColoredLegendText = false;
		TimeUnit Unit                 = TimeUnit::Milliseconds; // legend, grid, tooltip and table

		uint32_t BackgroundColor      = 0;      // 0 draws no background
		uint32_t BorderColor          = 0;      // 0 uses ImGuiCol_Border
		uint32_t TextColor            = 0;      // 0 uses ImGuiCol_Text
		uint32_t BudgetColor          = IM_COL32(255, 32, 32, 255);
		float    BudgetThickness      = 1.0f;

#ifndef KOI_PROFILER_NO_GRID
		bool     ShowGrid             = true;
		bool     ShowGridLabels       = true;
		float    GridStep             = 0.0f;   // seconds; 0 picks a round step
		uint32_t GridColor            = 0;      // 0 uses ImGuiCol_Border at half alpha
#endif

#ifndef KOI_PROFILER_NO_INTERACTION
		bool     ShowTooltip          = true;
		float    HighlightDimAlpha    = 0.3f;   // alpha of other names while one is hovered; 1 disables
#endif
#ifndef KOI_PROFILER_NO_MARKERS
		uint32_t MarkerColor          = IM_COL32(255, 255, 255, 160); // for markers without a color
#endif

		float    LegendLeftMarkerMargin   = 3.0f;
		float    LegendLeftMarkerWidth    = 5.0f;
		float    LegendMarkerLinkWidth    = 30.0f;
		float    LegendRightMarkerWidth   = 10.0f;
		float    LegendRightMarkerHeight  = 10.0f;
		float    LegendRightMarkerMargin  = 3.0f;
		float    LegendRightMarkerSpacing = 4.0f;
		ImVec2   LegendTextMargin         = ImVec2(5.0f, -3.0f);
	};

	struct AutoScaleSettings
	{
		float Percentile    = 0.95f;    // spikes above this clip
		float Headroom      = 1.25f;
		float SmoothTime    = 0.25f;    // seconds
		float MinTime       = 0.00005f; // seconds
		bool  IncludeBudget = false;    // always fit the budget line
	};

#ifndef KOI_PROFILER_NO_BUDGETS
	// A name's own budget: a fixed time or a share of BudgetTime
	struct NameBudget
	{
		std::string_view Name;
		float            Time  = 0.0f; // seconds; 0 for a share
		float            Share = 0.0f; // of BudgetTime; 0 for a time
	};
#endif

#ifndef KOI_PROFILER_NO_OVERLAY
	// One of a frame's longest names, for the overlay
	struct TopTask
	{
		std::string_view Name;
		float            Time   = 0.0f; // seconds, same-name tasks summed
		uint32_t         Color  = 0;
		float            Budget = 0.0f; // seconds; 0 when none
	};
#endif

	class ProfilerGraph;

	// Passed to a budget callback when a loaded frame goes over a budget
	struct BudgetEvent
	{
		const ProfilerGraph* Graph  = nullptr;
		std::string_view     Name;          // empty for the frame budget
		float                Time   = 0.0f; // seconds
		float                Budget = 0.0f; // seconds
		uint32_t             Calls  = 0;    // tasks with the name this frame; 0 for the frame budget
	};

	using BudgetCallback = void (*)(const BudgetEvent& event, void* userData);

	namespace internal
	{
		template <typename Range>
		using EnableIfRange = std::void_t<decltype(std::begin(std::declval<const Range&>())), decltype(std::end(std::declval<const Range&>()))>;

		template <typename Range>
		using RangeElement = std::remove_cv_t<std::remove_reference_t<decltype(*std::begin(std::declval<const Range&>()))>>;
	}

	class ProfilerGraph
	{
	public:
		explicit ProfilerGraph(const GraphConfig& config = {})
			: m_config(config)
			, m_tasks(config.FramesCount * config.MaxTasksPerFrame)
			, m_frames(config.FramesCount)
			, m_nameTable(config.MaxUniqueNames, config.MaxNameLength)
			, m_names(config.MaxUniqueNames)
			, m_nameHints(config.MaxTasksPerFrame, internal::NameTable::kInvalidId)
			, m_legendNames(config.MaxTasksPerFrame)
			, m_spans(config.FramesCount)
#ifndef KOI_PROFILER_NO_MARKERS
			, m_markers(config.MaxMarkers)
			, m_markerChars(config.MaxMarkers * config.MaxMarkerLength)
#endif
#ifndef KOI_PROFILER_NO_BUDGETS
			, m_budgetNames(config.MaxBudgets, config.MaxNameLength)
			, m_budgets(config.MaxBudgets)
#endif
#ifndef KOI_PROFILER_NO_TABLE
			, m_nameStats(config.MaxUniqueNames)
			, m_statSamples(config.FramesCount)
			, m_tableRows(config.MaxUniqueNames)
#endif
#ifndef KOI_PROFILER_NO_OVERLAY
			, m_topTimes(config.MaxTasksPerFrame)
#endif
		{
			KOI_PROF_ASSERT(config.FramesCount > 0 && config.MaxTasksPerFrame > 0 && config.MaxUniqueNames > 0 && config.MaxNameLength > 0, "GraphConfig capacities must be non-zero");
			m_sortedSpans.reserve(config.FramesCount);
		}

		// Merges consecutive tasks with the same name and color, counting each as a call
		void LoadFrameData(const ProfilerTask* tasks, size_t count)
		{
			LoadTasks(tasks, tasks + count, [](const ProfilerTask& task) -> const ProfilerTask& { return task; });
		}

		// Any type with a TaskTraits specialization
		template <typename T>
		void LoadFrameData(const T* tasks, size_t count)
		{
			LoadTasks(tasks, tasks + count, [](const T& task) -> decltype(auto) { return TaskTraits<T>::ToTask(task); });
		}

		// Any range (std::vector, std::span, an array...) of ProfilerTask or a type with TaskTraits
		template <typename Range, typename = internal::EnableIfRange<Range>>
		void LoadFrameData(const Range& tasks)
		{
			using T = internal::RangeElement<Range>;
			LoadTasks(std::begin(tasks), std::end(tasks), [](const T& task) -> decltype(auto) { return TaskTraits<T>::ToTask(task); });
		}

		// Any range, converted by toTask(element), which returns a ProfilerTask
		template <typename Range, typename ToTask, typename = internal::EnableIfRange<Range>,
			typename = std::enable_if_t<std::is_invocable_r_v<ProfilerTask, ToTask&, const internal::RangeElement<Range>&>>>
		void LoadFrameData(const Range& tasks, ToTask&& toTask)
		{
			LoadTasks(std::begin(tasks), std::end(tasks), toTask);
		}

		// Called by LoadFrameData when the frame's tracked time is over BudgetTime (empty Name), and for every name over its own budget
		void SetBudgetCallback(BudgetCallback callback, void* userData = nullptr)
		{
			m_budgetCallback = callback;
			m_budgetUserData = userData;
		}

		// Any callable taking a const BudgetEvent&. It's stored by pointer, so it must outlive the graph.
		template <typename F>
		void SetBudgetCallback(F& callable)
		{
			static_assert(!std::is_function_v<F>, "Pass functions as a BudgetCallback, taking (const BudgetEvent&, void*)");
			SetBudgetCallback([](const BudgetEvent& event, void* userData) { (*static_cast<F*>(userData))(event); },
				const_cast<void*>(static_cast<const void*>(&callable)));
		}

	private:
		template <typename It, typename ToTask>
		void LoadTasks(It begin, It end, ToTask&& toTask)
		{
			SyncPalette();

			FrameData& frame = m_frames[m_currFrameIndex];
			Task* frameTasks = GetFrameTasks(m_currFrameIndex);

			// The overwritten frame releases its names
			for (size_t taskIndex = 0; taskIndex < frame.TaskCount; taskIndex++)
			{
				m_names[frameTasks[taskIndex].NameId].RefCount--;
			}
			frame.TaskCount = 0;
			frame.TotalTime = 0.0f;
			frame.Span      = 0.0f;

			size_t taskIndex = 0;
			for (It it = begin; it != end; ++it, ++taskIndex)
			{
				const ProfilerTask& task = toTask(*it);
				const bool hasHint = taskIndex < m_nameHints.size();
				const uint32_t nameId = FindOrAddName(task.Name, hasHint ? m_nameHints[taskIndex] : kInvalidNameId, task.NameHash);
				if (nameId == kInvalidNameId)
				{
					continue;
				}
				if (hasHint)
				{
					m_nameHints[taskIndex] = nameId;
				}
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
				if (task.Location)
				{
					m_names[nameId].Location = task.Location;
				}
#endif

				const uint32_t color = task.Color != 0 ? task.Color : GetPaletteColor(nameId, task.Name);
				Task* last = frame.TaskCount > 0 ? &frameTasks[frame.TaskCount - 1] : nullptr;
				if (last && last->NameId == nameId && last->Color == color)
				{
					last->End = float(task.EndTime);
					last->Calls++;
				}
				else if (frame.TaskCount < m_config.MaxTasksPerFrame)
				{
					frameTasks[frame.TaskCount++] = { float(task.StartTime), float(task.EndTime), color, nameId, 1 };
					m_names[nameId].RefCount++;
				}
				else
				{
					KOI_PROF_ASSERT(false, "Too many tasks in one frame, raise GraphConfig::MaxTasksPerFrame");
					break;
				}

				frame.TotalTime += float(task.GetLength());
				frame.Span = std::max(frame.Span, float(task.EndTime));
			}

			if (frame.TaskCount > 0)
			{
				m_spans[m_spanHead] = frame.Span;
				m_spanHead    = (m_spanHead + 1) % m_spans.size();
				m_spanCount   = std::min(m_spanCount + 1, m_spans.size());
				m_spansDirty  = true;
			}

			m_currFrameIndex = (m_currFrameIndex + 1) % m_frames.size();
			m_frameSerial++;
			m_maxTimesDirty = true;
#ifndef KOI_PROFILER_NO_TABLE
			m_tableDirty = true;
#endif

			if (m_budgetCallback)
			{
				ReportBudgets(frame, frameTasks);
			}
		}

	public:
#ifndef KOI_PROFILER_NO_MARKERS
		// Marks the next loaded frame. The name is copied; color 0 uses Style.MarkerColor
		void AddMarker(std::string_view name, uint32_t color = 0)
		{
			if (m_markers.empty())
			{
				return;
			}
			const std::string_view key = name.substr(0, m_config.MaxMarkerLength);
			Marker& marker = m_markers[m_markerHead];
			marker.Frame  = m_frameSerial;
			marker.Color  = color;
			marker.Length = uint32_t(key.size());
			if (!key.empty())
			{
				std::memcpy(&m_markerChars[m_markerHead * m_config.MaxMarkerLength], key.data(), key.size());
			}
			m_markerHead  = (m_markerHead + 1) % m_markers.size();
			m_markerCount = std::min(m_markerCount + 1, m_markers.size());
		}
#endif

#ifndef KOI_PROFILER_NO_BUDGETS
		// Budget for every task with this name, e.g. SetBudget("Physics", 2ms). Replaces a share
		template <typename Rep, typename Period>
		void SetBudget(std::string_view name, std::chrono::duration<Rep, Period> budget)
		{
			SetNameBudget(name, std::chrono::duration<float>(budget).count(), 0.0f);
		}

		// Budget as a share of BudgetTime, e.g. 0.15f. Replaces a time.
		void SetBudgetShare(std::string_view name, float share) { SetNameBudget(name, 0.0f, share); }

		void ClearBudget(std::string_view name) { SetNameBudget(name, 0.0f, 0.0f); }

		// Seconds, shares resolved; 0 when none
		[[nodiscard]] float GetBudget(std::string_view name) const
		{
			return ResolveBudget(m_budgetNames.Find(name));
		}

		// Budget slots; cleared ones have no time or share
		[[nodiscard]] size_t GetNameBudgetCount() const { return m_budgetNames.GetCount(); }
		[[nodiscard]] NameBudget GetNameBudget(size_t index) const
		{
			return { m_budgetNames.Get(uint32_t(index)), m_budgets[index].Time, m_budgets[index].Share };
		}
#endif

		// Draws the graph and legend as one item. Sizes <= 0 fill the region, like ImGui widgets.
		// maxFrameTime 0 uses GetAutoScaleTime(). Returns the clicked frame offset, or -1.
		int Render(ImVec2 size = ImVec2(0.0f, 0.0f), int frameOffset = 0, float maxFrameTime = 0.0f)
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			size.x = std::floor(std::max(size.x > 0.0f ? size.x : available.x + size.x, 1.0f));
			size.y = std::floor(std::max(size.y > 0.0f ? size.y : available.y + size.y, 1.0f));
			if (!ImGui::IsRectVisible(size))
			{
				m_lastVertexCount = 0;
				ImGui::Dummy(size);
				return -1;
			}
			if (maxFrameTime <= 0.0f)
			{
				maxFrameTime = GetAutoScaleTime();
			}

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 widgetPos = ImGui::GetCursorScreenPos();
			const size_t offset = ClampFrameOffset(frameOffset);
			const float legendWidth = std::floor(std::clamp(Style.LegendWidth, 0.0f, size.x - 1.0f));
			const ImVec2 graphSize(size.x - legendWidth, size.y);

			const int vertexStart = drawList->VtxBuffer.Size;
#ifndef KOI_PROFILER_NO_INTERACTION
			m_legendHoveredName = kInvalidNameId;
#endif
			drawList->PushClipRect(widgetPos, widgetPos + size, true);
			RenderGraph(drawList, widgetPos, graphSize, offset, maxFrameTime);
			if (legendWidth > 0.0f)
			{
				RenderLegend(drawList, widgetPos + ImVec2(graphSize.x, 0.0f), ImVec2(legendWidth, size.y), offset, maxFrameTime);
			}
			drawList->PopClipRect();
			m_lastVertexCount = drawList->VtxBuffer.Size - vertexStart;
			ImGui::Dummy(size);

#ifndef KOI_PROFILER_NO_INTERACTION
			// Hover is applied next frame, after the graph has been drawn
			m_highlightName = kInvalidNameId;
			if (!ImGui::IsItemHovered())
			{
				return -1;
			}
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			if (mouse.x >= widgetPos.x + graphSize.x)
			{
				m_highlightName = m_legendHoveredName;
				return -1;
			}

			const size_t hoveredFrame = HitTestFrame(widgetPos, graphSize, offset, mouse.x);
			if (hoveredFrame == kNoFrame)
			{
				return -1;
			}
			const size_t hoveredTask = HitTestTask(hoveredFrame, (widgetPos.y + graphSize.y - 1.0f - mouse.y) * maxFrameTime / graphSize.y);
			if (hoveredTask != kNoTask)
			{
				m_highlightName = GetFrameTasks(GetFrameIndex(hoveredFrame))[hoveredTask].NameId;
			}
			if (Style.ShowTooltip)
			{
				RenderTooltip(hoveredFrame, hoveredTask);
			}
			return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? int(hoveredFrame) : -1;
#else
			return -1;
#endif
		}

#ifndef KOI_PROFILER_NO_TABLE
		// Sortable per-name stats over the history, refreshed at most 4 times a second
		void RenderTable(ImVec2 size = ImVec2(0.0f, 0.0f))
		{
			UpdateTableStats();
			const bool hasFrameBudget = BudgetTime > 0.0f;
#ifndef KOI_PROFILER_NO_BUDGETS
			const bool hasNameBudgets = HasNameBudgets();
#else
			const bool hasNameBudgets = false;
#endif
			const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("##Stats", 8 + int(hasNameBudgets) + int(hasFrameBudget), flags, size))
			{
				return;
			}
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f, TableName);
			ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_None, 1.0f, TableLast);
			ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableAverage);
			ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableMin);
			ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableMax);
			ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableP95);
			ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableFrames);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableCalls);
			if (hasNameBudgets)
			{
				ImGui::TableSetupColumn("Budget", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableNameBudget);
			}
			if (hasFrameBudget)
			{
				ImGui::TableSetupColumn("Frame %", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, TableFrameShare);
			}
			ImGui::TableHeadersRow();

			size_t rowCount = 0;
			for (uint32_t nameId = 0; nameId < m_nameTable.GetCount(); nameId++)
			{
				if (m_nameStats[nameId].Frames > 0)
				{
					m_tableRows[rowCount++] = nameId;
				}
			}
			SortTableRows(rowCount, ImGui::TableGetSortSpecs());

			const float swatchSize = ImGui::GetTextLineHeight();
			const size_t historyFrames = std::min(size_t(m_frameSerial), m_frames.size());
			for (size_t rowIndex = 0; rowIndex < rowCount; rowIndex++)
			{
				const uint32_t nameId = m_tableRows[rowIndex];
				const NameStats& stats = m_nameStats[nameId];
				const std::string_view name = m_nameTable.Get(nameId);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				const ImVec2 swatchPos = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(swatchPos, swatchPos + ImVec2(swatchSize, swatchSize), stats.Color);
				ImGui::Dummy(ImVec2(swatchSize, swatchSize));
				ImGui::SameLine();
				ImGui::TextUnformatted(name.data(), name.data() + name.size());
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
				if (const SourceLocation* location = m_names[nameId].Location; location && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s:%u  %s", internal::GetFileName(location->File), location->Line, location->Function ? location->Function : "");
				}
#endif

#ifndef KOI_PROFILER_NO_BUDGETS
				const float nameBudget = GetNameBudgetTime(nameId);
#else
				const float nameBudget = 0.0f;
#endif
				TableTimeCell(stats.Last, nameBudget);
				TableTimeCell(stats.Average, nameBudget);
				TableTimeCell(stats.Min, nameBudget);
				TableTimeCell(stats.Max, nameBudget);
				TableTimeCell(stats.P95, nameBudget);
				ImGui::TableNextColumn();
				ImGui::Text("%.0f%%", historyFrames > 0 ? double(stats.Frames) * 100.0 / double(historyFrames) : 0.0);
				ImGui::TableNextColumn();
				ImGui::Text(stats.Calls == std::floor(stats.Calls) ? "%.0f" : "%.1f", double(stats.Calls));
				if (hasNameBudgets)
				{
					ImGui::TableNextColumn();
					if (nameBudget > 0.0f)
					{
						char text[32];
						internal::FormatTime(text, sizeof(text), double(nameBudget), Style.Unit);
						ImGui::TextUnformatted(text);
					}
				}
				if (hasFrameBudget)
				{
					ImGui::TableNextColumn();
					ImGui::Text("%.1f%%", double(stats.Average / BudgetTime) * 100.0);
				}
			}
			ImGui::EndTable();
		}
#endif

#ifndef KOI_PROFILER_NO_OVERLAY
		// A frame's longest names, same-name tasks summed. Returns the count written.
		size_t GetTopTasks(int frameOffset, TopTask* topTasks, size_t maxCount)
		{
			const size_t frameIndex = GetFrameIndex(ClampFrameOffset(frameOffset));
			const Task* frameTasks = GetFrameTasks(frameIndex);
			size_t uniqueCount = 0;
			for (size_t taskIndex = 0; taskIndex < m_frames[frameIndex].TaskCount; taskIndex++)
			{
				const Task& task = frameTasks[taskIndex];
				size_t uniqueIndex = 0;
				while (uniqueIndex < uniqueCount && m_legendNames[uniqueIndex] != task.NameId)
				{
					uniqueIndex++;
				}
				if (uniqueIndex == uniqueCount)
				{
					m_legendNames[uniqueCount] = task.NameId;
					m_topTimes[uniqueCount++] = 0.0f;
				}
				m_topTimes[uniqueIndex] += task.End - task.Start;
			}

			const size_t count = std::min(uniqueCount, maxCount);
			for (size_t topIndex = 0; topIndex < count; topIndex++)
			{
				size_t longest = topIndex;
				for (size_t candidate = topIndex + 1; candidate < uniqueCount; candidate++)
				{
					longest = m_topTimes[candidate] > m_topTimes[longest] ? candidate : longest;
				}
				std::swap(m_topTimes[topIndex], m_topTimes[longest]);
				std::swap(m_legendNames[topIndex], m_legendNames[longest]);

				const uint32_t nameId = m_legendNames[topIndex];
				TopTask& top = topTasks[topIndex];
				top.Name   = m_nameTable.Get(nameId);
				top.Time   = m_topTimes[topIndex];
				top.Color  = FindTaskColor(frameIndex, nameId);
#ifndef KOI_PROFILER_NO_BUDGETS
				top.Budget = GetNameBudgetTime(nameId);
#endif
			}
			return count;
		}

		// Tracked time averaged over recent frames
		[[nodiscard]] float GetAverageTotalTime(size_t frameCount) const
		{
			const size_t count = std::min({ frameCount, size_t(m_frameSerial), m_frames.size() });
			float sum = 0.0f;
			for (size_t frameOffset = 0; frameOffset < count; frameOffset++)
			{
				sum += m_frames[GetFrameIndex(frameOffset)].TotalTime;
			}
			return count > 0 ? sum / float(count) : 0.0f;
		}

		// Recent tracked times as a line, newest on the right. maxTime 0 fits the data
		void RenderSparkline(ImDrawList* drawList, ImVec2 pos, ImVec2 size, size_t frameCount, uint32_t color, float maxTime = 0.0f) const
		{
			const size_t count = std::min({ frameCount, size_t(m_frameSerial), m_frames.size() });
			if (count < 2)
			{
				return;
			}
			if (maxTime <= 0.0f)
			{
				maxTime = BudgetTime;
				for (size_t frameOffset = 0; frameOffset < count; frameOffset++)
				{
					maxTime = std::max(maxTime, m_frames[GetFrameIndex(frameOffset)].TotalTime);
				}
			}
			if (maxTime <= 0.0f)
			{
				return;
			}
			if (BudgetTime > 0.0f && BudgetTime <= maxTime)
			{
				const float budgetY = std::floor(pos.y + size.y - BudgetTime / maxTime * size.y) + 0.5f;
				drawList->AddLine(ImVec2(pos.x, budgetY), ImVec2(pos.x + size.x, budgetY), internal::ScaleAlpha(Style.BudgetColor, 0.5f));
			}
			for (size_t pointIndex = 0; pointIndex < count; pointIndex++)
			{
				const float value = m_frames[GetFrameIndex(count - 1 - pointIndex)].TotalTime;
				const float x = pos.x + size.x * float(pointIndex) / float(count - 1);
				const float y = pos.y + size.y - std::min(value / maxTime, 1.0f) * size.y;
				drawList->PathLineTo(ImVec2(x, y));
			}
			drawList->PathStroke(color, ImDrawFlags_None, 1.0f);
		}
#endif

		[[nodiscard]] float GetTotalTaskTime(int frameOffset) const
		{
			return m_frames[GetFrameIndex(ClampFrameOffset(frameOffset))].TotalTime;
		}

		// End of the frame's last task, in seconds
		[[nodiscard]] float GetFrameSpan(int frameOffset) const
		{
			return m_frames[GetFrameIndex(ClampFrameOffset(frameOffset))].Span;
		}

		// Top of the graph for Render's maxFrameTime | Computed lazily
		[[nodiscard]] float GetAutoScaleTime()
		{
			if ((m_spansDirty || AutoScale.Percentile != m_percentileUsed) && m_spanCount > 0)
			{
				m_percentileUsed = AutoScale.Percentile;
				m_sortedSpans.assign(m_spans.begin(), m_spans.begin() + ptrdiff_t(m_spanCount));
				const auto nth = m_sortedSpans.begin() + ptrdiff_t(float(m_spanCount - 1) * std::clamp(AutoScale.Percentile, 0.0f, 1.0f));
				std::nth_element(m_sortedSpans.begin(), nth, m_sortedSpans.end());
				m_percentileSpan = *nth;
				m_spansDirty = false;
			}

			const float budget = AutoScale.IncludeBudget ? BudgetTime : 0.0f;
			const float target = std::max(m_percentileSpan, budget) * AutoScale.Headroom;
			const Clock::time_point now = Clock::now();
			const float deltaTime = std::chrono::duration<float>(now - m_lastScaleTime).count();
			m_lastScaleTime = now;
			if (m_snapAutoScale || AutoScale.SmoothTime <= 0.0f)
			{
				m_autoScaleTime = target;
				m_snapAutoScale = m_spanCount == 0;
			}
			else
			{
				m_autoScaleTime += (target - m_autoScaleTime) * (1.0f - std::exp(-deltaTime / AutoScale.SmoothTime));
			}
			return std::max(m_autoScaleTime, AutoScale.MinTime);
		}

		[[nodiscard]] const GraphConfig& GetConfig() const { return m_config; }

		// Capacity usage, for sizing GraphConfig (names include ones awaiting recycling)
		[[nodiscard]] size_t GetUsedNameCount() const { return m_nameTable.GetCount(); }
		[[nodiscard]] size_t GetMaxTaskCount() const
		{
			size_t maxCount = 0;
			for (const FrameData& frame : m_frames)
			{
				maxCount = std::max(maxCount, frame.TaskCount);
			}
			return maxCount;
		}

		// Vertices added by the last Render
		[[nodiscard]] int GetLastVertexCount() const { return m_lastVertexCount; }

	private:
		using Clock = std::chrono::steady_clock;

		static constexpr uint32_t kInvalidNameId = internal::NameTable::kInvalidId;
		static constexpr size_t   kNotShown      = SIZE_MAX;
		static constexpr size_t   kPicked        = SIZE_MAX - 1;
#if !defined(KOI_PROFILER_NO_INTERACTION) || !defined(KOI_PROFILER_NO_MARKERS)
		static constexpr size_t   kNoFrame       = SIZE_MAX;
#endif
#ifndef KOI_PROFILER_NO_INTERACTION
		static constexpr size_t   kNoTask        = SIZE_MAX;
#endif

#ifndef KOI_PROFILER_NO_TABLE
		static constexpr float kTableRefresh = 0.25f; // seconds

		enum TableColumn : ImGuiID
		{
			TableName,
			TableLast,
			TableAverage,
			TableMin,
			TableMax,
			TableP95,
			TableFrames,
			TableCalls,
			TableNameBudget,
			TableFrameShare,
		};
#endif

		// Floats relative to frame start; precise enough for sub-second spans
		struct Task
		{
			float    Start  = 0.0f;
			float    End    = 0.0f;
			uint32_t Color  = 0;
			uint32_t NameId = kInvalidNameId;
			uint32_t Calls  = 1; // consecutive same-name tasks merged into this one
		};

		struct FrameData
		{
			size_t TaskCount = 0;
			float  TotalTime = 0.0f;
			float  Span      = 0.0f; // end of the last task
		};

		// Indexed by m_nameTable id
		struct NameEntry
		{
			uint32_t              RefCount      = 0;     // tasks using this name; 0 means recyclable
			uint32_t              Color         = 0;     // cached palette color
			bool                  HasColor      = false;
			float                 MaxTime       = 0.0f;  // longest task in the history; decides legend overflow
			size_t                OnScreenIndex = kNotShown;
#ifndef KOI_PROFILER_NO_BUDGETS
			uint32_t              BudgetId      = kInvalidNameId; // cached
#endif
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
			const SourceLocation* Location      = nullptr;
#endif
		};

#ifndef KOI_PROFILER_NO_TABLE
		// Per-name stats for the table, computed only while it's drawn
		struct NameStats
		{
			float    Last    = 0.0f;
			float    Average = 0.0f;
			float    Min     = 0.0f;
			float    Max     = 0.0f;
			float    P95     = 0.0f;
			float    Calls   = 0.0f; // per frame it appears in
			uint32_t Frames  = 0;
			uint32_t Color   = 0;
		};
#endif

#ifndef KOI_PROFILER_NO_MARKERS
		struct Marker
		{
			uint64_t Frame  = 0; // serial of the marked frame
			uint32_t Color  = 0;
			uint32_t Length = 0;
		};
#endif

#ifndef KOI_PROFILER_NO_BUDGETS
		struct BudgetEntry
		{
			float Time  = 0.0f;
			float Share = 0.0f;
		};
#endif

	private:
		[[nodiscard]] Task* GetFrameTasks(size_t frameIndex) { return &m_tasks[frameIndex * m_config.MaxTasksPerFrame]; }
		[[nodiscard]] const Task* GetFrameTasks(size_t frameIndex) const { return &m_tasks[frameIndex * m_config.MaxTasksPerFrame]; }

		[[nodiscard]] size_t ClampFrameOffset(int frameOffset) const
		{
			return std::min(size_t(std::max(frameOffset, 0)), m_frames.size() - 1);
		}

		[[nodiscard]] size_t GetFrameIndex(size_t frameOffset) const
		{
			return (m_currFrameIndex + 2 * m_frames.size() - frameOffset - 1) % m_frames.size();
		}

#ifndef KOI_PROFILER_NO_OVERLAY
		[[nodiscard]] uint32_t FindTaskColor(size_t frameIndex, uint32_t nameId) const
		{
			const Task* frameTasks = GetFrameTasks(frameIndex);
			for (size_t taskIndex = 0; taskIndex < m_frames[frameIndex].TaskCount; taskIndex++)
			{
				if (frameTasks[taskIndex].NameId == nameId)
				{
					return frameTasks[taskIndex].Color;
				}
			}
			return 0;
		}
#endif

#ifndef KOI_PROFILER_NO_MARKERS
		[[nodiscard]] std::string_view GetMarkerName(size_t markerIndex) const
		{
			return std::string_view(m_markerChars.data() + markerIndex * m_config.MaxMarkerLength, m_markers[markerIndex].Length);
		}

		// kNoFrame once it left the history
		[[nodiscard]] size_t GetMarkerFrameOffset(size_t markerIndex) const
		{
			const uint64_t frame = m_markers[markerIndex].Frame;
			if (frame >= m_frameSerial || m_frameSerial - 1 - frame >= m_frames.size())
			{
				return kNoFrame;
			}
			return size_t(m_frameSerial - 1 - frame);
		}
#endif

#ifndef KOI_PROFILER_NO_BUDGETS
		void SetNameBudget(std::string_view name, float time, float share)
		{
			uint32_t budgetId = m_budgetNames.Find(name);
			const bool clearing = time <= 0.0f && share <= 0.0f;
			if (budgetId == kInvalidNameId)
			{
				if (clearing)
				{
					return;
				}
				// Reuse a cleared slot first
				for (uint32_t candidate = 0; candidate < m_budgetNames.GetCount() && budgetId == kInvalidNameId; candidate++)
				{
					if (m_budgets[candidate].Time <= 0.0f && m_budgets[candidate].Share <= 0.0f)
					{
						budgetId = candidate;
						m_budgetNames.Assign(budgetId, name);
					}
				}
				if (budgetId == kInvalidNameId)
				{
					budgetId = m_budgetNames.Add(name);
				}
				if (budgetId == kInvalidNameId)
				{
					KOI_PROF_ASSERT(false, "Too many name budgets, raise GraphConfig::MaxBudgets");
					return;
				}
			}
			m_budgets[budgetId] = { std::max(time, 0.0f), std::max(share, 0.0f) };

			// Budgets change rarely, so every name re-resolves
			for (uint32_t nameId = 0; nameId < m_nameTable.GetCount(); nameId++)
			{
				m_names[nameId].BudgetId = m_budgetNames.Find(m_nameTable.Get(nameId));
			}
		}

		[[nodiscard]] float ResolveBudget(uint32_t budgetId) const
		{
			if (budgetId == kInvalidNameId)
			{
				return 0.0f;
			}
			const BudgetEntry& entry = m_budgets[budgetId];
			return entry.Time > 0.0f ? entry.Time : entry.Share * BudgetTime;
		}

		[[nodiscard]] float GetNameBudgetTime(uint32_t nameId) const { return ResolveBudget(m_names[nameId].BudgetId); }

		[[nodiscard]] bool HasNameBudgets() const
		{
			for (uint32_t budgetId = 0; budgetId < m_budgetNames.GetCount(); budgetId++)
			{
				if (ResolveBudget(budgetId) > 0.0f)
				{
					return true;
				}
			}
			return false;
		}
#endif

		void ReportBudgets(const FrameData& frame, const Task* frameTasks) const
		{
			BudgetEvent event;
			event.Graph = this;
			if (BudgetTime > 0.0f && frame.TotalTime > BudgetTime)
			{
				event.Time   = frame.TotalTime;
				event.Budget = BudgetTime;
				m_budgetCallback(event, m_budgetUserData);
			}
#ifndef KOI_PROFILER_NO_BUDGETS
			for (size_t taskIndex = 0; taskIndex < frame.TaskCount; taskIndex++)
			{
				const uint32_t nameId = frameTasks[taskIndex].NameId;
				const float budget = GetNameBudgetTime(nameId);
				bool seen = false;
				for (size_t earlier = 0; earlier < taskIndex && !seen; earlier++)
				{
					seen = frameTasks[earlier].NameId == nameId;
				}
				if (budget <= 0.0f || seen)
				{
					continue;
				}

				float time = 0.0f;
				uint32_t calls = 0;
				for (size_t other = taskIndex; other < frame.TaskCount; other++)
				{
					if (frameTasks[other].NameId == nameId)
					{
						time += frameTasks[other].End - frameTasks[other].Start;
						calls += frameTasks[other].Calls;
					}
				}
				if (time > budget)
				{
					event.Name   = m_nameTable.Get(nameId);
					event.Time   = time;
					event.Budget = budget;
					event.Calls  = calls;
					m_budgetCallback(event, m_budgetUserData);
				}
			}
#else
			(void)frameTasks;
#endif
		}

		[[nodiscard]] uint32_t FindOrAddName(std::string_view name, uint32_t hintId, uint32_t nameHash)
		{
			uint32_t nameId = m_nameTable.Find(name, hintId, nameHash);
			if (nameId != kInvalidNameId)
			{
				return nameId;
			}

			nameId = m_nameTable.Add(name, nameHash);
			if (nameId == kInvalidNameId)
			{
				// Recycle a name that left the history
				for (uint32_t candidate = 0; candidate < m_nameTable.GetCount(); candidate++)
				{
					if (m_names[candidate].RefCount == 0)
					{
						nameId = candidate;
						m_nameTable.Assign(nameId, name, nameHash);
						break;
					}
				}
			}

			if (nameId == kInvalidNameId)
			{
				KOI_PROF_ASSERT(false, "Too many distinct task names in the history, raise GraphConfig::MaxUniqueNames");
				return kInvalidNameId;
			}
			m_names[nameId] = NameEntry{};
#ifndef KOI_PROFILER_NO_BUDGETS
			m_names[nameId].BudgetId = m_budgetNames.Find(name);
#endif
#ifndef KOI_PROFILER_NO_TABLE
			m_nameStats[nameId] = NameStats{};
#endif
			return nameId;
		}

		// Drop cached colors when the palette changed
		void SyncPalette()
		{
			if (Palette == m_syncedPalette && Palette->GetGeneration() == m_syncedGeneration)
			{
				return;
			}
			m_syncedPalette    = Palette;
			m_syncedGeneration = Palette->GetGeneration();
			for (uint32_t nameId = 0; nameId < m_nameTable.GetCount(); nameId++)
			{
				m_names[nameId].HasColor = false;
			}
		}

		[[nodiscard]] uint32_t GetPaletteColor(uint32_t nameId, std::string_view name)
		{
			NameEntry& entry = m_names[nameId];
			if (!entry.HasColor)
			{
				entry.Color    = Palette->GetColor(name);
				entry.HasColor = true;
			}
			return entry.Color;
		}

		void UpdateMaxTimes()
		{
			if (!m_maxTimesDirty)
			{
				return;
			}
			m_maxTimesDirty = false;

			for (uint32_t nameId = 0; nameId < m_nameTable.GetCount(); nameId++)
			{
				m_names[nameId].MaxTime = 0.0f;
			}
			for (size_t frameIndex = 0; frameIndex < m_frames.size(); frameIndex++)
			{
				const Task* frameTasks = GetFrameTasks(frameIndex);
				for (size_t taskIndex = 0; taskIndex < m_frames[frameIndex].TaskCount; taskIndex++)
				{
					const Task& task = frameTasks[taskIndex];
					NameEntry& entry = m_names[task.NameId];
					entry.MaxTime = std::max(entry.MaxTime, task.End - task.Start);
				}
			}
		}

#ifndef KOI_PROFILER_NO_TABLE
		// Throttled: one history pass per name
		void UpdateTableStats()
		{
			const Clock::time_point now = Clock::now();
			if (!m_tableDirty || std::chrono::duration<float>(now - m_tableTime).count() < kTableRefresh)
			{
				return;
			}
			m_tableDirty = false;
			m_tableTime  = now;

			const size_t historyFrames = std::min(size_t(m_frameSerial), m_frames.size());
			for (uint32_t nameId = 0; nameId < m_nameTable.GetCount(); nameId++)
			{
				NameStats& stats = m_nameStats[nameId];
				stats = NameStats{};
				if (m_names[nameId].RefCount == 0)
				{
					continue;
				}

				size_t sampleCount = 0;
				double sum = 0.0;
				uint64_t calls = 0;
				for (size_t frameOffset = 0; frameOffset < historyFrames; frameOffset++)
				{
					const size_t frameIndex = GetFrameIndex(frameOffset);
					const Task* frameTasks = GetFrameTasks(frameIndex);
					float frameTime = 0.0f;
					uint32_t frameCalls = 0;
					for (size_t taskIndex = 0; taskIndex < m_frames[frameIndex].TaskCount; taskIndex++)
					{
						const Task& task = frameTasks[taskIndex];
						if (task.NameId == nameId)
						{
							frameTime += task.End - task.Start;
							frameCalls += task.Calls;
							stats.Color = task.Color;
						}
					}
					if (frameTime <= 0.0f)
					{
						continue;
					}
					calls += frameCalls;
					if (frameOffset == 0)
					{
						stats.Last = frameTime;
					}
					stats.Min = sampleCount > 0 ? std::min(stats.Min, frameTime) : frameTime;
					stats.Max = std::max(stats.Max, frameTime);
					sum += double(frameTime);
					m_statSamples[sampleCount++] = frameTime;
				}
				if (sampleCount == 0)
				{
					continue;
				}

				stats.Frames  = uint32_t(sampleCount);
				stats.Average = float(sum / double(sampleCount));
				stats.Calls   = float(double(calls) / double(sampleCount));
				const auto nth = m_statSamples.begin() + ptrdiff_t(float(sampleCount - 1) * 0.95f);
				std::nth_element(m_statSamples.begin(), nth, m_statSamples.begin() + ptrdiff_t(sampleCount));
				stats.P95 = *nth;
			}
		}

		void SortTableRows(size_t rowCount, const ImGuiTableSortSpecs* sortSpecs)
		{
			if (!sortSpecs || sortSpecs->SpecsCount == 0)
			{
				return;
			}
			const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
			const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
			const ImGuiID column = spec.ColumnUserID;
			std::sort(m_tableRows.begin(), m_tableRows.begin() + ptrdiff_t(rowCount), [&](uint32_t left, uint32_t right)
			{
				if (column == TableName)
				{
					return ascending ? m_nameTable.Get(left) < m_nameTable.Get(right) : m_nameTable.Get(right) < m_nameTable.Get(left);
				}
				const float leftKey = GetSortKey(left, column);
				const float rightKey = GetSortKey(right, column);
				return ascending ? leftKey < rightKey : rightKey < leftKey;
			});
		}

		[[nodiscard]] float GetSortKey(uint32_t nameId, ImGuiID column) const
		{
			const NameStats& stats = m_nameStats[nameId];
			switch (column)
			{
			case TableLast:   return stats.Last;
			case TableMin:    return stats.Min;
			case TableMax:    return stats.Max;
			case TableP95:    return stats.P95;
			case TableFrames: return float(stats.Frames);
			case TableCalls:  return stats.Calls;
#ifndef KOI_PROFILER_NO_BUDGETS
			case TableNameBudget: return GetNameBudgetTime(nameId);
#endif
			default:          return stats.Average; // frame share is the average over the budget
			}
		}

		// Red when over the name's budget
		void TableTimeCell(float seconds, float budget) const
		{
			char text[32];
			internal::FormatTime(text, sizeof(text), double(seconds), Style.Unit);
			ImGui::TableNextColumn();
#ifndef KOI_PROFILER_NO_BUDGETS
			if (budget > 0.0f && seconds > budget)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(internal::kOverBudgetColor), "%s", text);
				return;
			}
#else
			(void)budget;
#endif
			ImGui::TextUnformatted(text);
		}
#endif

#ifndef KOI_PROFILER_NO_INTERACTION
		[[nodiscard]] size_t HitTestFrame(ImVec2 graphPos, ImVec2 graphSize, size_t frameOffset, float mouseX) const
		{
			const float step = float(std::max(Style.FrameWidth + Style.FrameSpacing, 1));
			const float right = graphPos.x + graphSize.x - 1.0f;
			if (mouseX > right)
			{
				return kNoFrame;
			}
			const size_t frameNumber = size_t((right - mouseX) / step);
			const float columnLeft = right - float(Style.FrameWidth) - step * float(frameNumber);
			if (columnLeft < graphPos.x + 1.0f || frameOffset + frameNumber >= std::min(size_t(m_frameSerial), m_frames.size()))
			{
				return kNoFrame;
			}
			return frameOffset + frameNumber;
		}

		[[nodiscard]] size_t HitTestTask(size_t frameOffset, float time) const
		{
			const size_t frameIndex = GetFrameIndex(frameOffset);
			const Task* frameTasks = GetFrameTasks(frameIndex);
			for (size_t taskIndex = m_frames[frameIndex].TaskCount; taskIndex-- > 0;)
			{
				if (time >= frameTasks[taskIndex].Start && time < frameTasks[taskIndex].End)
				{
					return taskIndex;
				}
			}
			return kNoTask;
		}

		void RenderTooltip(size_t frameOffset, size_t hoveredTask) const
		{
			if (!ImGui::BeginTooltip())
			{
				return;
			}
			const size_t frameIndex = GetFrameIndex(frameOffset);
			const FrameData& frame = m_frames[frameIndex];
			const Task* frameTasks = GetFrameTasks(frameIndex);

			char spanText[32];
			char totalText[32];
			internal::FormatTime(spanText, sizeof(spanText), double(frame.Span), Style.Unit);
			internal::FormatTime(totalText, sizeof(totalText), double(frame.TotalTime), Style.Unit);
			if (frameOffset == 0)
			{
				ImGui::Text("Latest frame  %s  (tracked %s)", spanText, totalText);
			}
			else
			{
				ImGui::Text("%d frames back  %s  (tracked %s)", int(frameOffset), spanText, totalText);
			}

#ifndef KOI_PROFILER_NO_MARKERS
			for (size_t markerIndex = 0; markerIndex < m_markerCount; markerIndex++)
			{
				if (GetMarkerFrameOffset(markerIndex) == frameOffset)
				{
					const std::string_view name = GetMarkerName(markerIndex);
					const uint32_t color = m_markers[markerIndex].Color != 0 ? m_markers[markerIndex].Color : Style.MarkerColor;
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "| %.*s", int(name.size()), name.data());
				}
			}
#endif

			ImGui::Separator();
			const float swatchSize = ImGui::GetTextLineHeight();
			for (size_t taskIndex = frame.TaskCount; taskIndex-- > 0;)
			{
				const Task& task = frameTasks[taskIndex];
				const std::string_view name = m_nameTable.Get(task.NameId);
				const float taskTime = task.End - task.Start;
				char timeText[48];
				internal::FormatTime(timeText, sizeof(timeText), double(taskTime), Style.Unit);

				const ImVec2 swatchPos = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(swatchPos, swatchPos + ImVec2(swatchSize, swatchSize), task.Color);
				ImGui::Dummy(ImVec2(swatchSize, swatchSize));
				ImGui::SameLine();

				ImVec4 textColor = ImGui::GetStyleColorVec4(taskIndex == hoveredTask ? ImGuiCol_Text : ImGuiCol_TextDisabled);
#ifndef KOI_PROFILER_NO_BUDGETS
				if (const float budget = GetNameBudgetTime(task.NameId); budget > 0.0f)
				{
					char budgetText[32];
					internal::FormatTime(budgetText, sizeof(budgetText), double(budget), Style.Unit);
					const size_t length = std::strlen(timeText);
					std::snprintf(timeText + length, sizeof(timeText) - length, " / %s", budgetText);
					if (taskTime > budget)
					{
						textColor = ImGui::ColorConvertU32ToFloat4(internal::kOverBudgetColor);
					}
				}
#endif
				char callsText[16] = "";
				if (task.Calls > 1)
				{
					std::snprintf(callsText, sizeof(callsText), "  x%u", task.Calls);
				}
				ImGui::TextColored(textColor, "%10s  %.*s%s", timeText, int(name.size()), name.data(), callsText);
			}

#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
			if (hoveredTask != kNoTask)
			{
				if (const SourceLocation* location = m_names[frameTasks[hoveredTask].NameId].Location)
				{
					ImGui::Separator();
					ImGui::TextDisabled("%s:%u", internal::GetFileName(location->File), location->Line);
					if (location->Function)
					{
						ImGui::TextDisabled("%s", location->Function);
					}
				}
			}
#endif
			ImGui::EndTooltip();
		}
#endif

#ifndef KOI_PROFILER_NO_GRID
		void RenderGrid(ImDrawList* drawList, ImVec2 graphPos, ImVec2 graphSize, float maxFrameTime, bool labels) const
		{
			const float pixelsPerSecond = graphSize.y / maxFrameTime;
			float step = Style.GridStep;
			if (step <= 0.0f)
			{
				// Smallest 1/2/5 x 10^n step that keeps lines a few text lines apart
				const float minStep = ImGui::GetFontSize() * 2.5f / pixelsPerSecond;
				const float power = std::pow(10.0f, std::floor(std::log10(minStep)));
				step = power * (minStep <= power ? 1.0f : minStep <= 2.0f * power ? 2.0f : minStep <= 5.0f * power ? 5.0f : 10.0f);
			}
			if (step * pixelsPerSecond < 2.0f)
			{
				return;
			}

			const uint32_t lineColor = Style.GridColor != 0 ? Style.GridColor : ImGui::GetColorU32(ImGuiCol_Border, 0.5f);
			const uint32_t textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const internal::TimeUnitInfo& unit = internal::GetTimeUnitInfo(Style.Unit);
			const float bottom = graphPos.y + graphSize.y - 1.0f;
			for (int lineIndex = 1;; lineIndex++)
			{
				const float y = std::floor(bottom - float(lineIndex) * step * pixelsPerSecond);
				if (y <= graphPos.y + 1.0f)
				{
					break;
				}
				if (labels)
				{
					char text[32];
					std::snprintf(text, sizeof(text), "%g %s", double(float(lineIndex) * step) * unit.PerSecond, unit.Suffix);
					drawList->AddText(ImVec2(graphPos.x + 4.0f, y - ImGui::GetFontSize()), textColor, text);
				}
				else
				{
					drawList->AddLine(ImVec2(graphPos.x + 1.0f, y), ImVec2(graphPos.x + graphSize.x - 1.0f, y), lineColor);
				}
			}
		}
#endif

		void RenderGraph(ImDrawList* drawList, ImVec2 graphPos, ImVec2 graphSize, size_t frameOffset, float maxFrameTime) const
		{
			if (Style.BackgroundColor != 0)
			{
				drawList->AddRectFilled(graphPos, graphPos + graphSize, Style.BackgroundColor);
			}
			drawList->AddRect(graphPos, graphPos + graphSize, Style.BorderColor != 0 ? Style.BorderColor : ImGui::GetColorU32(ImGuiCol_Border));
#ifndef KOI_PROFILER_NO_GRID
			if (Style.ShowGrid)
			{
				RenderGrid(drawList, graphPos, graphSize, maxFrameTime, false);
			}
#endif

			const float graphTop = graphPos.y + 1.0f;
			const float pixelsPerSecond = graphSize.y / maxFrameTime;
#ifndef KOI_PROFILER_NO_INTERACTION
			const bool dimOthers = m_highlightName != kInvalidNameId && Style.HighlightDimAlpha < 1.0f;
#endif
			for (size_t frameNumber = 0; frameNumber < m_frames.size(); frameNumber++)
			{
				const ImVec2 framePos = graphPos + ImVec2(graphSize.x - 1.0f - float(Style.FrameWidth) - float(Style.FrameWidth + Style.FrameSpacing) * float(frameNumber), graphSize.y - 1.0f);
				if (framePos.x < graphPos.x + 1.0f || frameOffset + frameNumber >= m_frames.size())
				{
					break;
				}

				const size_t frameIndex = GetFrameIndex(frameOffset + frameNumber);
				const size_t taskCount = m_frames[frameIndex].TaskCount;
				if (taskCount == 0)
				{
					continue;
				}

				// Reserve the whole column to skip AddRectFilled's per-call checks
				const Task* frameTasks = GetFrameTasks(frameIndex);
				drawList->PrimReserve(int(taskCount) * 6, int(taskCount) * 4);
				size_t drawnCount = 0;
				for (size_t taskIndex = 0; taskIndex < taskCount; taskIndex++)
				{
					const Task& task = frameTasks[taskIndex];
					const float bottom = framePos.y - task.Start * pixelsPerSecond;
					const float top = std::max(framePos.y - task.End * pixelsPerSecond, graphTop); // clipped anyway
					if (bottom - top > Style.MinTaskHeight && (task.Color & IM_COL32_A_MASK) != 0)
					{
#ifndef KOI_PROFILER_NO_INTERACTION
						const uint32_t color = dimOthers && task.NameId != m_highlightName ? internal::ScaleAlpha(task.Color, Style.HighlightDimAlpha) : task.Color;
#else
						const uint32_t color = task.Color;
#endif
						drawList->PrimRect(ImVec2(framePos.x, top), ImVec2(framePos.x + float(Style.FrameWidth), bottom), color);
						drawnCount++;
					}
				}
				drawList->PrimUnreserve(int(taskCount - drawnCount) * 6, int(taskCount - drawnCount) * 4);
			}

#ifndef KOI_PROFILER_NO_MARKERS
			RenderMarkers(drawList, graphPos, graphSize, frameOffset);
#endif
#ifndef KOI_PROFILER_NO_GRID
			if (Style.ShowGrid && Style.ShowGridLabels)
			{
				RenderGrid(drawList, graphPos, graphSize, maxFrameTime, true);
			}
#endif

			if (BudgetTime > 0.0f)
			{
				const float budgetY = graphPos.y + graphSize.y - 1.0f - BudgetTime * pixelsPerSecond;
				if (budgetY > graphPos.y)
				{
					drawList->AddLine(ImVec2(graphPos.x + 1.0f, budgetY), ImVec2(graphPos.x + graphSize.x - 1.0f, budgetY), Style.BudgetColor, Style.BudgetThickness);
				}
				else
				{
					// Budget is above the graph: point up at it
					const float markerSize = std::floor(ImGui::GetFontSize() * 0.35f);
					const ImVec2 tip(graphPos.x + graphSize.x - 3.0f - markerSize, graphPos.y + 3.0f);
					drawList->AddTriangleFilled(tip, tip + ImVec2(markerSize, markerSize * 1.5f), tip + ImVec2(-markerSize, markerSize * 1.5f), Style.BudgetColor);
				}
			}
		}

#ifndef KOI_PROFILER_NO_MARKERS
		// A line through each marked frame, flagged at the top
		void RenderMarkers(ImDrawList* drawList, ImVec2 graphPos, ImVec2 graphSize, size_t frameOffset) const
		{
			const float step = float(Style.FrameWidth + Style.FrameSpacing);
			const float flagSize = std::floor(ImGui::GetFontSize() * 0.3f);
			for (size_t markerIndex = 0; markerIndex < m_markerCount; markerIndex++)
			{
				const size_t markerOffset = GetMarkerFrameOffset(markerIndex);
				if (markerOffset == kNoFrame || markerOffset < frameOffset)
				{
					continue;
				}
				const float x = std::floor(graphPos.x + graphSize.x - 1.0f - float(Style.FrameWidth) * 0.5f - step * float(markerOffset - frameOffset));
				if (x < graphPos.x + 1.0f)
				{
					continue;
				}
				const uint32_t color = m_markers[markerIndex].Color != 0 ? m_markers[markerIndex].Color : Style.MarkerColor;
				drawList->AddLine(ImVec2(x, graphPos.y + 1.0f), ImVec2(x, graphPos.y + graphSize.y - 1.0f), color);
				drawList->AddTriangleFilled(ImVec2(x, graphPos.y + 1.0f), ImVec2(x + flagSize * 2.0f, graphPos.y + 1.0f + flagSize), ImVec2(x, graphPos.y + 1.0f + flagSize * 2.0f), color);
			}
		}
#endif

		// Names in stacking order; on overflow, those with the longest tasks win
		void RenderLegend(ImDrawList* drawList, ImVec2 legendPos, ImVec2 legendSize, size_t frameOffset, float maxFrameTime)
		{
			const size_t frameIndex = GetFrameIndex(frameOffset);
			const Task* frameTasks = GetFrameTasks(frameIndex);
			const size_t taskCount = m_frames[frameIndex].TaskCount;
			const size_t maxEntries = size_t(std::max(legendSize.y / (Style.LegendRightMarkerHeight + Style.LegendRightMarkerSpacing), 0.0f));

			for (size_t taskIndex = 0; taskIndex < taskCount; taskIndex++)
			{
				m_names[frameTasks[taskIndex].NameId].OnScreenIndex = kNotShown;
			}
			size_t uniqueCount = 0;
			for (size_t taskIndex = 0; taskIndex < taskCount; taskIndex++)
			{
				NameEntry& entry = m_names[frameTasks[taskIndex].NameId];
				if (entry.OnScreenIndex == kNotShown)
				{
					entry.OnScreenIndex = kPicked;
					m_legendNames[uniqueCount++] = frameTasks[taskIndex].NameId;
				}
			}
			if (uniqueCount > maxEntries)
			{
				UpdateMaxTimes();
				const auto begin = m_legendNames.begin();
				std::nth_element(begin, begin + ptrdiff_t(maxEntries), begin + ptrdiff_t(uniqueCount), [this](uint32_t left, uint32_t right) { return m_names[left].MaxTime > m_names[right].MaxTime; });
				for (size_t dropped = maxEntries; dropped < uniqueCount; dropped++)
				{
					m_names[m_legendNames[dropped]].OnScreenIndex = kNotShown;
				}
			}

			const internal::TimeUnitInfo& unit = internal::GetTimeUnitInfo(Style.Unit);
			char widestText[16];
			std::snprintf(widestText, sizeof(widestText), "[%s", unit.WidestValue);
			const float nameOffset = ImGui::CalcTextSize(widestText).x;
			const uint32_t styleTextColor = Style.TextColor != 0 ? Style.TextColor : ImGui::GetColorU32(ImGuiCol_Text);
			const float pixelsPerSecond = legendSize.y / maxFrameTime;
#ifndef KOI_PROFILER_NO_INTERACTION
			const bool dimOthers = m_highlightName != kInvalidNameId && Style.HighlightDimAlpha < 1.0f;
#endif
			size_t shownCount = 0;
			for (size_t taskIndex = 0; taskIndex < taskCount; taskIndex++)
			{
				const Task& task = frameTasks[taskIndex];
				NameEntry& entry = m_names[task.NameId];
				if (entry.OnScreenIndex != kPicked)
				{
					continue;
				}
				entry.OnScreenIndex = shownCount++;

				ImVec2 leftMin = legendPos + ImVec2(Style.LegendLeftMarkerMargin, legendSize.y);
				ImVec2 leftMax = leftMin + ImVec2(Style.LegendLeftMarkerWidth, 0.0f);
				leftMin.y -= task.Start * pixelsPerSecond;
				leftMax.y -= task.End * pixelsPerSecond;

				const ImVec2 rightMin = legendPos + ImVec2(Style.LegendLeftMarkerMargin + Style.LegendLeftMarkerWidth + Style.LegendMarkerLinkWidth, legendSize.y - Style.LegendRightMarkerMargin - (Style.LegendRightMarkerHeight + Style.LegendRightMarkerSpacing) * float(entry.OnScreenIndex));
				const ImVec2 rightMax = rightMin + ImVec2(Style.LegendRightMarkerWidth, -Style.LegendRightMarkerHeight);
#ifndef KOI_PROFILER_NO_INTERACTION
				const float dim = dimOthers && task.NameId != m_highlightName ? Style.HighlightDimAlpha : 1.0f;
#else
				const float dim = 1.0f;
#endif
				RenderTaskMarker(drawList, leftMin, leftMax, rightMin, rightMax, internal::ScaleAlpha(task.Color, dim));

				uint32_t textColor = Style.UseColoredLegendText ? task.Color : styleTextColor;
				const char* overBudget = "";
#ifndef KOI_PROFILER_NO_BUDGETS
				if (const float budget = GetNameBudgetTime(task.NameId); budget > 0.0f && task.End - task.Start > budget)
				{
					textColor  = internal::kOverBudgetColor;
					overBudget = " !";
				}
#endif
				textColor = internal::ScaleAlpha(textColor, dim);
				const std::string_view name = m_nameTable.Get(task.NameId);

				char timeText[32];
				std::snprintf(timeText, sizeof(timeText), "[%.*f", unit.Decimals, double(task.End - task.Start) * unit.PerSecond);
				char nameText[128];
				std::snprintf(nameText, sizeof(nameText), "%s] %.*s%s", unit.Suffix, int(name.size()), name.data(), overBudget);

				drawList->AddText(rightMax + Style.LegendTextMargin + ImVec2(nameOffset - ImGui::CalcTextSize(timeText).x, 0.0f), textColor, timeText);
				drawList->AddText(rightMax + Style.LegendTextMargin + ImVec2(nameOffset, 0.0f), textColor, nameText);

#ifndef KOI_PROFILER_NO_INTERACTION
				// Hovering the whole row highlights its name
				const float rowSpacing = Style.LegendRightMarkerSpacing * 0.5f;
				if (ImGui::IsMouseHoveringRect(ImVec2(rightMin.x, rightMax.y - rowSpacing), ImVec2(legendPos.x + legendSize.x, rightMin.y + rowSpacing), false))
				{
					m_legendHoveredName = task.NameId;
				}
#endif
			}
		}

		static void RenderTaskMarker(ImDrawList* drawList, ImVec2 leftMin, ImVec2 leftMax, ImVec2 rightMin, ImVec2 rightMax, uint32_t color)
		{
			drawList->AddRectFilled(leftMin, leftMax, color);
			drawList->AddRectFilled(rightMin, rightMax, color);
			const std::array<ImVec2, 4> points = {
				ImVec2(leftMax.x, leftMin.y),
				ImVec2(leftMax.x, leftMax.y),
				ImVec2(rightMin.x, rightMax.y),
				ImVec2(rightMin.x, rightMin.y)
			};
			drawList->AddConvexPolyFilled(points.data(), int(points.size()), color);
		}

	public:
		GraphStyle        Style;
		AutoScaleSettings AutoScale;
		ColorPalette*     Palette    = &DefaultPalette(); // must not be null
		float             BudgetTime = 0.0f;              // seconds; 0 hides the budget line

	private:
		GraphConfig              m_config;
		std::vector<Task>        m_tasks;       // FramesCount x MaxTasksPerFrame
		std::vector<FrameData>   m_frames;
		internal::NameTable      m_nameTable;
		std::vector<NameEntry>   m_names;
		std::vector<uint32_t>    m_nameHints;   // each input position's name id last frame
		std::vector<uint32_t>    m_legendNames; // legend and top-task scratch
		std::vector<float>       m_spans;       // ring buffer of frame end times
		std::vector<float>       m_sortedSpans;
#ifndef KOI_PROFILER_NO_MARKERS
		std::vector<Marker>      m_markers;     // ring buffer
		std::vector<char>        m_markerChars; // MaxMarkers x MaxMarkerLength
		size_t                   m_markerHead  = 0;
		size_t                   m_markerCount = 0;
#endif
#ifndef KOI_PROFILER_NO_BUDGETS
		internal::NameTable      m_budgetNames;
		std::vector<BudgetEntry> m_budgets;
#endif
#ifndef KOI_PROFILER_NO_TABLE
		std::vector<NameStats>   m_nameStats;   // table only
		std::vector<float>       m_statSamples; // table scratch
		std::vector<uint32_t>    m_tableRows;   // table scratch
		Clock::time_point        m_tableTime  = {};
		bool                     m_tableDirty = false;
#endif
#ifndef KOI_PROFILER_NO_OVERLAY
		std::vector<float>       m_topTimes;    // top task scratch
#endif
#ifndef KOI_PROFILER_NO_INTERACTION
		uint32_t                 m_highlightName     = kInvalidNameId;
		uint32_t                 m_legendHoveredName = kInvalidNameId;
#endif
		const ColorPalette*      m_syncedPalette    = nullptr;
		uint32_t                 m_syncedGeneration = 0;
		BudgetCallback           m_budgetCallback   = nullptr;
		void*                    m_budgetUserData   = nullptr;
		Clock::time_point        m_lastScaleTime    = Clock::now();
		uint64_t                 m_frameSerial      = 0; // frames loaded so far
		size_t                   m_currFrameIndex   = 0;
		size_t                   m_spanHead         = 0;
		size_t                   m_spanCount        = 0;
		int                      m_lastVertexCount  = 0;
		float                    m_percentileSpan   = 1.0f / 60.0f / 1.25f;
		float                    m_percentileUsed   = -1.0f;
		float                    m_autoScaleTime    = 1.0f / 60.0f;
		bool                     m_spansDirty       = false;
		bool                     m_snapAutoScale    = true;
		bool                     m_maxTimesDirty    = false;
	};

#ifndef KOI_PROFILER_NO_EDITORS
	// Setting editors, like ImGui::ShowStyleEditor | Each returns true on change

	inline bool ShowStyleEditor(GraphStyle& style)
	{
		bool changed = false;
		ImGui::PushID(&style);
		changed |= ImGui::SliderInt("Frame width", &style.FrameWidth, 1, 8);
		changed |= ImGui::SliderInt("Frame spacing", &style.FrameSpacing, 0, 4);
		changed |= ImGui::SliderFloat("Min task height", &style.MinTaskHeight, 0.0f, 4.0f, "%.1f px");
		changed |= ImGui::SliderFloat("Legend width", &style.LegendWidth, 0.0f, 500.0f, "%.0f px");
		changed |= ImGui::Checkbox("Colored legend text", &style.UseColoredLegendText);
		int unit = int(style.Unit);
		if (ImGui::Combo("Legend unit", &unit, "Milliseconds\0Microseconds\0"))
		{
			style.Unit = TimeUnit(unit);
			changed = true;
		}

		ImGui::SeparatorText("Colors");
		changed |= internal::EditOptionalColor("Background", style.BackgroundColor, IM_COL32(0, 0, 0, 96));
		changed |= internal::EditOptionalColor("Border", style.BorderColor, ImGui::GetColorU32(ImGuiCol_Border));
		changed |= internal::EditOptionalColor("Text", style.TextColor, ImGui::GetColorU32(ImGuiCol_Text));
		changed |= internal::EditColor("Budget line", style.BudgetColor);
		changed |= ImGui::SliderFloat("Budget thickness", &style.BudgetThickness, 0.5f, 5.0f, "%.1f px");

#ifndef KOI_PROFILER_NO_GRID
		ImGui::SeparatorText("Grid");
		changed |= ImGui::Checkbox("Show grid", &style.ShowGrid);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Labels", &style.ShowGridLabels);
		float gridStepMs = style.GridStep * 1000.0f;
		if (ImGui::SliderFloat("Grid step", &gridStepMs, 0.0f, 20.0f, gridStepMs > 0.0f ? "%.2f ms" : "Auto"))
		{
			style.GridStep = gridStepMs / 1000.0f;
			changed = true;
		}
		changed |= internal::EditOptionalColor("Grid", style.GridColor, ImGui::GetColorU32(ImGuiCol_Border, 0.5f));
#endif

#ifndef KOI_PROFILER_NO_INTERACTION
		ImGui::SeparatorText("Interaction");
		changed |= ImGui::Checkbox("Tooltip", &style.ShowTooltip);
		changed |= ImGui::SliderFloat("Highlight dim", &style.HighlightDimAlpha, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Alpha of other names while a task or legend entry is hovered, 1 turns highlighting off");
#endif
#ifndef KOI_PROFILER_NO_MARKERS
		changed |= internal::EditColor("Markers", style.MarkerColor);
#endif

		if (ImGui::TreeNode("Legend layout"))
		{
			changed |= ImGui::SliderFloat("Left marker margin", &style.LegendLeftMarkerMargin, 0.0f, 20.0f, "%.0f");
			changed |= ImGui::SliderFloat("Left marker width", &style.LegendLeftMarkerWidth, 0.0f, 20.0f, "%.0f");
			changed |= ImGui::SliderFloat("Marker link width", &style.LegendMarkerLinkWidth, 0.0f, 100.0f, "%.0f");
			changed |= ImGui::SliderFloat("Right marker width", &style.LegendRightMarkerWidth, 0.0f, 30.0f, "%.0f");
			changed |= ImGui::SliderFloat("Right marker height", &style.LegendRightMarkerHeight, 1.0f, 30.0f, "%.0f");
			changed |= ImGui::SliderFloat("Right marker margin", &style.LegendRightMarkerMargin, 0.0f, 20.0f, "%.0f");
			changed |= ImGui::SliderFloat("Right marker spacing", &style.LegendRightMarkerSpacing, 0.0f, 20.0f, "%.0f");
			changed |= ImGui::SliderFloat2("Text margin", &style.LegendTextMargin.x, -20.0f, 20.0f, "%.0f");
			ImGui::TreePop();
		}
		ImGui::PopID();
		return changed;
	}

	inline bool ShowAutoScaleEditor(AutoScaleSettings& settings)
	{
		bool changed = false;
		ImGui::PushID(&settings);
		changed |= ImGui::SliderFloat("Percentile", &settings.Percentile, 0.5f, 1.0f, "%.3f");
		ImGui::SetItemTooltip("Frames above this percentile clip instead of squashing the graph");
		changed |= ImGui::SliderFloat("Headroom", &settings.Headroom, 1.0f, 2.0f, "x%.2f");
		changed |= ImGui::SliderFloat("Smooth time", &settings.SmoothTime, 0.0f, 2.0f, "%.2f s");
		float minTimeMs = settings.MinTime * 1000.0f;
		if (ImGui::SliderFloat("Min time", &minTimeMs, 0.001f, 10.0f, "%.3f ms", ImGuiSliderFlags_Logarithmic))
		{
			settings.MinTime = minTimeMs / 1000.0f;
			changed = true;
		}
		changed |= ImGui::Checkbox("Include budget", &settings.IncludeBudget);
		ImGui::SetItemTooltip("Scale up so the budget line always fits");
		ImGui::PopID();
		return changed;
	}

	// Pinned colors can't be listed (names are stored by hash). Changing the parameters regenerates colors
	inline bool ShowPaletteEditor(ColorPalette& palette)
	{
		bool changed = false;
		ImGui::PushID(&palette);
		ImGui::Text("%d / %d names", int(palette.GetCount()), int(palette.GetCapacity()));

		// Preview of the first generated colors
		const float swatchSize = ImGui::GetFrameHeight();
		for (size_t colorIndex = 0; colorIndex < 16; colorIndex++)
		{
			if (colorIndex > 0)
			{
				ImGui::SameLine(0.0f, 2.0f);
			}
			ImGui::PushID(int(colorIndex));
			const ImVec4 color = ImGui::ColorConvertU32ToFloat4(palette.Generator(colorIndex, {}, palette.GeneratorUserData));
			ImGui::ColorButton("##Swatch", color, ImGuiColorEditFlags_NoTooltip, ImVec2(swatchSize, swatchSize));
			ImGui::PopID();
		}

		if (palette.Generator == &GoldenRatioColor)
		{
			if (GoldenRatioParams* params = static_cast<GoldenRatioParams*>(palette.GeneratorUserData))
			{
				bool paramsChanged = false;
				paramsChanged |= ImGui::SliderFloat("Hue start", &params->HueStart, 0.0f, 1.0f);
				paramsChanged |= ImGui::SliderFloat("Hue step", &params->HueStep, 0.0f, 1.0f, "%.6f");
				paramsChanged |= ImGui::SliderFloat("Saturation", &params->Saturation, 0.0f, 1.0f);
				paramsChanged |= ImGui::SliderFloat("Saturation alt", &params->SaturationAlt, 0.0f, 1.0f);
				paramsChanged |= ImGui::SliderFloat("Value", &params->Value, 0.0f, 1.0f);
				paramsChanged |= ImGui::SliderFloat("Value alt", &params->ValueAlt, 0.0f, 1.0f);
				paramsChanged |= ImGui::SliderFloat("Alpha", &params->Alpha, 0.0f, 1.0f);
				if (paramsChanged)
				{
					palette.Regenerate();
					changed = true;
				}
			}
			else
			{
				ImGui::TextDisabled("Golden ratio with default parameters");
			}
		}

		if (ImGui::Button("Regenerate"))
		{
			palette.Regenerate();
			changed = true;
		}
		ImGui::SetItemTooltip("Forget generated colors, keep pinned ones");
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			palette.Clear();
			changed = true;
		}
		ImGui::SetItemTooltip("Forget every color, pinned ones too");
		ImGui::PopID();
		return changed;
	}

#ifndef KOI_PROFILER_NO_BUDGETS
	// Per-name budgets: a fixed time or a share of the frame budget
	inline bool ShowNameBudgetEditor(ProfilerGraph& graph)
	{
		bool changed = false;
		ImGui::PushID("NameBudgets");
		const float fontSize = ImGui::GetFontSize();
		for (size_t budgetIndex = 0; budgetIndex < graph.GetNameBudgetCount(); budgetIndex++)
		{
			const NameBudget budget = graph.GetNameBudget(budgetIndex);
			if (budget.Time <= 0.0f && budget.Share <= 0.0f)
			{
				continue;
			}
			char name[64];
			std::snprintf(name, sizeof(name), "%.*s", int(budget.Name.size()), budget.Name.data());
			ImGui::PushID(int(budgetIndex));

			int mode = budget.Share > 0.0f ? 1 : 0;
			ImGui::SetNextItemWidth(fontSize * 4.5f);
			if (ImGui::Combo("##Mode", &mode, "Time\0Share\0"))
			{
				// Converted through the frame budget, keeping the current length
				const float frameBudget = graph.BudgetTime;
				if (mode == 1)
				{
					graph.SetBudgetShare(name, frameBudget > 0.0f ? budget.Time / frameBudget : 0.1f);
				}
				else
				{
					graph.SetBudget(name, std::chrono::duration<float>(frameBudget > 0.0f ? budget.Share * frameBudget : 0.001f));
				}
				changed = true;
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(fontSize * 9.0f);
			if (mode == 1)
			{
				float percent = budget.Share * 100.0f;
				if (ImGui::SliderFloat("##Share", &percent, 0.1f, 100.0f, "%.1f%% of frame", ImGuiSliderFlags_Logarithmic))
				{
					graph.SetBudgetShare(name, percent / 100.0f);
					changed = true;
				}
			}
			else
			{
				float milliseconds = budget.Time * 1000.0f;
				if (ImGui::SliderFloat("##Time", &milliseconds, 0.01f, 50.0f, "%.2f ms", ImGuiSliderFlags_Logarithmic))
				{
					graph.SetBudget(name, std::chrono::duration<float, std::milli>(milliseconds));
					changed = true;
				}
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(name);
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				graph.ClearBudget(name);
				changed = true;
			}
			ImGui::PopID();
		}

		// Shared by all editors; only one is typed into at a time
		static char newName[64] = "";
		ImGui::SetNextItemWidth(fontSize * 13.5f + ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputTextWithHint("##NewName", "Task name", newName, sizeof(newName));
		ImGui::SameLine();
		if (ImGui::Button("Add budget") && newName[0] != '\0')
		{
			graph.SetBudget(newName, std::chrono::milliseconds(1));
			newName[0] = '\0';
			changed = true;
		}
		ImGui::PopID();
		return changed;
	}
#endif

	// Budget, style, auto scale and palette of one graph
	inline bool ShowGraphEditor(ProfilerGraph& graph)
	{
		bool changed = false;
		ImGui::PushID(&graph);
		float budgetMs = graph.BudgetTime * 1000.0f;
		if (ImGui::SliderFloat("Budget", &budgetMs, 0.0f, 50.0f, budgetMs > 0.0f ? "%.2f ms" : "Off"))
		{
			graph.BudgetTime = budgetMs / 1000.0f;
			changed = true;
		}
#ifndef KOI_PROFILER_NO_BUDGETS
		if (ImGui::TreeNode("Name budgets"))
		{
			changed |= ShowNameBudgetEditor(graph);
			ImGui::TreePop();
		}
#endif
		if (ImGui::TreeNode("Style"))
		{
			changed |= ShowStyleEditor(graph.Style);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Auto scale"))
		{
			changed |= ShowAutoScaleEditor(graph.AutoScale);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Palette"))
		{
			changed |= ShowPaletteEditor(*graph.Palette);
			ImGui::TreePop();
		}
		ImGui::PopID();
		return changed;
	}
#endif

#ifndef KOI_PROFILER_NO_TABLE
	enum class TrackView
	{
		Graph,
		Table,
	};
#endif

	struct TrackConfig
	{
		const char* Label        = "Track";
		GraphConfig Graph;
		bool        Visible      = true;
		float       HeightWeight = 1.0f; // share of the space: height when stacked, width side by side
#ifndef KOI_PROFILER_NO_TABLE
		TrackView   View         = TrackView::Graph;
#endif
	};

	enum class TrackLayout
	{
		Stacked,
		SideBySide,
		Tabs,
	};

	// Hideable window parts. Right-clicking a graph still opens the options.
	enum ProfilerWindowFlags_ : int
	{
		ProfilerWindowFlags_None          = 0,
		ProfilerWindowFlags_NoHeader      = 1 << 0, // fps and tracked totals
		ProfilerWindowFlags_NoControls    = 1 << 1, // pause, scale, budget, frames back, options
		ProfilerWindowFlags_NoOptions     = 1 << 2, // options button only
		ProfilerWindowFlags_NoCounters    = 1 << 3,
		ProfilerWindowFlags_NoTrackLabels = 1 << 4, // also the graph/table toggle
	};
	using ProfilerWindowFlags = int;

	enum class CounterDisplay
	{
		Latest,
		Average, // over StatsInterval, readable for fast-changing counters
	};

#ifndef KOI_PROFILER_NO_OVERLAY
	enum class OverlayCorner
	{
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
	};

	// Compact corner summary like Unreal's "stat unit". Drawn with ProfilerWindow::RenderOverlay.
	struct OverlaySettings
	{
		bool          Visible         = true;
		OverlayCorner Corner          = OverlayCorner::TopRight;
		ImVec2        Margin          = ImVec2(10.0f, 10.0f); // from the area's edges
		float         BackgroundAlpha = 0.6f;
		bool          ShowFrame       = true;  // frame time and fps line
		bool          ShowTracks      = true;  // a line per visible track
		bool          ShowGraphs      = true;  // small graph on each line
		int           TopNames        = 3;     // longest names per track, up to kMaxTopNames
		int           AverageFrames   = 30;    // frames to average over
		int           GraphFrames     = 120;   // frames per small graph
		float         GraphWidth      = 8.0f;  // in font sizes
		float         GraphHeight     = 1.0f;  // in font sizes
		bool          ColorByBudget   = true;  // green, yellow above WarningShare, red over budget
		float         WarningShare    = 0.85f;

		static constexpr int kMaxTopNames = 5;
	};
#endif

	struct WindowConfig
	{
		const char*              Title                = "Koi Profiler###KoiProfiler";
		const char*              SettingsName         = nullptr; // imgui.ini key; nullptr uses Title, "" disables saving
		ImGuiWindowFlags         Flags                = ImGuiWindowFlags_NoScrollbar;
		ProfilerWindowFlags      ProfilerFlags        = ProfilerWindowFlags_None;
		std::vector<TrackConfig> Tracks               = { TrackConfig{ "CPU", {} }, TrackConfig{ "GPU", {} } };
		TrackLayout              Layout               = TrackLayout::Stacked;
		bool                     SharedScale          = false; // all tracks use the largest auto scale
		TimeUnit                 Unit                 = TimeUnit::Milliseconds; // header, labels, scale slider and overlay
		CounterDisplay           Counters             = CounterDisplay::Latest;
		size_t                   MaxCounters          = 32;
		size_t                   MaxCounterNameLength = 32;   // longer names are truncated
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
		size_t                   CounterHistoryLength = 120;  // frames per counter for the hover plot; 0 disables
#endif
#ifndef KOI_PROFILER_NO_OVERLAY
		size_t                   FrameHistoryLength   = 240;  // frame times for the overlay graph; 0 disables
		OverlaySettings          Overlay;
#endif
		float                    StatsInterval        = 0.5f; // seconds between fps and counter average updates
		int                      MinGraphHeight       = 40;
		int                      BudgetFps            = 60;   // 0 hides the budget line
		std::vector<int>         BudgetPresets        = { 0, 30, 60, 120, 144, 240 };
		float                    ScaleMs              = 1000.0f / 60.0f; // while auto scale is off
		float                    ScaleMinMs           = 0.1f;
		float                    ScaleMaxMs           = 50.0f;
		float                    AutoPauseMs          = 0.0f; // pause on a longer frame; 0 disables
		bool                     AutoScale            = true;
	};

	// One graph of a ProfilerWindow
	struct ProfilerTrack
	{
		explicit ProfilerTrack(const TrackConfig& config)
			: Label(config.Label)
			, Visible(config.Visible)
			, HeightWeight(config.HeightWeight)
#ifndef KOI_PROFILER_NO_TABLE
			, View(config.View)
#endif
			, Graph(config.Graph)
		{
		}

		const char*   Label;
		bool          Visible;
		float         HeightWeight;
#ifndef KOI_PROFILER_NO_TABLE
		TrackView     View;
#endif
		ProfilerGraph Graph;
	};

#if !defined(KOI_PROFILER_NO_OVERLAY) && !defined(KOI_PROFILER_NO_EDITORS)
	inline bool ShowOverlayEditor(OverlaySettings& overlay)
	{
		bool changed = false;
		ImGui::PushID(&overlay);
		changed |= ImGui::Checkbox("Visible", &overlay.Visible);
		int corner = int(overlay.Corner);
		if (ImGui::Combo("Corner", &corner, "Top left\0Top right\0Bottom left\0Bottom right\0"))
		{
			overlay.Corner = OverlayCorner(corner);
			changed = true;
		}
		changed |= ImGui::SliderFloat2("Margin", &overlay.Margin.x, 0.0f, 100.0f, "%.0f px");
		changed |= ImGui::SliderFloat("Background alpha", &overlay.BackgroundAlpha, 0.0f, 1.0f);

		ImGui::SeparatorText("Lines");
		changed |= ImGui::Checkbox("Frame", &overlay.ShowFrame);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Tracks", &overlay.ShowTracks);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Graphs", &overlay.ShowGraphs);
		changed |= ImGui::SliderInt("Top names", &overlay.TopNames, 0, OverlaySettings::kMaxTopNames);
		changed |= ImGui::SliderInt("Average over", &overlay.AverageFrames, 1, 240, "%d frames");
		changed |= ImGui::SliderInt("Graph frames", &overlay.GraphFrames, 2, 600);
		changed |= ImGui::SliderFloat("Graph width", &overlay.GraphWidth, 2.0f, 30.0f, "%.1f em");
		changed |= ImGui::SliderFloat("Graph height", &overlay.GraphHeight, 0.5f, 5.0f, "%.1f em");

		ImGui::SeparatorText("Colors");
		changed |= ImGui::Checkbox("Color by budget", &overlay.ColorByBudget);
		changed |= ImGui::SliderFloat("Warning above", &overlay.WarningShare, 0.0f, 1.0f, "%.2f of budget");
		ImGui::PopID();
		return changed;
	}
#endif

#ifndef KOI_PROFILER_NO_SETTINGS
	namespace internal
	{
		// Settings lines per window name, kept all run so rebuilt windows find theirs
		struct SettingsEntry
		{
			std::string Name;
			std::string Lines;
		};

		[[nodiscard]] inline std::vector<SettingsEntry>& GetSettingsEntries()
		{
			static std::vector<SettingsEntry> entries;
			return entries;
		}

		[[nodiscard]] inline SettingsEntry* FindSettings(std::string_view name)
		{
			for (SettingsEntry& entry : GetSettingsEntries())
			{
				if (entry.Name == name)
				{
					return &entry;
				}
			}
			return nullptr;
		}

		[[nodiscard]] inline SettingsEntry& FindOrAddSettings(std::string_view name)
		{
			if (SettingsEntry* entry = FindSettings(name))
			{
				return *entry;
			}
			return GetSettingsEntries().emplace_back(SettingsEntry{ std::string(name), {} });
		}

		enum class SettingType
		{
			Int, // also enums
			Float,
			Bool,
			Color,
			Vec2,
		};

		struct SettingField
		{
			const char* Name;
			SettingType Type;
			size_t      Offset;
		};

		inline constexpr SettingField kStyleFields[] = {
			{ "FrameWidth",               SettingType::Int,   offsetof(GraphStyle, FrameWidth) },
			{ "FrameSpacing",             SettingType::Int,   offsetof(GraphStyle, FrameSpacing) },
			{ "MinTaskHeight",            SettingType::Float, offsetof(GraphStyle, MinTaskHeight) },
			{ "LegendWidth",              SettingType::Float, offsetof(GraphStyle, LegendWidth) },
			{ "UseColoredLegendText",     SettingType::Bool,  offsetof(GraphStyle, UseColoredLegendText) },
			{ "Unit",                     SettingType::Int,   offsetof(GraphStyle, Unit) },
			{ "BackgroundColor",          SettingType::Color, offsetof(GraphStyle, BackgroundColor) },
			{ "BorderColor",              SettingType::Color, offsetof(GraphStyle, BorderColor) },
			{ "TextColor",                SettingType::Color, offsetof(GraphStyle, TextColor) },
			{ "BudgetColor",              SettingType::Color, offsetof(GraphStyle, BudgetColor) },
			{ "BudgetThickness",          SettingType::Float, offsetof(GraphStyle, BudgetThickness) },
#ifndef KOI_PROFILER_NO_GRID
			{ "ShowGrid",                 SettingType::Bool,  offsetof(GraphStyle, ShowGrid) },
			{ "ShowGridLabels",           SettingType::Bool,  offsetof(GraphStyle, ShowGridLabels) },
			{ "GridStep",                 SettingType::Float, offsetof(GraphStyle, GridStep) },
			{ "GridColor",                SettingType::Color, offsetof(GraphStyle, GridColor) },
#endif
#ifndef KOI_PROFILER_NO_INTERACTION
			{ "ShowTooltip",              SettingType::Bool,  offsetof(GraphStyle, ShowTooltip) },
			{ "HighlightDimAlpha",        SettingType::Float, offsetof(GraphStyle, HighlightDimAlpha) },
#endif
#ifndef KOI_PROFILER_NO_MARKERS
			{ "MarkerColor",              SettingType::Color, offsetof(GraphStyle, MarkerColor) },
#endif
			{ "LegendLeftMarkerMargin",   SettingType::Float, offsetof(GraphStyle, LegendLeftMarkerMargin) },
			{ "LegendLeftMarkerWidth",    SettingType::Float, offsetof(GraphStyle, LegendLeftMarkerWidth) },
			{ "LegendMarkerLinkWidth",    SettingType::Float, offsetof(GraphStyle, LegendMarkerLinkWidth) },
			{ "LegendRightMarkerWidth",   SettingType::Float, offsetof(GraphStyle, LegendRightMarkerWidth) },
			{ "LegendRightMarkerHeight",  SettingType::Float, offsetof(GraphStyle, LegendRightMarkerHeight) },
			{ "LegendRightMarkerMargin",  SettingType::Float, offsetof(GraphStyle, LegendRightMarkerMargin) },
			{ "LegendRightMarkerSpacing", SettingType::Float, offsetof(GraphStyle, LegendRightMarkerSpacing) },
			{ "LegendTextMargin",         SettingType::Vec2,  offsetof(GraphStyle, LegendTextMargin) },
		};

		inline constexpr SettingField kAutoScaleFields[] = {
			{ "Percentile",    SettingType::Float, offsetof(AutoScaleSettings, Percentile) },
			{ "Headroom",      SettingType::Float, offsetof(AutoScaleSettings, Headroom) },
			{ "SmoothTime",    SettingType::Float, offsetof(AutoScaleSettings, SmoothTime) },
			{ "MinTime",       SettingType::Float, offsetof(AutoScaleSettings, MinTime) },
			{ "IncludeBudget", SettingType::Bool,  offsetof(AutoScaleSettings, IncludeBudget) },
		};

#ifndef KOI_PROFILER_NO_OVERLAY
		inline constexpr SettingField kOverlayFields[] = {
			{ "Visible",         SettingType::Bool,  offsetof(OverlaySettings, Visible) },
			{ "Corner",          SettingType::Int,   offsetof(OverlaySettings, Corner) },
			{ "Margin",          SettingType::Vec2,  offsetof(OverlaySettings, Margin) },
			{ "BackgroundAlpha", SettingType::Float, offsetof(OverlaySettings, BackgroundAlpha) },
			{ "ShowFrame",       SettingType::Bool,  offsetof(OverlaySettings, ShowFrame) },
			{ "ShowTracks",      SettingType::Bool,  offsetof(OverlaySettings, ShowTracks) },
			{ "ShowGraphs",      SettingType::Bool,  offsetof(OverlaySettings, ShowGraphs) },
			{ "TopNames",        SettingType::Int,   offsetof(OverlaySettings, TopNames) },
			{ "AverageFrames",   SettingType::Int,   offsetof(OverlaySettings, AverageFrames) },
			{ "GraphFrames",     SettingType::Int,   offsetof(OverlaySettings, GraphFrames) },
			{ "GraphWidth",      SettingType::Float, offsetof(OverlaySettings, GraphWidth) },
			{ "GraphHeight",     SettingType::Float, offsetof(OverlaySettings, GraphHeight) },
			{ "ColorByBudget",   SettingType::Bool,  offsetof(OverlaySettings, ColorByBudget) },
			{ "WarningShare",    SettingType::Float, offsetof(OverlaySettings, WarningShare) },
		};
#endif

		inline void WriteField(ImGuiTextBuffer& buffer, const char* prefix, const SettingField& field, const void* object)
		{
			const char* data = static_cast<const char*>(object) + field.Offset;
			switch (field.Type)
			{
			case SettingType::Int:   buffer.appendf("%s%s=%d\n", prefix, field.Name, *reinterpret_cast<const int*>(data)); break;
			case SettingType::Float: buffer.appendf("%s%s=%g\n", prefix, field.Name, double(*reinterpret_cast<const float*>(data))); break;
			case SettingType::Bool:  buffer.appendf("%s%s=%d\n", prefix, field.Name, int(*reinterpret_cast<const bool*>(data))); break;
			case SettingType::Color: buffer.appendf("%s%s=0x%08X\n", prefix, field.Name, *reinterpret_cast<const uint32_t*>(data)); break;
			case SettingType::Vec2:
			{
				const ImVec2& value = *reinterpret_cast<const ImVec2*>(data);
				buffer.appendf("%s%s=%g,%g\n", prefix, field.Name, double(value.x), double(value.y));
				break;
			}
			}
		}

		template <size_t N>
		inline void WriteFields(ImGuiTextBuffer& buffer, const char* prefix, const SettingField (&fields)[N], const void* object)
		{
			for (const SettingField& field : fields)
			{
				WriteField(buffer, prefix, field, object);
			}
		}

		// False for unknown keys. Enums aren't range checked; callers clamp.
		template <size_t N>
		inline bool ReadField(const SettingField (&fields)[N], std::string_view key, const char* value, void* object)
		{
			for (const SettingField& field : fields)
			{
				if (key != field.Name)
				{
					continue;
				}
				char* data = static_cast<char*>(object) + field.Offset;
				switch (field.Type)
				{
				case SettingType::Int:   *reinterpret_cast<int*>(data) = int(std::strtol(value, nullptr, 10)); break;
				case SettingType::Float: *reinterpret_cast<float*>(data) = std::strtof(value, nullptr); break;
				case SettingType::Bool:  *reinterpret_cast<bool*>(data) = std::strtol(value, nullptr, 10) != 0; break;
				case SettingType::Color: *reinterpret_cast<uint32_t*>(data) = uint32_t(std::strtoul(value, nullptr, 16)); break;
				case SettingType::Vec2:
				{
					ImVec2& vec = *reinterpret_cast<ImVec2*>(data);
					char* end = nullptr;
					vec.x = std::strtof(value, &end);
					vec.y = (end && *end == ',') ? std::strtof(end + 1, nullptr) : vec.y;
					break;
				}
				}
				return true;
			}
			return false;
		}
	}

	// Registers the imgui.ini handler. Windows do this themselves; call it after ImGui::CreateContext
	// if a window is created after the first NewFrame (when imgui.ini is read).
	inline void RegisterSettingsHandler()
	{
		if (!ImGui::GetCurrentContext() || ImGui::FindSettingsHandler("koiProfiler"))
		{
			return;
		}
		ImGuiSettingsHandler handler;
		handler.TypeName   = "KoiProfiler";
		handler.TypeHash   = ImHashStr("KoiProfiler");
		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void*
		{
			internal::SettingsEntry& entry = internal::FindOrAddSettings(name);
			entry.Lines.clear();
			return &entry;
		};
		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
		{
			static_cast<internal::SettingsEntry*>(entry)->Lines.append(line).push_back('\n');
		};
		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* settingsHandler, ImGuiTextBuffer* buffer)
		{
			for (const internal::SettingsEntry& entry : internal::GetSettingsEntries())
			{
				buffer->appendf("[%s][%s]\n", settingsHandler->TypeName, entry.Name.c_str());
				buffer->append(entry.Lines.c_str());
				buffer->append("\n");
			}
		};
		ImGui::AddSettingsHandler(&handler);
	}
#endif

	// Window with graph tracks (CPU and GPU by default), counters and controls. Settings persist in imgui.ini.
	class ProfilerWindow
	{
	public:
		explicit ProfilerWindow(const WindowConfig& config = {})
			: ProfilerFlags(config.ProfilerFlags)
			, Layout(config.Layout)
			, SharedScale(config.SharedScale)
			, Unit(config.Unit)
			, Counters(config.Counters)
			, AutoPauseMs(config.AutoPauseMs)
#ifndef KOI_PROFILER_NO_OVERLAY
			, Overlay(config.Overlay)
#endif
			, m_config(config)
			, m_counterNames(config.MaxCounters, config.MaxCounterNameLength)
			, m_counters(config.MaxCounters)
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
			, m_counterHistory(config.MaxCounters * config.CounterHistoryLength)
#endif
#ifndef KOI_PROFILER_NO_OVERLAY
			, m_frameTimes(config.FrameHistoryLength)
#endif
			, m_trackScales(config.Tracks.size())
			, m_scaleMs(config.ScaleMs)
			, m_autoScale(config.AutoScale)
		{
			m_tracks.reserve(config.Tracks.size());
			for (const TrackConfig& trackConfig : config.Tracks)
			{
				m_tracks.emplace_back(trackConfig);
				m_tracks.back().Graph.Style.Unit = config.Unit;
				m_maxFramesCount = std::max(m_maxFramesCount, trackConfig.Graph.FramesCount);
			}
			SetBudgetFps(config.BudgetFps);
#ifndef KOI_PROFILER_NO_SETTINGS
			RegisterSettingsHandler();
#endif
		}

		// Ignored while paused. Takes what ProfilerGraph::LoadFrameData takes: tasks and a count, a range, or a range and a projection.
		template <typename... Tasks>
		void LoadFrameData(size_t trackIndex, Tasks&&... tasks)
		{
			KOI_PROF_ASSERT(trackIndex < m_tracks.size(), "Track index out of range");
			if (m_paused || trackIndex >= m_tracks.size())
			{
				return;
			}
			ProfilerGraph& graph = m_tracks[trackIndex].Graph;
			graph.LoadFrameData(std::forward<Tasks>(tasks)...);

			// Pause applies on the next render, so every track gets this frame
			const float spanMs = graph.GetFrameSpan(0) * 1000.0f;
			if (AutoPauseMs > 0.0f && spanMs > AutoPauseMs && !m_pausePending)
			{
				m_pausePending   = true;
				m_autoPauseTrack = trackIndex;
				m_autoPauseMs    = spanMs;
			}
		}

		// Seen counters stay listed (0 when missing) so the row doesn't jump. Ignored while paused.
		void LoadCounters(const Counter* counters, size_t count)
		{
			if (m_paused)
			{
				return;
			}

			const uint32_t knownCount = m_counterNames.GetCount();
			for (uint32_t counterId = 0; counterId < knownCount; counterId++)
			{
				m_counters[counterId].Latest = 0.0;
			}
			for (size_t counterIndex = 0; counterIndex < count; counterIndex++)
			{
				const uint32_t counterId = FindOrAddCounter(counters[counterIndex].Name, counterIndex);
				if (counterId != internal::NameTable::kInvalidId)
				{
					m_counters[counterId].Latest = counters[counterIndex].Value;
					m_counters[counterId].Unit   = counters[counterIndex].Unit;
				}
			}

#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
			const size_t historyLength = m_config.CounterHistoryLength;
#endif
			for (uint32_t counterId = 0; counterId < m_counterNames.GetCount(); counterId++)
			{
				CounterStats& stats = m_counters[counterId];
				stats.Min = stats.Samples > 0 ? std::min(stats.Min, stats.Latest) : stats.Latest;
				stats.Max = stats.Samples > 0 ? std::max(stats.Max, stats.Latest) : stats.Latest;
				stats.Sum += stats.Latest;
				stats.Samples++;
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
				if (historyLength > 0)
				{
					m_counterHistory[counterId * historyLength + m_historyHead] = float(stats.Latest);
				}
#endif
			}
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
			if (historyLength > 0)
			{
				m_historyHead  = (m_historyHead + 1) % historyLength;
				m_historyCount = std::min(m_historyCount + 1, historyLength);
			}
#endif

			const TimePoint now = Clock::now();
			if (std::chrono::duration<float>(now - m_counterIntervalStart).count() >= m_config.StatsInterval)
			{
				m_counterIntervalStart = now;
				for (uint32_t counterId = 0; counterId < m_counterNames.GetCount(); counterId++)
				{
					CounterStats& stats = m_counters[counterId];
					stats.Average     = stats.Sum / double(stats.Samples);
					stats.IntervalMin = stats.Min;
					stats.IntervalMax = stats.Max;
					stats.HasInterval = true;
					stats.Sum         = 0.0;
					stats.Samples     = 0;
				}
			}
		}

#ifndef KOI_PROFILER_NO_MARKERS
		// Marks the next frame of every track. Ignored while paused.
		void AddMarker(std::string_view name, uint32_t color = 0)
		{
			for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
			{
				AddMarker(trackIndex, name, color);
			}
		}

		void AddMarker(size_t trackIndex, std::string_view name, uint32_t color = 0)
		{
			if (!m_paused && trackIndex < m_tracks.size())
			{
				m_tracks[trackIndex].Graph.AddMarker(name, color);
			}
		}
#endif

		// Sets the budget callback of every track. FindTrack(*event.Graph) tells which track an event came from.
		void SetBudgetCallback(BudgetCallback callback, void* userData = nullptr)
		{
			for (ProfilerTrack& track : m_tracks)
			{
				track.Graph.SetBudgetCallback(callback, userData);
			}
		}

		template <typename F>
		void SetBudgetCallback(F& callable)
		{
			for (ProfilerTrack& track : m_tracks)
			{
				track.Graph.SetBudgetCallback(callable);
			}
		}

		// Index of the track that owns graph, or -1
		[[nodiscard]] int FindTrack(const ProfilerGraph& graph) const
		{
			for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
			{
				if (&m_tracks[trackIndex].Graph == &graph)
				{
					return int(trackIndex);
				}
			}
			return -1;
		}

		// Begins its own window. extraFlags are added to WindowConfig::Flags.
		void Render(bool* open = nullptr, ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None)
		{
			if (BackgroundAlpha >= 0.0f)
			{
				ImGui::SetNextWindowBgAlpha(BackgroundAlpha);
			}
			if (ImGui::Begin(m_config.Title, open, m_config.Flags | extraFlags))
			{
				RenderContents();
			}
			ImGui::End();
		}

		// Draws into the current window, for embedding
		void RenderContents()
		{
			ImGui::PushID(this);
			LoadSettingsOnce();
			UpdateFrameTime();
			if (m_pausePending)
			{
				m_pausePending = false;
				m_paused       = true;
				m_autoPaused   = true;
			}
			if (!m_paused)
			{
				m_frameOffset = 0;
				m_autoPaused  = false;
			}

			if (!(ProfilerFlags & ProfilerWindowFlags_NoHeader))
			{
				RenderHeader();
			}
			if (!(ProfilerFlags & ProfilerWindowFlags_NoControls))
			{
				RenderControls();
			}
			if (!(ProfilerFlags & ProfilerWindowFlags_NoCounters))
			{
				RenderCounters();
			}

#ifndef KOI_PROFILER_NO_EDITORS
			m_graphHovered = false;
#endif
			RenderTracks();
#ifndef KOI_PROFILER_NO_EDITORS
			if (m_graphHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
			{
				ImGui::OpenPopup("##Options");
			}
			if (ImGui::BeginPopup("##Options"))
			{
				ShowOptionsEditor();
				ImGui::EndPopup();
			}
#endif

			if (m_settingsDirty)
			{
				m_settingsDirty = false;
				SaveSettings();
			}
			ImGui::PopID();
		}

#ifndef KOI_PROFILER_NO_OVERLAY
		// Draws the overlay in a corner. It's not a window, so it never takes focus or input.
		// Defaults to the foreground draw list over the main viewport; pass a window's draw list and rect to draw over that window.
		void RenderOverlay(ImDrawList* drawList = nullptr, ImVec2 areaMin = ImVec2(0.0f, 0.0f), ImVec2 areaMax = ImVec2(0.0f, 0.0f))
		{
			LoadSettingsOnce();
			UpdateFrameTime();
			if (!Overlay.Visible)
			{
				return;
			}
			if (!drawList)
			{
				drawList = ImGui::GetForegroundDrawList();
			}
			if (areaMax.x <= areaMin.x || areaMax.y <= areaMin.y)
			{
				const ImGuiViewport* viewport = ImGui::GetMainViewport();
				areaMin = viewport->WorkPos;
				areaMax = viewport->WorkPos + viewport->WorkSize;
			}

			// Measure first: the background is drawn before the rows
			const ImGuiStyle& style = ImGui::GetStyle();
			const float fontSize = ImGui::GetFontSize();
			const float indent = fontSize;
			const ImVec2 graphSize(std::floor(fontSize * Overlay.GraphWidth), std::floor(fontSize * Overlay.GraphHeight));
			const bool showGraphs = Overlay.ShowGraphs && graphSize.x > 1.0f && graphSize.y > 1.0f;
			auto getRowHeight = [&](const OverlayRow& row) { return showGraphs && (row.Graph || row.FrameGraph) ? std::max(fontSize, graphSize.y) : fontSize; };

			float labelWidth = 0.0f;
			float valueWidth = 0.0f;
			float rowsHeight = 0.0f;
			size_t rowCount = 0;
			ForEachOverlayRow([&](const OverlayRow& row)
			{
				labelWidth = std::max(labelWidth, ImGui::CalcTextSize(row.Label.data(), row.Label.data() + row.Label.size()).x + (row.Indent ? indent : 0.0f));
				valueWidth = std::max(valueWidth, ImGui::CalcTextSize(row.Value).x);
				rowsHeight += getRowHeight(row);
				rowCount++;
			});
			if (rowCount == 0)
			{
				return;
			}

			const float columnGap = style.ItemSpacing.x * 2.0f;
			const float rowGap = style.ItemSpacing.y * 0.5f;
			const ImVec2 size(std::floor(style.WindowPadding.x * 2.0f + labelWidth + columnGap + valueWidth + (showGraphs ? columnGap + graphSize.x : 0.0f)),
				std::floor(style.WindowPadding.y * 2.0f + rowsHeight + rowGap * float(rowCount - 1)));
			const bool right = Overlay.Corner == OverlayCorner::TopRight || Overlay.Corner == OverlayCorner::BottomRight;
			const bool bottom = Overlay.Corner == OverlayCorner::BottomLeft || Overlay.Corner == OverlayCorner::BottomRight;
			const ImVec2 position(std::floor(right ? areaMax.x - Overlay.Margin.x - size.x : areaMin.x + Overlay.Margin.x),
				std::floor(bottom ? areaMax.y - Overlay.Margin.y - size.y : areaMin.y + Overlay.Margin.y));

			const uint32_t background = (ImGui::GetColorU32(ImGuiCol_WindowBg) & ~IM_COL32_A_MASK) | (uint32_t(std::clamp(Overlay.BackgroundAlpha, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
			drawList->AddRectFilled(position, position + size, background, style.WindowRounding);
			if (style.WindowBorderSize > 0.0f)
			{
				drawList->AddRect(position, position + size, ImGui::GetColorU32(ImGuiCol_Border), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
			}

			const float valueX = position.x + style.WindowPadding.x + labelWidth + columnGap;
			const float graphX = valueX + valueWidth + columnGap;
			float rowY = position.y + style.WindowPadding.y;
			ForEachOverlayRow([&](const OverlayRow& row)
			{
				const float rowHeight = getRowHeight(row);
				const float textY = rowY + std::floor((rowHeight - fontSize) * 0.5f);
				drawList->AddText(ImVec2(position.x + style.WindowPadding.x + (row.Indent ? indent : 0.0f), textY), row.LabelColor, row.Label.data(), row.Label.data() + row.Label.size());
				drawList->AddText(ImVec2(valueX, textY), row.ValueColor, row.Value);
				if (showGraphs && row.Graph)
				{
					row.Graph->RenderSparkline(drawList, ImVec2(graphX, rowY), graphSize, size_t(std::max(Overlay.GraphFrames, 2)), ImGui::GetColorU32(ImGuiCol_PlotLines));
				}
				else if (showGraphs && row.FrameGraph)
				{
					RenderFrameTimeGraph(drawList, ImVec2(graphX, rowY), graphSize);
				}
				rowY += rowHeight + rowGap;
			});
		}
#endif

#ifndef KOI_PROFILER_NO_EDITORS
		// The Options popup contents, for use in your own panel. Returns true on change.
		bool ShowOptionsEditor()
		{
			bool changed = false;
			int layout = int(Layout);
			if (ImGui::Combo("Layout", &layout, "Stacked\0Side by side\0Tabs\0"))
			{
				Layout = TrackLayout(layout);
				changed = true;
			}
			changed |= ImGui::Checkbox("Shared scale", &SharedScale);
			ImGui::SetItemTooltip("Every track uses the largest auto scale, so their heights compare");
			int unit = int(Unit);
			if (ImGui::Combo("Unit", &unit, "Milliseconds\0Microseconds\0"))
			{
				SetUnit(TimeUnit(unit));
				changed = true;
			}
			int counters = int(Counters);
			if (ImGui::Combo("Counters", &counters, "Latest\0Average\0"))
			{
				Counters = CounterDisplay(counters);
				changed = true;
			}
			changed |= ImGui::SliderFloat("Auto-pause above", &AutoPauseMs, 0.0f, 100.0f, AutoPauseMs > 0.0f ? "%.1f ms" : "Off");
			ImGui::SetItemTooltip("Pauses when a track loads a frame longer than this, so the spike stays on screen");
			float alpha = BackgroundAlpha >= 0.0f ? BackgroundAlpha : ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w;
			if (ImGui::SliderFloat("Background alpha", &alpha, 0.0f, 1.0f))
			{
				BackgroundAlpha = alpha;
				changed = true;
			}

			if (ImGui::TreeNode("Hide"))
			{
				changed |= ImGui::CheckboxFlags("Header", &ProfilerFlags, ProfilerWindowFlags_NoHeader);
				changed |= ImGui::CheckboxFlags("Controls", &ProfilerFlags, ProfilerWindowFlags_NoControls);
				changed |= ImGui::CheckboxFlags("Options button", &ProfilerFlags, ProfilerWindowFlags_NoOptions);
				changed |= ImGui::CheckboxFlags("Counters", &ProfilerFlags, ProfilerWindowFlags_NoCounters);
				changed |= ImGui::CheckboxFlags("Track labels", &ProfilerFlags, ProfilerWindowFlags_NoTrackLabels);
				ImGui::TextDisabled("Right-click a graph for these options");
				ImGui::TreePop();
			}
#ifndef KOI_PROFILER_NO_OVERLAY
			if (ImGui::TreeNode("Overlay"))
			{
				changed |= ShowOverlayEditor(Overlay);
				ImGui::TreePop();
			}
#endif

			ImGui::SeparatorText("Tracks");
			for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
			{
				ProfilerTrack& track = m_tracks[trackIndex];
				ImGui::PushID(int(trackIndex));
				changed |= ImGui::Checkbox("##Visible", &track.Visible);
				ImGui::SetItemTooltip("Visible");
				ImGui::SameLine();
				if (ImGui::TreeNode(track.Label))
				{
#ifndef KOI_PROFILER_NO_TABLE
					int view = int(track.View);
					if (ImGui::Combo("View", &view, "Graph\0Table\0"))
					{
						track.View = TrackView(view);
						changed = true;
					}
#endif
					if (Layout != TrackLayout::Tabs)
					{
						changed |= ImGui::SliderFloat("Size weight", &track.HeightWeight, 0.1f, 4.0f, "%.1f");
					}
					changed |= ShowGraphEditor(track.Graph);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			m_settingsDirty |= changed;
			return changed;
		}
#endif

		[[nodiscard]] bool IsPaused() const { return m_paused; }
		void SetPaused(bool paused) { m_paused = paused; }
		[[nodiscard]] bool IsAutoScale() const { return m_autoScale; }
		void SetAutoScale(bool autoScale) { m_autoScale = autoScale; }
		[[nodiscard]] int GetBudgetFps() const { return m_budgetFps; }

		// Sets every track's budget line; tracks can still be changed individually
		void SetBudgetFps(int fps)
		{
			m_budgetFps = fps;
			for (ProfilerTrack& track : m_tracks)
			{
				track.Graph.BudgetTime = fps > 0 ? 1.0f / float(fps) : 0.0f;
			}
		}

		// Sets the unit of the window and every track
		void SetUnit(TimeUnit unit)
		{
			Unit = unit;
			for (ProfilerTrack& track : m_tracks)
			{
				track.Graph.Style.Unit = unit;
			}
		}

		[[nodiscard]] size_t GetTrackCount() const { return m_tracks.size(); }
		[[nodiscard]] ProfilerTrack& GetTrack(size_t trackIndex) { return m_tracks[trackIndex]; }
		[[nodiscard]] const WindowConfig& GetConfig() const { return m_config; }

		// Saves settings on the next render. Only needed for changes made from code.
		void MarkSettingsChanged() { m_settingsDirty = true; }

	private:
		using Clock     = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		struct CounterStats
		{
			double      Latest      = 0.0;
			double      Min         = 0.0; // of the running interval
			double      Max         = 0.0;
			double      Sum         = 0.0;
			int64_t     Samples     = 0;
			double      Average     = 0.0; // of the last finished interval
			double      IntervalMin = 0.0;
			double      IntervalMax = 0.0;
			CounterUnit Unit        = CounterUnit::None;
			bool        HasInterval = false;
		};

	private:
		[[nodiscard]] uint32_t FindOrAddCounter(std::string_view name, size_t position)
		{
			const uint32_t hintId = position < m_counterNames.GetCount() ? uint32_t(position) : internal::NameTable::kInvalidId;
			uint32_t counterId = m_counterNames.Find(name, hintId);
			if (counterId == internal::NameTable::kInvalidId)
			{
				counterId = m_counterNames.Add(name);
				KOI_PROF_ASSERT(counterId != internal::NameTable::kInvalidId, "Too many distinct counters, raise WindowConfig::MaxCounters");
			}
			return counterId;
		}

		[[nodiscard]] const char* GetSettingsName() const
		{
			return m_config.SettingsName ? m_config.SettingsName : m_config.Title;
		}

		// imgui.ini is read on the first NewFrame, so apply on first render, not in the constructor
		void LoadSettingsOnce()
		{
			if (m_settingsLoaded)
			{
				return;
			}
			m_settingsLoaded = true;
#ifndef KOI_PROFILER_NO_SETTINGS
			RegisterSettingsHandler();
			const char* name = GetSettingsName();
			const internal::SettingsEntry* entry = name[0] != '\0' ? internal::FindSettings(name) : nullptr;
			if (!entry)
			{
				return;
			}
			const std::string_view lines = entry->Lines;
			size_t lineStart = 0;
			while (lineStart < lines.size())
			{
				const size_t lineEnd = std::min(lines.find('\n', lineStart), lines.size());
				ApplySetting(lines.substr(lineStart, lineEnd - lineStart));
				lineStart = lineEnd + 1;
			}
#endif
		}

		void SaveSettings()
		{
#ifndef KOI_PROFILER_NO_SETTINGS
			const char* name = GetSettingsName();
			if (name[0] == '\0' || !ImGui::GetCurrentContext())
			{
				return;
			}
			ImGuiTextBuffer buffer;
			buffer.appendf("Flags=%d\nLayout=%d\nSharedScale=%d\nUnit=%d\nCounters=%d\n", ProfilerFlags, int(Layout), int(SharedScale), int(Unit), int(Counters));
			buffer.appendf("BackgroundAlpha=%g\nAutoScale=%d\nBudgetFps=%d\nScaleMs=%g\nAutoPauseMs=%g\n", double(BackgroundAlpha), int(m_autoScale), m_budgetFps, double(m_scaleMs), double(AutoPauseMs));
#ifndef KOI_PROFILER_NO_OVERLAY
			internal::WriteFields(buffer, "Overlay.", internal::kOverlayFields, &Overlay);
#endif
			for (const ProfilerTrack& track : m_tracks)
			{
				char prefix[96];
				std::snprintf(prefix, sizeof(prefix), "Track[%s].", track.Label);
				buffer.appendf("%sVisible=%d\n%sWeight=%g\n%sBudget=%g\n", prefix, int(track.Visible), prefix, double(track.HeightWeight), prefix, double(track.Graph.BudgetTime));
#ifndef KOI_PROFILER_NO_TABLE
				buffer.appendf("%sView=%d\n", prefix, int(track.View));
#endif
#ifndef KOI_PROFILER_NO_BUDGETS
				for (size_t budgetIndex = 0; budgetIndex < track.Graph.GetNameBudgetCount(); budgetIndex++)
				{
					const NameBudget budget = track.Graph.GetNameBudget(budgetIndex);
					if (budget.Time > 0.0f || budget.Share > 0.0f)
					{
						buffer.appendf("%sNameBudget[%.*s]=%g,%g\n", prefix, int(budget.Name.size()), budget.Name.data(), double(budget.Time), double(budget.Share));
					}
				}
#endif

				char fieldPrefix[128];
				std::snprintf(fieldPrefix, sizeof(fieldPrefix), "%sStyle.", prefix);
				internal::WriteFields(buffer, fieldPrefix, internal::kStyleFields, &track.Graph.Style);
				std::snprintf(fieldPrefix, sizeof(fieldPrefix), "%sAutoScale.", prefix);
				internal::WriteFields(buffer, fieldPrefix, internal::kAutoScaleFields, &track.Graph.AutoScale);
			}
			internal::FindOrAddSettings(name).Lines.assign(buffer.c_str(), size_t(buffer.size()));
			ImGui::MarkIniSettingsDirty();
#endif
		}

#ifndef KOI_PROFILER_NO_SETTINGS
		[[nodiscard]] ProfilerTrack* FindTrack(std::string_view label)
		{
			for (ProfilerTrack& track : m_tracks)
			{
				if (label == track.Label)
				{
					return &track;
				}
			}
			return nullptr;
		}

		void ApplySetting(std::string_view line)
		{
			const size_t equals = line.find('=');
			if (equals == std::string_view::npos)
			{
				return;
			}
			const std::string_view key = line.substr(0, equals);
			char value[64];
			std::snprintf(value, sizeof(value), "%.*s", int(line.size() - equals - 1), line.data() + equals + 1);
			const int intValue = int(std::strtol(value, nullptr, 10));
			const float floatValue = std::strtof(value, nullptr);

			if (key.substr(0, 6) == "Track[")
			{
				const size_t labelEnd = key.find("].");
				ProfilerTrack* track = labelEnd != std::string_view::npos ? FindTrack(key.substr(6, labelEnd - 6)) : nullptr;
				if (!track)
				{
					return;
				}
				const std::string_view field = key.substr(labelEnd + 2);
				if (field == "Visible") { track->Visible = intValue != 0; }
				else if (field == "Weight") { track->HeightWeight = floatValue; }
				else if (field == "Budget") { track->Graph.BudgetTime = floatValue; }
#ifndef KOI_PROFILER_NO_TABLE
				else if (field == "View") { track->View = TrackView(std::clamp(intValue, 0, 1)); }
#endif
#ifndef KOI_PROFILER_NO_BUDGETS
				else if (field.substr(0, 11) == "NameBudget[" && field.back() == ']')
				{
					const std::string_view budgetName = field.substr(11, field.size() - 12);
					char* end = nullptr;
					const float time = std::strtof(value, &end);
					const float share = (end && *end == ',') ? std::strtof(end + 1, nullptr) : 0.0f;
					if (share > 0.0f)
					{
						track->Graph.SetBudgetShare(budgetName, share);
					}
					else
					{
						track->Graph.SetBudget(budgetName, std::chrono::duration<float>(time));
					}
				}
#endif
				else if (field.substr(0, 6) == "Style.")
				{
					internal::ReadField(internal::kStyleFields, field.substr(6), value, &track->Graph.Style);
					track->Graph.Style.Unit = TimeUnit(std::clamp(int(track->Graph.Style.Unit), 0, 1));
				}
				else if (field.substr(0, 10) == "AutoScale.") { internal::ReadField(internal::kAutoScaleFields, field.substr(10), value, &track->Graph.AutoScale); }
				return;
			}

#ifndef KOI_PROFILER_NO_OVERLAY
			if (key.substr(0, 8) == "Overlay.")
			{
				internal::ReadField(internal::kOverlayFields, key.substr(8), value, &Overlay);
				Overlay.Corner = OverlayCorner(std::clamp(int(Overlay.Corner), 0, 3));
				Overlay.TopNames = std::clamp(Overlay.TopNames, 0, OverlaySettings::kMaxTopNames);
				return;
			}
#endif

			if (key == "Flags") { ProfilerFlags = intValue; }
			else if (key == "Layout") { Layout = TrackLayout(std::clamp(intValue, 0, 2)); }
			else if (key == "SharedScale") { SharedScale = intValue != 0; }
			else if (key == "Unit") { Unit = TimeUnit(std::clamp(intValue, 0, 1)); }
			else if (key == "Counters") { Counters = CounterDisplay(std::clamp(intValue, 0, 1)); }
			else if (key == "BackgroundAlpha") { BackgroundAlpha = floatValue; }
			else if (key == "AutoScale") { m_autoScale = intValue != 0; }
			else if (key == "BudgetFps") { m_budgetFps = intValue; }
			else if (key == "ScaleMs") { m_scaleMs = floatValue; }
			else if (key == "AutoPauseMs") { AutoPauseMs = floatValue; }
		}
#endif

		void FormatTime(char* buffer, size_t size, float seconds) const
		{
			internal::FormatTime(buffer, size, double(seconds), Unit);
		}

		void FormatCounter(char* buffer, size_t size, double value, CounterUnit unit) const
		{
			switch (unit)
			{
			case CounterUnit::Bytes:
			{
				static constexpr const char* kSuffixes[] = { "B", "KB", "MB", "GB", "TB" };
				size_t suffix = 0;
				while (std::fabs(value) >= 1024.0 && suffix + 1 < std::size(kSuffixes))
				{
					value /= 1024.0;
					suffix++;
				}
				std::snprintf(buffer, size, suffix == 0 ? "%.0f %s" : "%.1f %s", value, kSuffixes[suffix]);
				break;
			}
			case CounterUnit::Time:
				internal::FormatTime(buffer, size, value, Unit);
				break;
			case CounterUnit::Percent:
				std::snprintf(buffer, size, "%.1f%%", value);
				break;
			default:
			{
				// Fewer decimals as values grow, so averages of large counts stay readable
				const double magnitude = std::fabs(value);
				const char* format = value == std::floor(value) || magnitude >= 100.0 ? "%.0f" : magnitude >= 10.0 ? "%.1f" : "%.2f";
				std::snprintf(buffer, size, format, value);
				break;
			}
			}
		}

		// Once per ImGui frame
		void UpdateFrameTime()
		{
			const int imguiFrame = ImGui::GetFrameCount();
			if (imguiFrame == m_lastUpdateFrame)
			{
				return;
			}
			m_lastUpdateFrame = imguiFrame;

			const TimePoint now = Clock::now();
#ifndef KOI_PROFILER_NO_OVERLAY
			if (!m_frameTimes.empty())
			{
				m_frameTimes[m_frameTimeHead] = std::chrono::duration<float>(now - m_prevFrameTime).count();
				m_frameTimeHead  = (m_frameTimeHead + 1) % m_frameTimes.size();
				m_frameTimeCount = std::min(m_frameTimeCount + 1, m_frameTimes.size());
			}
			m_prevFrameTime = now;
#endif

			m_fpsFramesCount++;
			const float fpsDeltaTime = std::chrono::duration<float>(now - m_prevFpsFrameTime).count();
			if (fpsDeltaTime > m_config.StatsInterval)
			{
				m_avgFrameTime     = fpsDeltaTime / float(m_fpsFramesCount);
				m_fpsFramesCount   = 0;
				m_prevFpsFrameTime = now;
			}
		}

#ifndef KOI_PROFILER_NO_OVERLAY
		// One overlay line
		struct OverlayRow
		{
			std::string_view     Label;
			uint32_t             LabelColor = 0;
			bool                 Indent     = false;
			char                 Value[48]  = {};
			uint32_t             ValueColor = 0;
			const ProfilerGraph* Graph      = nullptr; // set for a track's line
			bool                 FrameGraph = false;   // set for the frame line
		};

		// Green, yellow past the warning share, red over budget; text color without a budget
		[[nodiscard]] uint32_t GetBudgetColor(float time, float budget) const
		{
			if (!Overlay.ColorByBudget || budget <= 0.0f)
			{
				return ImGui::GetColorU32(ImGuiCol_Text);
			}
			if (time > budget)
			{
				return IM_COL32(255, 90, 76, 255);
			}
			return time > budget * Overlay.WarningShare ? IM_COL32(255, 216, 76, 255) : IM_COL32(115, 230, 115, 255);
		}

		// Visits each overlay line, so it can be measured then drawn without storing lines
		template <typename RowFn>
		void ForEachOverlayRow(RowFn&& rowFn)
		{
			const size_t averageFrames = size_t(std::max(Overlay.AverageFrames, 1));
			const uint32_t textColor = ImGui::GetColorU32(ImGuiCol_Text);
			char timeText[32];

			if (Overlay.ShowFrame)
			{
				const float frameBudget = m_budgetFps > 0 ? 1.0f / float(m_budgetFps) : 0.0f;
				OverlayRow row;
				row.Label      = "Frame";
				row.LabelColor = textColor;
				row.ValueColor = GetBudgetColor(m_avgFrameTime, frameBudget);
				row.FrameGraph = true;
				FormatTime(timeText, sizeof(timeText), m_avgFrameTime);
				std::snprintf(row.Value, sizeof(row.Value), "%s  %.0f fps", timeText, 1.0f / m_avgFrameTime);
				rowFn(row);
			}
			if (!Overlay.ShowTracks)
			{
				return;
			}

			std::array<TopTask, OverlaySettings::kMaxTopNames> topTasks;
			for (ProfilerTrack& track : m_tracks)
			{
				if (!track.Visible)
				{
					continue;
				}
				ProfilerGraph& graph = track.Graph;
				const float trackTime = graph.GetAverageTotalTime(averageFrames);
				OverlayRow row;
				row.Label      = track.Label;
				row.LabelColor = textColor;
				row.ValueColor = GetBudgetColor(trackTime, graph.BudgetTime);
				row.Graph      = &graph;
				FormatTime(row.Value, sizeof(row.Value), trackTime);
				rowFn(row);

				const size_t topCount = graph.GetTopTasks(0, topTasks.data(), size_t(std::clamp(Overlay.TopNames, 0, OverlaySettings::kMaxTopNames)));
				for (size_t topIndex = 0; topIndex < topCount; topIndex++)
				{
					const TopTask& top = topTasks[topIndex];
					OverlayRow nameRow;
					nameRow.Label      = top.Name;
					nameRow.LabelColor = top.Color;
					nameRow.Indent     = true;
					nameRow.ValueColor = GetBudgetColor(top.Time, top.Budget);
					FormatTime(nameRow.Value, sizeof(nameRow.Value), top.Time);
					rowFn(nameRow);
				}
			}
		}

		// Real time between frames, including untracked work
		void RenderFrameTimeGraph(ImDrawList* drawList, ImVec2 position, ImVec2 size) const
		{
			const float frameBudget = m_budgetFps > 0 ? 1.0f / float(m_budgetFps) : 0.0f;
			const size_t count = std::min(size_t(std::max(Overlay.GraphFrames, 2)), m_frameTimeCount);
			if (count < 2)
			{
				return;
			}

			// Oldest shown frame is count entries behind the head
			auto frameTimeAt = [&](size_t pointIndex) { return m_frameTimes[(m_frameTimeHead + m_frameTimes.size() - count + pointIndex) % m_frameTimes.size()]; };
			float maxTime = frameBudget;
			for (size_t pointIndex = 0; pointIndex < count; pointIndex++)
			{
				maxTime = std::max(maxTime, frameTimeAt(pointIndex));
			}
			if (maxTime <= 0.0f)
			{
				return;
			}

			if (frameBudget > 0.0f)
			{
				const float budgetY = std::floor(position.y + size.y - frameBudget / maxTime * size.y) + 0.5f;
				drawList->AddLine(ImVec2(position.x, budgetY), ImVec2(position.x + size.x, budgetY), IM_COL32(255, 32, 32, 128));
			}
			for (size_t pointIndex = 0; pointIndex < count; pointIndex++)
			{
				const float x = position.x + size.x * float(pointIndex) / float(count - 1);
				drawList->PathLineTo(ImVec2(x, position.y + size.y - frameTimeAt(pointIndex) / maxTime * size.y));
			}
			drawList->PathStroke(ImGui::GetColorU32(ImGuiCol_PlotLines), ImDrawFlags_None, 1.0f);
		}
#endif

		void RenderHeader() const
		{
			char text[256];
			char timeText[32];
			FormatTime(timeText, sizeof(timeText), m_avgFrameTime);
			int length = std::snprintf(text, sizeof(text), "%.0f fps  %s", 1.0f / m_avgFrameTime, timeText);
			const char* separator = "  |  tracked";
			for (const ProfilerTrack& track : m_tracks)
			{
				if (!track.Visible || length < 0 || size_t(length) >= sizeof(text))
				{
					continue;
				}
				FormatTime(timeText, sizeof(timeText), track.Graph.GetTotalTaskTime(m_frameOffset));
				length += std::snprintf(text + length, sizeof(text) - size_t(length), "%s %s %s", separator, track.Label, timeText);
				separator = "  ";
			}
			ImGui::TextUnformatted(text);
		}

		void RenderControls()
		{
			const float fontSize = ImGui::GetFontSize();
			if (ImGui::Checkbox("Pause", &m_paused) && !m_paused)
			{
				m_autoPaused = false;
			}
			ImGui::SameLine();
			m_settingsDirty |= ImGui::Checkbox("Auto scale", &m_autoScale);

			ImGui::SameLine();
			ImGui::SetNextItemWidth(fontSize * 7.0f);
			char budgetPreview[16];
			FormatBudget(budgetPreview, sizeof(budgetPreview), m_budgetFps);
			if (ImGui::BeginCombo("Budget", budgetPreview))
			{
				for (const int fps : m_config.BudgetPresets)
				{
					char label[16];
					FormatBudget(label, sizeof(label), fps);
					if (ImGui::Selectable(label, fps == m_budgetFps))
					{
						SetBudgetFps(fps);
						m_settingsDirty = true;
					}
				}
				ImGui::Separator();
				int customFps = m_budgetFps;
				ImGui::SetNextItemWidth(fontSize * 4.0f);
				if (ImGui::InputInt("fps", &customFps, 0))
				{
					SetBudgetFps(std::max(customFps, 0));
					m_settingsDirty = true;
				}
				ImGui::EndCombo();
			}

			if (!m_autoScale)
			{
				// Slider is in the display unit; the scale is stored in ms
				const internal::TimeUnitInfo& unit = internal::GetTimeUnitInfo(Unit);
				const float perMs = float(unit.PerSecond / 1000.0);
				char format[16];
				std::snprintf(format, sizeof(format), "%%.%df %s", unit.Decimals, unit.Suffix);
				float scale = m_scaleMs * perMs;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(fontSize * 9.0f);
				if (ImGui::SliderFloat("Scale", &scale, m_config.ScaleMinMs * perMs, m_config.ScaleMaxMs * perMs, format, ImGuiSliderFlags_Logarithmic))
				{
					m_scaleMs = scale / perMs;
					m_settingsDirty = true;
				}
			}
			if (m_paused)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(fontSize * 12.0f);
				ImGui::SliderInt("Frames back", &m_frameOffset, 0, int(m_maxFramesCount) - 1);
#ifndef KOI_PROFILER_NO_INTERACTION
				ImGui::SetItemTooltip("Click a frame to jump to it, or scroll over a graph");
#endif
			}

#ifndef KOI_PROFILER_NO_EDITORS
			if (!(ProfilerFlags & ProfilerWindowFlags_NoOptions))
			{
				ImGui::SameLine();
				if (ImGui::Button("Options"))
				{
					ImGui::OpenPopup("##Options");
				}
			}
#endif

			if (m_paused && m_autoPaused && m_autoPauseTrack < m_tracks.size())
			{
				char timeText[32];
				FormatTime(timeText, sizeof(timeText), m_autoPauseMs / 1000.0f);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Auto-paused: %s frame took %s", m_tracks[m_autoPauseTrack].Label, timeText);
			}
		}

		static void FormatBudget(char* buffer, size_t size, int fps)
		{
			if (fps > 0)
			{
				std::snprintf(buffer, size, "%d fps", fps);
			}
			else
			{
				std::snprintf(buffer, size, "Off");
			}
		}

		// Left to right, wrapping when a counter doesn't fit
		void RenderCounters() const
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			const float gap = ImGui::GetFontSize();
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
			const size_t historyLength = m_config.CounterHistoryLength;
			const bool hasHistory = historyLength > 0 && m_historyCount > 1;
#else
			const bool hasHistory = false;
#endif

			for (uint32_t counterId = 0; counterId < m_counterNames.GetCount(); counterId++)
			{
				const std::string_view name = m_counterNames.Get(counterId);
				const CounterStats& stats = m_counters[counterId];
				char value[32];
				FormatCounter(value, sizeof(value), Counters == CounterDisplay::Average && stats.HasInterval ? stats.Average : stats.Latest, stats.Unit);

				const float width = ImGui::CalcTextSize(name.data(), name.data() + name.size()).x + style.ItemInnerSpacing.x + ImGui::CalcTextSize(value).x;
				if (counterId > 0)
				{
					ImGui::SameLine(0.0f, gap);
					if (ImGui::GetCursorScreenPos().x + width > right)
					{
						ImGui::NewLine();
					}
				}

				ImGui::BeginGroup();
				ImGui::TextDisabled("%.*s", int(name.size()), name.data());
				ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
				ImGui::TextUnformatted(value);
				ImGui::EndGroup();
				if ((stats.HasInterval || hasHistory) && ImGui::BeginItemTooltip())
				{
					char latest[32];
					FormatCounter(latest, sizeof(latest), stats.Latest, stats.Unit);
					ImGui::Text("Latest %s", latest);
					if (stats.HasInterval)
					{
						char minText[32];
						char avgText[32];
						char maxText[32];
						FormatCounter(minText, sizeof(minText), stats.IntervalMin, stats.Unit);
						FormatCounter(avgText, sizeof(avgText), stats.Average, stats.Unit);
						FormatCounter(maxText, sizeof(maxText), stats.IntervalMax, stats.Unit);
						ImGui::Text("Min %s  Avg %s  Max %s", minText, avgText, maxText);
						ImGui::TextDisabled("over the last %.1f s", double(m_config.StatsInterval));
					}
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
					if (hasHistory)
					{
						const int offset = m_historyCount == historyLength ? int(m_historyHead) : 0;
						ImGui::PlotLines("##History", &m_counterHistory[counterId * historyLength], int(m_historyCount), offset, nullptr, FLT_MAX, FLT_MAX, ImVec2(ImGui::GetFontSize() * 16.0f, ImGui::GetFontSize() * 3.0f));
						ImGui::TextDisabled("last %d frames", int(m_historyCount));
					}
#endif
					ImGui::EndTooltip();
				}
			}
		}

		// Each track is a label line plus graph; tracks split the remaining space
		void RenderTracks()
		{
			size_t visibleCount = 0;
			float totalWeight = 0.0f;
			float sharedScale = 0.0f;
			for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
			{
				ProfilerTrack& track = m_tracks[trackIndex];
				if (!track.Visible)
				{
					continue;
				}
				visibleCount++;
				totalWeight += std::max(track.HeightWeight, 0.01f);
				m_trackScales[trackIndex] = m_autoScale ? track.Graph.GetAutoScaleTime() : m_scaleMs / 1000.0f;
				sharedScale = std::max(sharedScale, m_trackScales[trackIndex]);
			}
			if (visibleCount == 0)
			{
				return;
			}
			if (SharedScale)
			{
				std::fill(m_trackScales.begin(), m_trackScales.end(), sharedScale);
			}

			const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float labelHeight = (ProfilerFlags & ProfilerWindowFlags_NoTrackLabels) ? 0.0f : ImGui::GetTextLineHeightWithSpacing();
			switch (Layout)
			{
			case TrackLayout::Stacked:
			{
				const float graphsHeight = available.y - float(visibleCount) * labelHeight - float(visibleCount - 1) * spacing.y;
				for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
				{
					const ProfilerTrack& track = m_tracks[trackIndex];
					if (track.Visible)
					{
						const float height = std::max(std::floor(graphsHeight * std::max(track.HeightWeight, 0.01f) / totalWeight), float(m_config.MinGraphHeight));
						RenderTrack(trackIndex, ImVec2(0.0f, height), false);
					}
				}
				break;
			}
			case TrackLayout::SideBySide:
			{
				const float graphsWidth = available.x - float(visibleCount - 1) * spacing.x;
				const float height = std::max(available.y - labelHeight, float(m_config.MinGraphHeight));
				bool first = true;
				for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
				{
					const ProfilerTrack& track = m_tracks[trackIndex];
					if (!track.Visible)
					{
						continue;
					}
					if (!first)
					{
						ImGui::SameLine();
					}
					first = false;

					const float width = std::max(std::floor(graphsWidth * std::max(track.HeightWeight, 0.01f) / totalWeight), 1.0f);
					ImGui::BeginGroup();
					RenderTrack(trackIndex, ImVec2(width, height), false);
					ImGui::EndGroup();
				}
				break;
			}
			case TrackLayout::Tabs:
			{
				if (ImGui::BeginTabBar("Tracks"))
				{
					for (size_t trackIndex = 0; trackIndex < m_tracks.size(); trackIndex++)
					{
						if (!m_tracks[trackIndex].Visible)
						{
							continue;
						}
						ImGui::PushID(int(trackIndex));
						if (ImGui::BeginTabItem(m_tracks[trackIndex].Label))
						{
							RenderTrack(trackIndex, ImVec2(0.0f, 0.0f), true);
							ImGui::EndTabItem();
						}
						ImGui::PopID();
					}
					ImGui::EndTabBar();
				}
				break;
			}
			}
		}

		// In a tab the label is on the tab, so show only the scale
		void RenderTrack(size_t trackIndex, ImVec2 size, bool inTab)
		{
			ProfilerTrack& track = m_tracks[trackIndex];
			const float scaleTime = m_trackScales[trackIndex];
			ImGui::PushID(int(trackIndex));
			if (!(ProfilerFlags & ProfilerWindowFlags_NoTrackLabels))
			{
				char scaleText[32];
				FormatTime(scaleText, sizeof(scaleText), scaleTime);
				if (inTab)
				{
					ImGui::TextDisabled("scale %s", scaleText);
				}
				else
				{
					ImGui::TextDisabled("%s  (scale %s)", track.Label, scaleText);
				}
#ifndef KOI_PROFILER_NO_TABLE
				ImGui::SameLine();
				if (ImGui::SmallButton(track.View == TrackView::Graph ? "Table" : "Graph"))
				{
					track.View = track.View == TrackView::Graph ? TrackView::Table : TrackView::Graph;
					m_settingsDirty = true;
				}
#endif
			}
			if (inTab)
			{
				size.y = std::max(ImGui::GetContentRegionAvail().y, float(m_config.MinGraphHeight));
			}

#ifndef KOI_PROFILER_NO_TABLE
			if (track.View == TrackView::Table)
			{
				track.Graph.RenderTable(size);
#ifndef KOI_PROFILER_NO_EDITORS
				m_graphHovered |= ImGui::IsItemHovered();
#endif
				ImGui::PopID();
				return;
			}
#endif

			// Click pauses on a frame; scrolling while paused steps frames
			const int clickedFrame = track.Graph.Render(size, m_frameOffset, scaleTime);
			const bool hovered = ImGui::IsItemHovered();
#ifndef KOI_PROFILER_NO_EDITORS
			m_graphHovered |= hovered;
#endif
#ifndef KOI_PROFILER_NO_INTERACTION
			if (clickedFrame >= 0)
			{
				m_paused      = true;
				m_frameOffset = clickedFrame;
			}
			else if (hovered && m_paused && ImGui::GetIO().MouseWheel != 0.0f)
			{
				m_frameOffset = std::clamp(m_frameOffset + (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1), 0, int(m_maxFramesCount) - 1);
			}
#else
			(void)clickedFrame;
			(void)hovered;
#endif
			ImGui::PopID();
		}

	public:
		ProfilerWindowFlags ProfilerFlags;
		TrackLayout         Layout;
		bool                SharedScale;
		TimeUnit            Unit;                   // use SetUnit to update every track
		CounterDisplay      Counters;
		float               AutoPauseMs;
#ifndef KOI_PROFILER_NO_OVERLAY
		OverlaySettings     Overlay;
#endif
		float               BackgroundAlpha = -1.0f; // negative keeps the style's background

	private:
		WindowConfig               m_config;
		std::vector<ProfilerTrack> m_tracks;
		internal::NameTable        m_counterNames;
		std::vector<CounterStats>  m_counters;
#ifndef KOI_PROFILER_NO_COUNTER_HISTORY
		std::vector<float>         m_counterHistory; // MaxCounters x CounterHistoryLength rings
		size_t                     m_historyHead  = 0;
		size_t                     m_historyCount = 0;
#endif
#ifndef KOI_PROFILER_NO_OVERLAY
		std::vector<float>         m_frameTimes;     // ring of real frame times, for the overlay
		size_t                     m_frameTimeHead  = 0;
		size_t                     m_frameTimeCount = 0;
		TimePoint                  m_prevFrameTime  = Clock::now();
#endif
		std::vector<float>         m_trackScales;
		size_t                     m_maxFramesCount       = 1;
		size_t                     m_autoPauseTrack       = 0;
		TimePoint                  m_prevFpsFrameTime     = Clock::now();
		TimePoint                  m_counterIntervalStart = Clock::now();
		size_t                     m_fpsFramesCount       = 0;
		float                      m_avgFrameTime         = 1.0f / 60.0f;
		float                      m_autoPauseMs          = 0.0f;
		int                        m_budgetFps            = 0;
		int                        m_lastUpdateFrame      = -1;
		float                      m_scaleMs;
		int                        m_frameOffset          = 0;
		bool                       m_autoScale;
		bool                       m_paused               = false;
		bool                       m_pausePending         = false;
		bool                       m_autoPaused           = false;
#ifndef KOI_PROFILER_NO_EDITORS
		bool                       m_graphHovered         = false;
#endif
		bool                       m_settingsLoaded       = false;
		bool                       m_settingsDirty        = false;
	};
}

// Hooks to also forward to another profiler, e.g. Tracy:
//     #define KOI_PROFILE_HOOK(name) ZoneScopedN(name)
//     #define KOI_PROFILE_DYNAMIC_HOOK(name) ZoneTransientN(koiZone, name, true)
//     #define KOI_PROFILE_FUNCTION_HOOK() ZoneScoped
//     #define KOI_PROFILE_COUNT_HOOK(name, value) TracyPlot(name, koi::prof::CounterValue(value))
// Define before including. They run even without KOI_PROFILER_ENABLE_STATS.
#ifndef KOI_PROFILE_HOOK
	#define KOI_PROFILE_HOOK(name)
#endif
#ifndef KOI_PROFILE_DYNAMIC_HOOK
	#define KOI_PROFILE_DYNAMIC_HOOK(name)
#endif
#ifndef KOI_PROFILE_FUNCTION_HOOK
	#define KOI_PROFILE_FUNCTION_HOOK()
#endif
#ifndef KOI_PROFILE_COUNT_HOOK
	#define KOI_PROFILE_COUNT_HOOK(name, value)
#endif
#ifndef KOI_PROFILE_VALUE_HOOK
	#define KOI_PROFILE_VALUE_HOOK(name, value)
#endif

#if defined(_MSC_VER)
	#define KOI_PROF_FUNCTION_NAME __FUNCTION__
#else
	#define KOI_PROF_FUNCTION_NAME __func__
#endif
#define KOI_PROF_CONCAT_INNER(a, b) a##b
#define KOI_PROF_CONCAT(a, b) KOI_PROF_CONCAT_INNER(a, b)

// Opt-in CPU scopes and counters that feed a ProfilerWindow.
// Define KOI_PROFILER_ENABLE_STATS to enable; without it the macros only run their hooks.
#ifdef KOI_PROFILER_ENABLE_STATS
namespace koi::prof::stats
{
	// Single-threaded: record only from the thread calling NewFrame
	namespace internal
	{
		using Clock = std::chrono::steady_clock;

		struct State
		{
			Clock::time_point         FrameStart = Clock::now();
			std::vector<ProfilerTask> Current;
			std::vector<ProfilerTask> Last;
			std::vector<Counter>      CurrentCounters;
			std::vector<Counter>      LastCounters;
			uint32_t                  Depth    = 0;
			bool                      Reserved = false;
		};

		[[nodiscard]] inline State& GetState()
		{
			static State state;
			return state;
		}

		inline void WriteCounter(std::string_view name, double value, CounterUnit unit, bool replace)
		{
			std::vector<Counter>& counters = GetState().CurrentCounters;
			for (Counter& counter : counters)
			{
				if (counter.Name == name)
				{
					counter.Value = replace ? value : counter.Value + value;
					counter.Unit  = unit;
					return;
				}
			}

			if (counters.size() == counters.capacity())
			{
				KOI_PROF_ASSERT(false, "Too many counters in one frame, raise the count passed to koi::prof::stats::Reserve");
				return;
			}
			counters.push_back({ name, value, unit });
		}
	}

	// Call before the first NewFrame; defaults to 128 scopes and 64 counters
	inline void Reserve(size_t maxScopes, size_t maxCounters)
	{
		internal::State& state = internal::GetState();
		state.Current.reserve(maxScopes);
		state.Last.reserve(maxScopes);
		state.CurrentCounters.reserve(maxCounters);
		state.LastCounters.reserve(maxCounters);
		state.Reserved = true;
	}

	// Call at the start of every frame; the previous frame becomes GetLastFrame/GetLastCounters
	inline void NewFrame()
	{
		internal::State& state = internal::GetState();
		if (!state.Reserved)
		{
			Reserve(128, 64);
		}

		std::swap(state.Current, state.Last);
		std::swap(state.CurrentCounters, state.LastCounters);
		state.Current.clear();
		state.CurrentCounters.clear();
		state.FrameStart = internal::Clock::now();
	}

	// Names aren't copied; keep them alive until the frame is loaded
	[[nodiscard]] inline const std::vector<ProfilerTask>& GetLastFrame() { return internal::GetState().Last; }
	[[nodiscard]] inline const std::vector<Counter>& GetLastCounters() { return internal::GetState().LastCounters; }

	// Seconds since the frame began
	[[nodiscard]] inline double Now()
	{
		return std::chrono::duration<double>(internal::Clock::now() - internal::GetState().FrameStart).count();
	}

	// Adds to a counter for this frame. value is a number, Bytes, Percent, a std::chrono duration or any type with CounterTraits.
	template <typename T>
	void Count(std::string_view name, const T& value)
	{
		internal::WriteCounter(name, CounterTraits<T>::ToValue(value), CounterTraits<T>::Unit, false);
	}

	inline void Count(std::string_view name) { Count(name, 1); }

	// Sets a counter for this frame, replacing earlier writes. For levels like memory in use rather than tallies.
	template <typename T>
	void Set(std::string_view name, const T& value)
	{
		internal::WriteCounter(name, CounterTraits<T>::ToValue(value), CounterTraits<T>::Unit, true);
	}

	// Times its lifetime. Only the outermost scope is recorded; nested ones fold into it.
	class Scope
	{
	public:
		// nameHash is HashName(name), or 0 to hash on load
		explicit Scope(std::string_view name, uint32_t nameHash = 0)
			: m_name(name)
			, m_nameHash(nameHash)
			, m_active(internal::GetState().Depth++ == 0)
		{
			if (m_active)
			{
				m_start = Now();
			}
		}

#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		// Location needs static storage, as the macros provide
		Scope(std::string_view name, uint32_t nameHash, const SourceLocation* location)
			: m_name(name)
			, m_location(location)
			, m_nameHash(nameHash)
			, m_active(internal::GetState().Depth++ == 0)
		{
			if (m_active)
			{
				m_start = Now();
			}
		}
#endif

		~Scope()
		{
			internal::State& state = internal::GetState();
			state.Depth--;
			if (!m_active)
			{
				return;
			}

			if (state.Current.size() == state.Current.capacity())
			{
				KOI_PROF_ASSERT(false, "Too many scopes in one frame, raise the count passed to koi::prof::stats::Reserve");
				return;
			}
			ProfilerTask& task = state.Current.emplace_back();
			task.StartTime = m_start;
			task.EndTime   = Now();
			task.Name      = m_name;
			task.NameHash  = m_nameHash;
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
			task.Location  = m_location;
#endif
		}

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		std::string_view      m_name;
#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		const SourceLocation* m_location = nullptr;
#endif
		double                m_start    = 0.0;
		uint32_t              m_nameHash = 0;
		bool                  m_active   = false;
	};
}

	#ifndef KOI_PROFILER_NO_SOURCE_LOCATION
		// Static location next to the scope, so recording costs a pointer
		#define KOI_PROFILE_IMPL(name, hash, var) \
			static constexpr ::koi::prof::SourceLocation KOI_PROF_CONCAT(var, Location){ __FILE__, KOI_PROF_FUNCTION_NAME, uint32_t(__LINE__) }; \
			::koi::prof::stats::Scope var{ name, hash, &KOI_PROF_CONCAT(var, Location) }
	#else
		#define KOI_PROFILE_IMPL(name, hash, var) ::koi::prof::stats::Scope var{ name, hash }
	#endif
	// Hashed at compile time, so the name must be a constant like a string literal
	#define KOI_PROFILE_HASHED(name, var) \
		static constexpr uint32_t KOI_PROF_CONCAT(var, Hash) = ::koi::prof::HashName(name); \
		KOI_PROFILE_IMPL(name, KOI_PROF_CONCAT(var, Hash), var)
	// The function name is fixed per call site, so it's hashed once
	#define KOI_PROFILE_FUNCTION_IMPL(var) \
		static const uint32_t KOI_PROF_CONCAT(var, Hash) = ::koi::prof::HashName(KOI_PROF_FUNCTION_NAME); \
		KOI_PROFILE_IMPL(KOI_PROF_FUNCTION_NAME, KOI_PROF_CONCAT(var, Hash), var)

	#define KOI_PROFILE(name) KOI_PROFILE_HOOK(name); KOI_PROFILE_HASHED(name, KOI_PROF_CONCAT(koiStatsScope, __COUNTER__))
	#define KOI_PROFILE_DYNAMIC(name) KOI_PROFILE_DYNAMIC_HOOK(name); KOI_PROFILE_IMPL(name, 0, KOI_PROF_CONCAT(koiStatsScope, __COUNTER__))
	#define KOI_PROFILE_FUNCTION() KOI_PROFILE_FUNCTION_HOOK(); KOI_PROFILE_FUNCTION_IMPL(KOI_PROF_CONCAT(koiStatsScope, __COUNTER__))
	// The value is variadic so types with commas work, e.g. std::chrono::duration<double, std::milli>(x)
	#define KOI_PROFILE_COUNT(name, ...) \
		do \
		{ \
			KOI_PROFILE_COUNT_HOOK(name, (__VA_ARGS__)); \
			::koi::prof::stats::Count(name, __VA_ARGS__); \
		} while (false)
	#define KOI_PROFILE_VALUE(name, ...) \
		do \
		{ \
			KOI_PROFILE_VALUE_HOOK(name, (__VA_ARGS__)); \
			::koi::prof::stats::Set(name, __VA_ARGS__); \
		} while (false)
#else
	#define KOI_PROFILE(name) KOI_PROFILE_HOOK(name)
	#define KOI_PROFILE_DYNAMIC(name) KOI_PROFILE_DYNAMIC_HOOK(name)
	#define KOI_PROFILE_FUNCTION() KOI_PROFILE_FUNCTION_HOOK()
	#define KOI_PROFILE_COUNT(name, ...) \
		do \
		{ \
			KOI_PROFILE_COUNT_HOOK(name, (__VA_ARGS__)); \
		} while (false)
	#define KOI_PROFILE_VALUE(name, ...) \
		do \
		{ \
			KOI_PROFILE_VALUE_HOOK(name, (__VA_ARGS__)); \
		} while (false)
#endif
