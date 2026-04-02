#pragma once

#include "MouseSensitivityFix/Config.h"

namespace msf
{
    class MenuFrameworkBridge
    {
    public:
        bool Initialize();
        void Shutdown();

        // Called when the SKSE Menu Framework UI writes new values.
        void OnSettingsApplied(const ConfigValues& updatedValues);
    };
}
