#ifndef UNIIKI_FCITX5_ENGINE_H_
#define UNIIKI_FCITX5_ENGINE_H_

#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx-utils/event.h>

#include <string>
#include <deque>
#include <memory>
#include <utility>
#include <unordered_map>
#include <vector>

namespace fcitx {

class UniikiState : public InputContextProperty {
public:
    void reset();

    struct PendingKey {
        uint64_t eventId = 0;
        Key key;
        Key rawKey;
        bool isRelease = false;
        int time = 0;
    };

    struct ReplacementTransaction {
        uint64_t id = 0;
        std::string oldRendered;
        std::string newRendered;
        unsigned int deleteUnits = 0;
        bool completed = false;
    };

    std::string rawBuffer;
    std::string displayText;
    std::string lastRenderedText;
    std::string recoverRawBuffer;
    std::string recoverRenderedText;
    std::string recoverSuffix;
    bool directActive = false;
    bool isInternalCommit = false;
    bool replacementInProgress = false;
    std::deque<PendingKey> pendingKeys;
    ReplacementTransaction replacement;
    uint64_t revision = 0;
};

class UniikiEngine final : public InputMethodEngineV2 {
public:
    explicit UniikiEngine(Instance *instance);

    void keyEvent(const InputMethodEntry &entry, KeyEvent &event) override;
    void reset(const InputMethodEntry &entry, InputContextEvent &event) override;
    static std::string evaluateTelexForTest(const std::string &raw);
    static std::string simulateDirectForTest(const std::string &raw);
    static std::pair<unsigned int, std::string>
    replacementDeltaForTest(const std::string &oldText, const std::string &newText);

private:
    struct ProcessResult {
        bool handled = false;
        bool changed = false;
        std::string text;
        bool consumedAsTelex = false;
        bool rawAccepted = false;
        bool validComposition = false;
    };

    void processPendingKeys(InputContext *ic, UniikiState &state) const;
    void processPendingKey(InputContext *ic, UniikiState &state,
                           const UniikiState::PendingKey &pending) const;
    void completeReplacement(InputContext *ic, UniikiState &state,
                             const char *source) const;
    ProcessResult processChar(UniikiState &state, char ch) const;
    static std::string evaluateTelex(const std::string &raw);
    bool canUseDirectCommit(InputContext *ic, const UniikiState &state) const;
    unsigned int surroundingDeleteUnits(const std::string &utf8Text) const;
    void setOwnedLocalSurrounding(InputContext *ic, const std::string &text) const;
    void renderDirectCommit(InputContext *ic, UniikiState &state, const std::string &physicalKey,
                            const std::string &rawBefore,
                            const std::string &lastRenderedBefore, uint64_t eventId) const;
    void fallbackCommitRaw(InputContext *ic, UniikiState &state, const std::string &text,
                           const std::string &reason) const;
    void finishDirectComposition(InputContext *ic, UniikiState &state,
                                 const std::string &boundary = "",
                                 bool commitBoundary = false) const;

    Instance *instance_;
    FactoryFor<UniikiState> factory_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> surroundingUpdatedWatcher_;
};

class UniikiFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // UNIIKI_FCITX5_ENGINE_H_
