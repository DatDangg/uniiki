#include "uniiki_addon.h"
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>
#include <fcitx-utils/key.h>

UniikiEngine::UniikiEngine(fcitx::Instance* instance)
    : instance_(instance), factory_([this](fcitx::InputContext&) { return new UniikiState(); }) {
    instance_->inputContextManager().registerProperty("uniikiState", &factory_);
}

void UniikiEngine::keyEvent(const fcitx::InputMethodEntry& entry, fcitx::KeyEvent& keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }

    auto* ic = keyEvent.inputContext();
    auto* state = ic->propertyFor(&factory_);
    if (!state) return;

    auto key = keyEvent.key();
    if (key.hasModifier()) {
        state->engine().resetBuffer();
        return;
    }

    uint32_t sym = key.sym();
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
        char ch = 'a' + (sym - FcitxKey_a);
        auto result = state->engine().processKey(ch);

        if (result.action == EngineAction::MODIFY) {
            keyEvent.filterAndAccept();
            for (size_t i = 0; i < result.backspace_count; ++i) {
                ic->deleteSurroundingText(-1, 1);
            }
            ic->commitString(result.text);
        } else if (result.action == EngineAction::RESET) {
            state->engine().resetBuffer();
        }
    } else {
        state->engine().resetBuffer();
    }
}

fcitx::AddonInstance* UniikiEngineFactory::create(fcitx::AddonManager* manager) {
    return new UniikiEngine(manager->instance());
}

FCITX_ADDON_FACTORY(UniikiEngineFactory)
