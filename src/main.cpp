#include "MouseSensitivityFix/Plugin.h"
#include "MouseSensitivityFix/Log.h"

#include <cstdint>
#include <cstdlib>

#if MSF_USE_COMMONLIBSSE
#include <SKSE/SKSE.h>

#ifndef MSF_PLUGIN_VERSION_MAJOR
#define MSF_PLUGIN_VERSION_MAJOR 1
#endif
#ifndef MSF_PLUGIN_VERSION_MINOR
#define MSF_PLUGIN_VERSION_MINOR 0
#endif
#ifndef MSF_PLUGIN_VERSION_PATCH
#define MSF_PLUGIN_VERSION_PATCH 2
#endif
#ifndef MSF_PLUGIN_VERSION_TWEAK
#define MSF_PLUGIN_VERSION_TWEAK 0
#endif

constexpr SKSE::PluginVersionData MakePluginVersionData() noexcept
{
    SKSE::PluginVersionData version{};
    version.PluginVersion(REL::Version{
        static_cast<std::uint16_t>(MSF_PLUGIN_VERSION_MAJOR),
        static_cast<std::uint16_t>(MSF_PLUGIN_VERSION_MINOR),
        static_cast<std::uint16_t>(MSF_PLUGIN_VERSION_PATCH),
        static_cast<std::uint16_t>(MSF_PLUGIN_VERSION_TWEAK)});
    version.PluginName("MouseSensitivityFix");
    version.UsesAddressLibrary();
    version.UsesNoStructs();
    version.versionIndependenceEx |= SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5;
    return version;
}

SKSEPluginVersion = MakePluginVersionData();

static_assert(
    (MakePluginVersionData().versionIndependence &
     SKSE::PluginVersionData::kVersionIndependent_AddressLibraryPostAE) != 0,
    "SKSEPlugin_Version must declare Address Library Post-AE");
static_assert(
    (MakePluginVersionData().versionIndependenceEx &
     SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5) != 0,
    "SKSEPlugin_Version must declare Address Library v5 for Skyrim 1.7.99+");
static_assert(
    (MakePluginVersionData().versionIndependenceEx &
     SKSE::PluginVersionData::kVersionIndependentEx_NoStructUse) != 0,
    "SKSEPlugin_Version must declare struct independence for SE and AE 1.6.629+");

namespace
{
    // Hooks.cpp patches flat-runtime vtable slots. VR shifts FirstPersonState::Update (3->4),
    // ThirdPersonState::HandleLookInput (0x0F->0x10), and ModifyMovementData (0x11A->0x11C),
    // so loading on VR would overwrite unrelated virtuals.
    bool RejectUnsupportedRuntime()
    {
        if (!REL::Module::IsVR()) {
            return false;
        }
        msf::LogError("Skyrim VR is not supported. CMC will not load or install hooks.");
        return true;
    }
}

extern "C"
{
    SKSE_EXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
    {
        pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
        pluginInfo->name = SKSEPlugin_Version.GetPluginName().data();
        pluginInfo->version = SKSEPlugin_Version.pluginVersion;
        // SKSEVR uses the legacy Query/Load handshake; refusing here keeps Load from running.
        if (REL::Module::IsVR()) {
            msf::InitializeLogging();
            RejectUnsupportedRuntime();
            return false;
        }
        return true;
    }

    SKSE_EXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface* skse)
    {
        msf::InitializeLogging();
        if (RejectUnsupportedRuntime()) {
            return false;
        }
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
