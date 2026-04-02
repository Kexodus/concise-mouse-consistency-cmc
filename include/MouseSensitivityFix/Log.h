#pragma once

#include <string_view>

namespace msf
{
    bool InitializeLogging();

    void LogInfo(std::string_view message);
    void LogWarn(std::string_view message);
    void LogError(std::string_view message);
    void LogDebug(std::string_view message);
}
