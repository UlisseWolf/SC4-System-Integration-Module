// SC4SIM DLL - rewrites the rel32 target of one CALL instruction (power-pole hooks). The expected target is
// checked before anything is written; Uninstall restores the call only if it still holds our hook.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace PowerPoles
{
    class RelativeCallPatch final
    {
    public:
        RelativeCallPatch() = default;
        RelativeCallPatch(std::string_view name, uintptr_t callSiteAddress, void* hookFn,
                          uintptr_t expectedTarget = 0);

        // expectedTarget: the function the CALL must target before it is patched (0 = any). It is
        // checked before anything is written, so a site that does not match is never touched.
        void Configure(std::string_view name, uintptr_t callSiteAddress, void* hookFn, uintptr_t expectedTarget = 0);

        [[nodiscard]] bool Install();
        void Uninstall();

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] uintptr_t GetOriginalTarget() const noexcept;

    private:
        static bool ComputeRelativeCallTarget(uintptr_t callSiteAddress, uintptr_t targetAddress, int32_t& relOut);

    private:
        std::string name_{};
        uintptr_t callSiteAddress_ = 0;
        void* hookFn_ = nullptr;
        uintptr_t expectedTarget_ = 0;
        int32_t originalRel_ = 0;
        int32_t hookRel_ = 0;
        uintptr_t originalTarget_ = 0;
        bool installed_ = false;
    };
}
