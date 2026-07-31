#ifndef UNIIKI_ADDON_H
#define UNIIKI_ADDON_H

#include <fcitx/inputmethodengine.h>
#include <fcitx/addonfactory.h>
#include "engine.h"

class UniikiState : public fcitx::InputContextProperty {
public:
    UniikiState() : engine_("telex", true) {}
    VietnameseEngine& engine() { return engine_; }
private:
    VietnameseEngine engine_;
};

class UniikiEngine : public fcitx::InputMethodEngineV2 {
public:
    UniikiEngine(fcitx::Instance* instance);
    void keyEvent(const fcitx::InputMethodEntry& entry, fcitx::KeyEvent& keyEvent) override;

private:
    fcitx::Instance* instance_;
    fcitx::FactoryFor<UniikiState> factory_;
};

class UniikiEngineFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance* create(fcitx::AddonManager* manager) override;
};

#endif // UNIIKI_ADDON_H
