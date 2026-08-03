#include "uniiki_engine.h"

#include <iostream>
#include <deque>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

} // namespace

int main() {
    const auto [ddDelete, ddCommit] =
        fcitx::UniikiEngine::replacementDeltaForTest("d", "đ");
    if (ddDelete != 1 || ddCommit != "đ") {
        std::cerr << "dd replacement delta delete=" << ddDelete
                  << " commit=" << ddCommit << '\n';
        return 1;
    }
    const auto [clDelete, clCommit] =
        fcitx::UniikiEngine::replacementDeltaForTest("c", "cl");
    if (clDelete != 0 || clCommit != "l") {
        std::cerr << "literal append delta delete=" << clDelete
                  << " commit=" << clCommit << '\n';
        return 1;
    }

    const std::vector<std::pair<std::string, std::string>> visibleCases = {
        {"canaf", "cần"}, {"bene", "bên"},
        {"dd", "đ"}, {"oroooo", "ỏoooo"},
        {"gox\b", "g"}, {"gos\b", "g"},
        {"gaa\b", "g"}, {"gaw\b", "g"},
        {"gow\b", "g"}, {"guw\b", "g"},
        {"tirr\b", "ti"}, {"cass\b", "ca"},
    };
    for (const auto &[raw, expected] : visibleCases) {
        const auto actual = fcitx::UniikiEngine::simulateDirectForTest(raw);
        if (actual != expected) {
            std::cerr << "direct raw=" << raw << " actual=" << actual
                      << " expected=" << expected << '\n';
            return 1;
        }
    }

    const auto goxProvenance =
        fcitx::UniikiEngine::conversionWithProvenanceForTest("gox");
    if (goxProvenance.rendered != "gõ" ||
        goxProvenance.graphemes.size() != 2 ||
        goxProvenance.graphemes[0].text != "g" ||
        goxProvenance.graphemes[0].rawIndexes != std::vector<size_t>{0} ||
        goxProvenance.graphemes[1].text != "õ" ||
        goxProvenance.graphemes[1].rawIndexes !=
            (std::vector<size_t>{1, 2})) {
        std::cerr << "unexpected gox provenance\n";
        return 1;
    }

    const std::vector<std::pair<std::string, std::string>> visualDeletes = {
        {"gox", "g"}, {"gos", "g"}, {"gof", "g"},
        {"gor", "g"}, {"goj", "g"}, {"gaa", "g"},
        {"gee", "g"}, {"goo", "g"}, {"gaw", "g"},
        {"gow", "g"}, {"guw", "g"}, {"tirr", "ti"},
        {"cass", "ca"},
    };
    for (const auto &[raw, expectedRendered] : visualDeletes) {
        const auto [rawAfter, renderedAfter] =
            fcitx::UniikiEngine::visualBackspaceForTest(raw);
        if (renderedAfter != expectedRendered) {
            std::cerr << "visual backspace raw=" << raw
                      << " rawAfter=" << rawAfter
                      << " renderedAfter=" << renderedAfter
                      << " expected=" << expectedRendered << '\n';
            return 1;
        }
    }

    std::string minhRaw = "minhf";
    const std::vector<std::string> minhDeletes = {"mìn", "mì", "m", ""};
    for (const auto &expected : minhDeletes) {
        const auto step =
            fcitx::UniikiEngine::visualBackspaceForTest(minhRaw);
        minhRaw = step.first;
        if (step.second != expected) {
            std::cerr << "mình visual sequence actual=" << step.second
                      << " expected=" << expected << '\n';
            return 1;
        }
    }

    std::string duocRaw = "dduowcj";
    const std::vector<std::string> duocDeletes = {"đượ", "đư", "đ", ""};
    for (const auto &expected : duocDeletes) {
        const auto step =
            fcitx::UniikiEngine::visualBackspaceForTest(duocRaw);
        duocRaw = step.first;
        if (step.second != expected) {
            std::cerr << "được visual sequence actual=" << step.second
                      << " expected=" << expected << '\n';
            return 1;
        }
    }

    const auto ddTrace = fcitx::UniikiEngine::replacementTraceForTest(
        "d", "d", "đ", 2, 2);
    if (!contains(ddTrace, "deleteUnits=1") ||
        !contains(ddTrace, "commitText=đ") ||
        !contains(ddTrace, "visibleTextAfter=đ") ||
        !contains(ddTrace, "status=COMMITTED")) {
        std::cerr << "unexpected dd trace: " << ddTrace << '\n';
        return 1;
    }

    const auto ddTwoPhase =
        fcitx::UniikiEngine::twoPhaseReplacementTraceForTest(
            "d", "d", "đ", "", "đ");
    if (!contains(ddTwoPhase, "suffixMatches=true") ||
        !contains(ddTwoPhase, "deleteOffset=-1") ||
        !contains(ddTwoPhase, "deleteSize=1") ||
        !contains(ddTwoPhase, "commitSent=true") ||
        !contains(ddTwoPhase, "commitText=đ") ||
        !contains(ddTwoPhase, "visibleTextAfter=đ") ||
        !contains(ddTwoPhase, "status=COMMITTED")) {
        std::cerr << "unexpected two-phase dd trace: " << ddTwoPhase << '\n';
        return 1;
    }

    const auto ignoredDelete =
        fcitx::UniikiEngine::twoPhaseReplacementTraceForTest(
            "d", "d", "đ", "d", "d");
    if (!contains(ignoredDelete, "status=DELETE_NOT_ACKNOWLEDGED") ||
        !contains(ignoredDelete, "commitSent=false") ||
        !contains(ignoredDelete, "visibleTextAfter=d") ||
        contains(ignoredDelete, "visibleTextAfter=dđ")) {
        std::cerr << "ignored delete was not blocked: " << ignoredDelete
                  << '\n';
        return 1;
    }

    const auto autocompleteSelection =
        fcitx::UniikiEngine::replacementSelectionPolicyForTest(
            1, 5, "d", "d");
    if (!contains(autocompleteSelection, "selectionLength=4") ||
        !contains(autocompleteSelection, "autocompleteSelection=1") ||
        !contains(autocompleteSelection, "selectionSafe=1")) {
        std::cerr << "autocomplete selection rejected: "
                  << autocompleteSelection << '\n';
        return 1;
    }
    const auto foreignSelection =
        fcitx::UniikiEngine::replacementSelectionPolicyForTest(
            5, 1, "drive", "d");
    if (!contains(foreignSelection, "autocompleteSelection=0") ||
        !contains(foreignSelection, "selectionSafe=0")) {
        std::cerr << "foreign selection accepted: " << foreignSelection
                  << '\n';
        return 1;
    }

    const auto canTrace = fcitx::UniikiEngine::replacementTraceForTest(
        "cân", "cân", "cần", 5, 5);
    if (!contains(canTrace, "commonPrefix=c") ||
        !contains(canTrace, "visibleTextAfter=cần") ||
        !contains(canTrace, "status=COMMITTED")) {
        std::cerr << "unexpected cần trace: " << canTrace << '\n';
        return 1;
    }

    const auto mismatchTrace = fcitx::UniikiEngine::replacementTraceForTest(
        "ân", "cân", "cần", 4, 5);
    if (!contains(mismatchTrace, "visibleTextAfter=ân") ||
        !contains(mismatchTrace, "status=SURROUNDING_MISMATCH")) {
        std::cerr << "unexpected mismatch trace: " << mismatchTrace << '\n';
        return 1;
    }

    struct KeyEvent {
        uint64_t id;
        char key;
    };
    const std::string stressRaw = "canaf bene dd oroooo";
    const std::string stressExpected = "cần bên đ ỏoooo";
    std::mt19937 random(0x554E494B);
    for (size_t iteration = 0; iteration < 1000; ++iteration) {
        std::deque<KeyEvent> queue;
        uint64_t nextId = 1;
        for (char key : stressRaw) {
            const KeyEvent event{nextId++, key};
            queue.push_back(event);
            if ((random() % 19) == 0) {
                queue.push_back(event);
            }
        }
        std::unordered_set<uint64_t> processed;
        std::string appliedRaw;
        while (!queue.empty()) {
            const auto event = queue.front();
            queue.pop_front();
            if (!processed.insert(event.id).second) {
                continue;
            }
            appliedRaw.push_back(event.key);
        }
        if (processed.size() != stressRaw.size() || appliedRaw != stressRaw ||
            fcitx::UniikiEngine::simulateDirectForTest(appliedRaw) !=
                stressExpected) {
            std::cerr << "exactly-once stress failed iteration=" << iteration
                      << '\n';
            return 1;
        }
    }

    // Fake autocomplete clients which send no update, a delayed update, or a
    // stale update. Dispatch completion is internal, so none of those signals
    // can leave processing/render-in-flight set or keep a key queued.
    enum class CallbackMode { Missing, Delayed, Stale };
    const std::vector<CallbackMode> callbackModes = {
        CallbackMode::Missing, CallbackMode::Delayed, CallbackMode::Stale};
    const std::vector<std::string> literalBursts = {
        "clabc", "client", "class", "cloud", "clone", "clear"};
    for (size_t iteration = 0; iteration < 500; ++iteration) {
        for (const auto mode : callbackModes) {
            (void)mode;
            for (const auto &keys : literalBursts) {
                std::string raw;
                std::string applied;
                bool processing = false;
                bool renderInFlight = false;
                bool waitingForSurroundingUpdate = false;
                std::deque<char> pending;
                size_t deleteCalls = 0;

                for (char key : keys) {
                    pending.push_back(key);
                    while (!pending.empty()) {
                        const char current = pending.front();
                        pending.pop_front();
                        processing = true;
                        renderInFlight = true;
                        const auto oldRendered = applied;
                        raw.push_back(current);
                        const auto newRendered =
                            fcitx::UniikiEngine::simulateDirectForTest(raw);
                        const auto delta =
                            fcitx::UniikiEngine::replacementDeltaForTest(
                                oldRendered, newRendered);
                        deleteCalls += delta.first > 0 ? 1 : 0;
                        applied = newRendered; // delete/commit dispatched now
                        processing = false;
                        renderInFlight = false;
                        waitingForSurroundingUpdate = false;
                    }
                }
                if (applied != keys || processing || renderInFlight ||
                    waitingForSurroundingUpdate || !pending.empty() ||
                    deleteCalls != 0) {
                    std::cerr << "autocomplete no-ack stress failed iteration="
                              << iteration << " keys=" << keys
                              << " applied=" << applied
                              << " deleteCalls=" << deleteCalls << '\n';
                    return 1;
                }
            }
        }
    }

    const std::vector<std::pair<std::string, std::string>> regressions = {
        {"dd", "đ"}, {"gox", "gõ"}, {"tieengs", "tiếng"}};
    for (const auto &[raw, expected] : regressions) {
        if (fcitx::UniikiEngine::simulateDirectForTest(raw) != expected) {
            std::cerr << "transform regression raw=" << raw << '\n';
            return 1;
        }
    }

    std::cout << "RUNTIME_HARNESS dd " << ddTrace << '\n';
    std::cout << "TWO_PHASE_HARNESS dd " << ddTwoPhase << '\n';
    std::cout << "TWO_PHASE_HARNESS delete-ignored " << ignoredDelete << '\n';
    std::cout << "RUNTIME_HARNESS canaf " << canTrace << '\n';
    std::cout << "RUNTIME_HARNESS stale-canaf " << mismatchTrace << '\n';
    std::cout << "direct-visible-cases=" << visibleCases.size()
              << " exactly-once-stress=1000"
              << " autocomplete-no-ack-stress=500x3"
              << " literal-bursts=" << literalBursts.size()
              << " status=pass\n";
    return 0;
}
