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
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fcitx {

class UniikiState : public InputContextProperty {
public:
    void reset();

    enum class RenderOperation {
        AppendLiteral,
        ReplaceSuffix,
        NoVisibleChange,
    };

    struct PendingKey {
        uint64_t eventId = 0;
        Key key;
        Key rawKey;
        bool isRelease = false;
        int time = 0;
    };

    struct TraceRecord {
        uint64_t timestampMicros = 0;
        uint64_t threadId = 0;
        uint64_t eventId = 0;
        uintptr_t contextId = 0;
        uint64_t contextGeneration = 0;
        uint64_t transactionId = 0;
        std::string eventType;
        std::string key;
        std::string functionStage;
        std::string rawBefore;
        std::string rawAfter;
        std::string desiredRendered;
        std::string appliedRendered;
        std::string parserStateHash;
        uint64_t desiredRevision = 0;
        uint64_t appliedRevision = 0;
        uint64_t surroundingRevision = 0;
        size_t queueLength = 0;
        bool isPress = false;
        bool isRelease = false;
        bool isRepeat = false;
        bool processing = false;
        bool renderInFlight = false;
        bool waitingForSurrounding = false;
        bool focusState = false;
        std::string cursor;
        std::string anchor;
    };

    struct ReplacementTransaction {
        uint64_t id = 0;
        uint64_t inputVersion = 0;
        uint64_t commitId = 0;
        uint64_t startedAtMicros = 0;
        uint64_t contextGeneration = 0;
        std::string application;
        std::string frontend;
        std::string rawBefore;
        std::string rawAfter;
        std::string converterOutput;
        std::string oldRendered;
        std::string newRendered;
        std::string commonPrefix;
        std::string oldSuffix;
        std::string newSuffix;
        std::string commitText;
        std::string surroundingTextBefore;
        std::string beforeCursorBefore;
        std::string expectedAfterDelete;
        std::string expectedAfterCommit;
        std::string cursorBefore;
        std::string anchorBefore;
        std::string typedPrefixBefore;
        std::string autocompleteSuffixBefore;
        std::string expectedAfterAutocompleteStage;
        unsigned int compositionStart = 0;
        unsigned int selectionLength = 0;
        bool autocompleteSelection = false;
        int deleteOffset = 0;
        unsigned int deleteUnits = 0;
        bool suffixMatches = false;
        bool completed = false;
        RenderOperation operation = RenderOperation::NoVisibleChange;
    };

    std::string rawBuffer;
    // rawBuffer/displayText are the synchronous desired state. Applied state
    // advances when Fcitx edit requests are dispatched; surrounding text is
    // diagnostic input and never a pipeline lock.
    std::string displayText;
    std::string lastRenderedText;
    std::string lastAppliedRawBuffer;
    std::string recoverRawBuffer;
    std::string recoverRenderedText;
    std::string recoverSuffix;
    bool directActive = false;
    bool isInternalCommit = false;
    bool replacementInProgress = false;
    std::deque<PendingKey> pendingKeys;
    std::deque<TraceRecord> traceRing;
    std::unordered_set<uint64_t> appliedEventIds;
    ReplacementTransaction replacement;
    uint64_t revision = 0;
    uint64_t appliedRevision = 0;
    uint64_t desiredEventId = 0;
    std::string desiredPhysicalKey;
    uint64_t nextCommitId = 0;
    uint64_t contextGeneration = 0;
    uint64_t surroundingRevision = 0;
    bool focused = false;
};

class UniikiEngine final : public InputMethodEngineV2 {
public:
    struct RenderedGrapheme {
        std::string text;
        std::vector<size_t> rawIndexes;
    };

    struct ConversionResult {
        std::string rendered;
        std::vector<RenderedGrapheme> graphemes;
    };

    explicit UniikiEngine(Instance *instance);

    void keyEvent(const InputMethodEntry &entry, KeyEvent &event) override;
    void activate(const InputMethodEntry &entry, InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry, InputContextEvent &event) override;
    void reset(const InputMethodEntry &entry, InputContextEvent &event) override;
    static std::string evaluateTelexForTest(const std::string &raw);
    static std::string toneOwnershipForTest(const std::string &raw);
    static std::string converterSegmentationForTest(const std::string &raw);
    static std::string conversionTraceForTest(const std::string &raw);
    static bool rawOwnershipInvariantForTest(const std::string &raw);
    static std::string simulateDirectForTest(const std::string &raw);
    static ConversionResult conversionWithProvenanceForTest(
        const std::string &raw);
    static std::pair<std::string, std::string>
    visualBackspaceForTest(const std::string &raw);
    static std::string replacementTraceForTest(
        const std::string &visibleBefore, const std::string &oldRendered,
        const std::string &newRendered, uint64_t transactionId,
        uint64_t stateVersion);
    static std::string twoPhaseReplacementTraceForTest(
        const std::string &visibleBefore, const std::string &oldRendered,
        const std::string &newRendered, const std::string &visibleAfterDelete,
        const std::string &visibleAfterCommit);
    static std::string replacementSelectionPolicyForTest(
        unsigned int cursor, unsigned int anchor,
        const std::string &beforeCursor, const std::string &oldRendered);
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
    void scheduleLatestRender(InputContext *ic, UniikiState &state) const;
    void completeReplacement(InputContext *ic, UniikiState &state,
                             const char *source) const;
    void abortReplacement(InputContext *ic, UniikiState &state,
                          const char *reason) const;
    ProcessResult processChar(UniikiState &state, char ch) const;
    static std::string evaluateTelex(const std::string &raw);
    static std::string evaluateTelexCore(const std::string &raw, bool enableDTransform);
    static ConversionResult conversionWithProvenance(const std::string &raw);
    static std::string rawAfterVisualBackspace(const std::string &raw);
    bool canUseDirectCommit(InputContext *ic, const UniikiState &state) const;
    unsigned int surroundingDeleteUnits(const std::string &utf8Text) const;
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
    std::unique_ptr<HandlerTableEntry<EventHandler>> focusOutWatcher_;
};

class UniikiFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // UNIIKI_FCITX5_ENGINE_H_
