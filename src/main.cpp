#include "MouseSensitivityFix/Plugin.h"
#include "MouseSensitivityFix/Log.h"

#if MSF_USE_COMMONLIBSSE
#include <SKSE/SKSE.h>

extern "C"
{
    __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse)
    {
        msf::InitializeLogging();
        SKSE::Init(skse);
        return msf::Plugin::Initialize();
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
