// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Explicitly unregisters sliders before their native addresses can be reused.
// - Inspects only the supplied owned hierarchy, including never-active tabs.

#include "saberstage/ui/SliderLifetime.hpp"
#include "saberstage/ui/SliderRegistration.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/Logging.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ListSliderSetting.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"

#include <cstdint>
#include <stdexcept>

namespace saberstage::ui {
namespace {
struct ReleaseCounts {
    std::size_t wrappers = 0;
    std::size_t removed = 0;
    std::size_t missing = 0;
    std::size_t mismatches = 0;
};

template <typename Wrapper, typename Registry>
void ReleaseWrappers(UnityEngine::GameObject* root, Registry& registry,
    std::string_view context, const char* kind, ReleaseCounts& counts) {
    // Use Unity's Type overload: these wrappers are mod-created IL2CPP types,
    // so a concrete AOT GetComponentsInChildren<Wrapper> is not guaranteed.
    for (auto component : root->GetComponentsInChildren(csTypeOf(Wrapper*), true)) {
        auto* wrapper = reinterpret_cast<Wrapper*>(component.ptr()); // Type-filtered by Unity above.
        if (!wrapper || !UnityEngine::Object::op_Inequality(wrapper, nullptr)) continue;
        ++counts.wrappers;
        auto* slider = wrapper->slider;
        switch (EraseOwnedSliderRegistration(registry, slider, wrapper)) {
            case SliderRegistrationRelease::Removed: ++counts.removed; break;
            case SliderRegistrationRelease::Missing: ++counts.missing; break;
            case SliderRegistrationRelease::ForeignOwner:
                ++counts.mismatches;
                Logging::Logger.error(
                    "SliderLifetime context={} kind={} ownership mismatch slider=0x{:x} wrapper=0x{:x}; preserving registration",
                    context, kind, reinterpret_cast<std::uintptr_t>(slider),
                    reinterpret_cast<std::uintptr_t>(wrapper));
                break;
        }
    }
}
} // namespace

bool ReleaseSliderRegistrations(UnityEngine::GameObject* ownedRoot, std::string_view context) noexcept {
    return ErrorManager::Instance().Guard(context, [&] {
        if (!ownedRoot || !UnityEngine::Object::op_Inequality(ownedRoot, nullptr)) return;
        ReleaseCounts counts;
        // BSML removes these entries in OnDestroy, but Unity never invokes it
        // for controls that never became active (e.g. an unopened Fit tab).
        // An address reused by Instantiate can then resolve the old formatter
        // during Awake, BEFORE the replacement wrapper has been constructed.
        // Unregister while descendants are still alive; do not clear the
        // shared map or wait for a destructor callback on an inactive object.
        ReleaseWrappers<BSML::SliderSetting>(ownedRoot, BSML::SliderSetting::remappers, context, "numeric", counts);
        ReleaseWrappers<BSML::ListSliderSetting>(ownedRoot, BSML::ListSliderSetting::remappers, context, "list", counts);
        if (counts.wrappers != 0) {
            Logging::Logger.info("SliderLifetime context={} wrappers={} removed={} unregistered={} mismatches={}",
                context, counts.wrappers, counts.removed, counts.missing, counts.mismatches);
        }
        if (counts.mismatches != 0) {
            throw std::runtime_error("Slider registration ownership mismatch; UI teardown/rebuild was stopped");
        }
    }, "SaberStage menu cleanup failed",
       "SaberStage could not safely release its old slider controls. The panel was retained and details were written to the log.");
}

} // namespace saberstage::ui
