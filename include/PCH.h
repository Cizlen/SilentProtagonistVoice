#pragma once

// CommonLib first. SKSEMenuFramework.h pulls in <windows.h>, so it is only
// included from UI.cpp, after this header, to keep Win32 macros out of the
// engine headers.
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace logger = SKSE::log;
using namespace std::literals;

#ifndef MOD_NAME
#	define MOD_NAME "Silent Protagonist Voice"
#endif
