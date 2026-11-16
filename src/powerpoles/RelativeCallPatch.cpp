#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "RelativeCallPatch.h"

#include <Windows.h>

#include <cstring>
#include <limits>

#include "SimLogFormat.h"

namespace
{
    constexpr size_t kCallInstructionLength = 5;
}

namespace PowerPoles
{
    RelativeCallPatch::RelativeCallPatch(const std::string_view name,
                                         const uintptr_t callSiteAddress,
                                         void* hookFn,
                                         const uintptr_t expectedTarget)
    {
        Configure(name, callSiteAddress, hookFn, expectedTarget);
    }

    void RelativeCallPatch::Configure(const std::string_view name, const uintptr_t callSiteAddress, void* hookFn,
                                      const uintptr_t expectedTarget)
    {
        name_ = name;
        callSiteAddress_ = callSiteAddress;
        hookFn_ = hookFn;
        expectedTarget_ = expectedTarget;
        originalRel_ = 0;
        hookRel_ = 0;
        originalTarget_ = 0;
        installed_ = false;
    }

    bool RelativeCallPatch::Install()
    {
        if (installed_) {
            return true;
        }

        if (callSiteAddress_ == 0 || hookFn_ == nullptr) {
            LOG_ERROR("RelativeCallPatch: incomplete call patch configuration for {}", name_);
            return false;
        }

        auto* const site = reinterpret_cast<uint8_t*>(callSiteAddress_);
        if (site[0] != 0xE8) {
            LOG_ERROR("RelativeCallPatch: expected CALL rel32 at 0x{:08X} for {}",
                      static_cast<uint32_t>(callSiteAddress_), name_);
            return false;
        }

        std::memcpy(&originalRel_, site + 1, sizeof(originalRel_));
        originalTarget_ = callSiteAddress_ + kCallInstructionLength + originalRel_;
        // Verify before writing: a call site that does not target the expected function (another
        // game version, or another DLL already hooked it) is left exactly as it is.
        if (expectedTarget_ != 0 && originalTarget_ != expectedTarget_) {
            LOG_ERROR("RelativeCallPatch: {} call site 0x{:08X} targets 0x{:08X}, expected 0x{:08X}; not patching.",
                      name_, static_cast<uint32_t>(callSiteAddress_), static_cast<uint32_t>(originalTarget_),
                      static_cast<uint32_t>(expectedTarget_));
            return false;
        }

        int32_t newRel = 0;
        if (!ComputeRelativeCallTarget(callSiteAddress_, reinterpret_cast<uintptr_t>(hookFn_), newRel)) {
            LOG_ERROR("RelativeCallPatch: rel32 range failure for {}", name_);
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(site + 1, sizeof(newRel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("RelativeCallPatch: VirtualProtect failed for {}", name_);
            return false;
        }

        std::memcpy(site + 1, &newRel, sizeof(newRel));
        FlushInstructionCache(GetCurrentProcess(), site, kCallInstructionLength);
        VirtualProtect(site + 1, sizeof(newRel), oldProtect, &oldProtect);

        hookRel_ = newRel;
        installed_ = true;
        return true;
    }

    void RelativeCallPatch::Uninstall()
    {
        if (!installed_) {
            return;
        }

        auto* const site = reinterpret_cast<uint8_t*>(callSiteAddress_);
        int32_t currentRel = 0;
        std::memcpy(&currentRel, site + 1, sizeof(currentRel));
        if (site[0] != 0xE8 || currentRel != hookRel_) {
            // Another DLL hooked the site after us and chains to our hook: restoring the vanilla
            // target would silently remove its hook. Leave the site alone.
            LOG_WARN("RelativeCallPatch: {} call site 0x{:08X} was changed by someone else; left as it is.",
                     name_, static_cast<uint32_t>(callSiteAddress_));
            installed_ = false;
            return;
        }
        DWORD oldProtect = 0;
        if (VirtualProtect(site + 1, sizeof(originalRel_), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(site + 1, &originalRel_, sizeof(originalRel_));
            FlushInstructionCache(GetCurrentProcess(), site, kCallInstructionLength);
            VirtualProtect(site + 1, sizeof(originalRel_), oldProtect, &oldProtect);
        }

        installed_ = false;
    }

    bool RelativeCallPatch::IsInstalled() const noexcept
    {
        return installed_;
    }

    uintptr_t RelativeCallPatch::GetOriginalTarget() const noexcept
    {
        return originalTarget_;
    }

    bool RelativeCallPatch::ComputeRelativeCallTarget(const uintptr_t callSiteAddress,
                                                      const uintptr_t targetAddress,
                                                      int32_t& relOut)
    {
        const auto delta = static_cast<int64_t>(targetAddress) -
            static_cast<int64_t>(callSiteAddress + kCallInstructionLength);
        if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) {
            return false;
        }

        relOut = static_cast<int32_t>(delta);
        return true;
    }
}
