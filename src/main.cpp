#include "MouseSensitivityFix/Plugin.h"
#include "MouseSensitivityFix/Log.h"

#include <cstdlib>

#if MSF_USE_COMMONLIBSSE
#include <SKSE/SKSE.h>

extern "C"
{
    __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse)
    {
        msf::InitializeLogging();
        SKSE::Init(skse);
        std::atexit([] { msf::Plugin::Shutdown(); });
        if (!msf::Plugin::Initialize()) {
            return false;
        }
        return true;
    }
}
#else
extern "C"
{
    __declspec(dllexport) bool MouseSensitivityFix_StubLoad()
    {
        msf::InitializeLogging();
        return msf::Plugin::Initialize();
    }
}
#endif
