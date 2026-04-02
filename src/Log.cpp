#include "MouseSensitivityFix/Log.h"

#if MSF_USE_COMMONLIBSSE
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#else
#include <iostream>
#endif

#include <filesystem>

namespace msf
{
    bool InitializeLogging()
    {
#if MSF_USE_COMMONLIBSSE
        namespace logger = SKSE::log;

        auto logDirectory = logger::log_directory();
        if (!logDirectory) {
            return false;
        }

        *logDirectory /= "MouseSensitivityFix.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDirectory->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        spdlog::info("MouseSensitivityFix logger initialized.");
        return true;
#else
        std::cout << "[MouseSensitivityFix] Logger initialized in stub mode.\n";
        return true;
#endif
    }

    void LogInfo(std::string_view message)
    {
#if MSF_USE_COMMONLIBSSE
        spdlog::info("{}", message);
#else
        std::cout << "[MouseSensitivityFix] " << message << "\n";
#endif
    }

    void LogWarn(std::string_view message)
    {
#if MSF_USE_COMMONLIBSSE
        spdlog::warn("{}", message);
#else
        std::cout << "[MouseSensitivityFix][WARN] " << message << "\n";
#endif
    }

    void LogError(std::string_view message)
    {
#if MSF_USE_COMMONLIBSSE
        spdlog::error("{}", message);
#else
        std::cout << "[MouseSensitivityFix][ERROR] " << message << "\n";
#endif
    }

    void LogDebug(std::string_view message)
    {
#if MSF_USE_COMMONLIBSSE
        spdlog::debug("{}", message);
#else
        std::cout << "[MouseSensitivityFix][DEBUG] " << message << "\n";
#endif
    }
}
