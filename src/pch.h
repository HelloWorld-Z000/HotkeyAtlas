#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <Windows.h>
#include <Xinput.h>  // types only: XInputGetState is loaded at runtime

namespace logger = SKSE::log;
namespace fs     = std::filesystem;
using namespace std::literals;

// Marks English text that is stored now and translated where it is shown (TL). Every N_,
// TL and TLF literal goes into Translations/english.txt, the template for translators.
#define N_(s) s
