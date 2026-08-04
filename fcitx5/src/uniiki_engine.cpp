#include "uniiki_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <unordered_set>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace fcitx {

namespace {

constexpr int TONE_NONE = 0;
constexpr int TONE_SAC = 1;
constexpr int TONE_HUYEN = 2;
constexpr int TONE_HOI = 3;
constexpr int TONE_NGA = 4;
constexpr int TONE_NANG = 5;

bool isAsciiWordChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

uint64_t nextKeyEventId() {
    static uint64_t next = 0;
    return ++next;
}

uint64_t monotonicMicros() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string boolText(bool value) {
    return value ? "1" : "0";
}

const char *renderOperationText(UniikiState::RenderOperation operation) {
    switch (operation) {
    case UniikiState::RenderOperation::AppendLiteral:
        return "AppendLiteral";
    case UniikiState::RenderOperation::ReplaceSuffix:
        return "ReplaceSuffix";
    case UniikiState::RenderOperation::NoVisibleChange:
        return "NoVisibleChange";
    }
    return "NoVisibleChange";
}

class InternalCommitGuard {
public:
    explicit InternalCommitGuard(UniikiState &state) : state_(state) {
        state_.isInternalCommit = true;
    }
    ~InternalCommitGuard() { state_.isInternalCommit = false; }

private:
    UniikiState &state_;
};

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback)
        : callback_(std::move(callback)) {}
    ~ScopeExit() { callback_(); }

private:
    std::function<void()> callback_;
};

constexpr size_t TRACE_RING_CAPACITY = 1000;
constexpr uint64_t CONVERTER_SLOW_MICROS = 5000;
constexpr uint64_t KEY_HANDLER_SLOW_MICROS = 10000;
constexpr uint64_t RENDER_HANDLER_SLOW_MICROS = 10000;
constexpr size_t MAX_RAW_CONVERSION_LENGTH = 256;

std::string stateHashText(const UniikiState &state) {
    const auto value = std::hash<std::string>{}(
        state.rawBuffer + "\x1f" + state.displayText + "\x1f" +
        state.lastRenderedText + "\x1f" + std::to_string(state.revision) +
        "\x1f" + std::to_string(state.appliedRevision));
    return std::to_string(value);
}

void appendTrace(UniikiState &state, UniikiState::TraceRecord record) {
    if (state.traceRing.size() >= TRACE_RING_CAPACITY) {
        state.traceRing.pop_front();
    }
    state.traceRing.push_back(std::move(record));
}

void dumpTraceRing(const UniikiState &state, const char *reason) {
    FCITX_ERROR() << "UniikiTrace RING_DUMP_BEGIN"
                  << " reason=" << reason
                  << " records=" << state.traceRing.size();
    for (const auto &record : state.traceRing) {
        FCITX_ERROR() << "UniikiTrace RING_RECORD"
                      << " timestamp=" << record.timestampMicros
                      << " threadId=" << record.threadId
                      << " eventId=" << record.eventId
                      << " eventType=" << record.eventType
                      << " key=" << record.key
                      << " isPress=" << boolText(record.isPress)
                      << " isRelease=" << boolText(record.isRelease)
                      << " isRepeat=" << boolText(record.isRepeat)
                      << " contextId=" << record.contextId
                      << " contextGeneration=" << record.contextGeneration
                      << " functionStage=" << record.functionStage
                      << " rawBefore=" << record.rawBefore
                      << " rawAfter=" << record.rawAfter
                      << " desiredRendered=" << record.desiredRendered
                      << " appliedRendered=" << record.appliedRendered
                      << " desiredRevision=" << record.desiredRevision
                      << " appliedRevision=" << record.appliedRevision
                      << " surroundingRevision="
                      << record.surroundingRevision
                      << " queueLength=" << record.queueLength
                      << " processing=" << boolText(record.processing)
                      << " renderInFlight="
                      << boolText(record.renderInFlight)
                      << " waitingForSurrounding="
                      << boolText(record.waitingForSurrounding)
                      << " focusState=" << boolText(record.focusState)
                      << " transactionId=" << record.transactionId
                      << " cursor=" << record.cursor
                      << " anchor=" << record.anchor
                      << " parserStateHash=" << record.parserStateHash;
    }
    FCITX_ERROR() << "UniikiTrace RING_DUMP_END reason=" << reason;
}

bool isToneKey(char ch) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ch == 's' || ch == 'f' || ch == 'r' || ch == 'x' || ch == 'j' || ch == 'z';
}

bool isRawVowel(char ch) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' || ch == 'y';
}

bool hasUpperAfterFirst(const std::string &raw) {
    for (size_t i = 1; i < raw.size(); ++i) {
        if (std::isupper(static_cast<unsigned char>(raw[i])) &&
            std::tolower(static_cast<unsigned char>(raw[i])) != 'w') {
            return true;
        }
    }
    return false;
}

bool isDoubleWEscape(const std::string &raw) {
    return raw.size() == 2 &&
           std::all_of(raw.begin(), raw.end(), [](char ch) {
               return std::tolower(static_cast<unsigned char>(ch)) == 'w';
           });
}

bool hasInvalidWoContinuation(const std::string &rawLower) {
    static const std::vector<std::string> validCodas = {
        "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    size_t searchFrom = 0;
    while (true) {
        const auto wo = rawLower.find("wo", searchFrom);
        if (wo == std::string::npos) {
            return false;
        }
        const size_t continuation = wo + 2;
        if (continuation < rawLower.size()) {
            const char next = rawLower[continuation];
            if (!isRawVowel(next) && next != 'w') {
                const auto tail = rawLower.substr(
                    continuation + (isToneKey(next) ? 1 : 0));
                if (tail.empty() && isToneKey(next)) {
                    searchFrom = wo + 1;
                    continue;
                }
                const bool validCodaStart =
                    std::any_of(validCodas.begin(), validCodas.end(),
                                [&tail](const std::string &coda) {
                                    return coda.rfind(tail, 0) == 0 ||
                                           tail.rfind(coda, 0) == 0;
                                });
                if (!validCodaStart) {
                    return true;
                }
            }
        }
        searchFrom = wo + 1;
    }
}

bool isLikelyEnglishRawShape(const std::string &rawLower) {
    static const std::vector<std::string> englishInitialClusters = {
        "ww", "st", "fr", "fl", "dr", "br", "pr", "sl", "sm", "sn",
        "sp", "wr", "bl", "cl", "cr", "gl", "gr", "sc",
        "sk", "sq"
    };
    for (const auto &cluster : englishInitialClusters) {
        if (rawLower.rfind(cluster, 0) == 0) {
            return true;
        }
    }
    const auto medialDD = rawLower.find("dd", 1);
    if (medialDD != std::string::npos && rawLower.size() > 5 &&
        rawLower.front() != 'd') {
        bool hasVietnameseTransformBeforeDD = false;
        bool vowelSeen = false;
        for (size_t i = 0; i < medialDD; ++i) {
            const char ch = rawLower[i];
            if (isRawVowel(ch)) {
                vowelSeen = true;
            } else if (vowelSeen && (ch == 'w' || isToneKey(ch))) {
                hasVietnameseTransformBeforeDD = true;
                break;
            }
        }
        if (!hasVietnameseTransformBeforeDD) {
            return true;
        }
    }
    static const std::unordered_set<std::string> medialClusters = {
        "sk", "sp", "rd", "rs", "rl",
        "xt", "xl", "xy", "cl", "ts",
    };
    static const std::unordered_set<std::string> vowelSequences = {
        "a", "e", "i", "o", "u", "y", "aa", "ee", "oo", "ai", "ao", "au", "ay", "eo", "eu",
        "ia", "ie", "ieu", "iu", "oa", "oai", "oao", "oay", "oe", "oeo",
        "oi", "ua", "uai", "uo", "uoi", "ui", "uu", "uy", "uya", "uyu",
        "uye", "uyeu", "ye",
        "yeu",
    };
    for (size_t i = 0; i + 1 < rawLower.size(); ++i) {
        if (!isToneKey(rawLower[i])) {
            continue;
        }
        const char next = rawLower[i + 1];
        const bool nextCanStartCoda =
            next == 'c' || next == 'm' || next == 'n' ||
            next == 'p' || next == 't';
        if (medialClusters.count(rawLower.substr(i, 2)) != 0 &&
            !isToneKey(next) && !nextCanStartCoda) {
            const auto prefix = rawLower.substr(0, i);
            const bool hasStrongTelexNucleus =
                prefix.find("aa") != std::string::npos ||
                prefix.find("ee") != std::string::npos ||
                prefix.find("oo") != std::string::npos ||
                prefix.find('w') != std::string::npos;
            const bool repeatedEscape =
                i > 0 && rawLower[i - 1] == rawLower[i];
            if (!repeatedEscape &&
                (rawLower.substr(i, 2) != "st" ||
                 !hasStrongTelexNucleus)) {
                return true;
            }
        }
        if (i > 0 && isRawVowel(rawLower[i - 1]) &&
            isRawVowel(rawLower[i + 1])) {
            std::string vowels;
            for (char ch : rawLower) {
                if (isRawVowel(ch)) {
                    vowels.push_back(ch);
                }
            }
            std::string collapsedVowels;
            for (char vowel : vowels) {
                if (!collapsedVowels.empty() &&
                    collapsedVowels.back() == vowel &&
                    (vowel == 'a' || vowel == 'e' || vowel == 'o')) {
                    continue;
                }
                collapsedVowels.push_back(vowel);
            }
            if (vowelSequences.count(vowels) == 0 &&
                vowelSequences.count(collapsedVowels) == 0) {
                return true;
            }
            if (std::any_of(rawLower.begin() + i + 1, rawLower.end(),
                            [trigger = rawLower[i]](char ch) {
                                return isToneKey(ch) && ch != trigger;
                            })) {
                return true;
            }
        }
    }
    return false;
}

bool startsWith(const std::string &text, const std::string &prefix) {
    return text.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string &text, const std::string &suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string textBeforeCursor(const SurroundingText &surroundingText) {
    const auto &text = surroundingText.text();
    auto cursor = surroundingText.cursor();
    if (cursor == 0) {
        return "";
    }
    auto charLength = utf8::length(text);
    if (cursor > charLength) {
        return "";
    }
    auto byteLength = static_cast<size_t>(utf8::ncharByteLength(text.begin(), cursor));
    return text.substr(0, byteLength);
}

std::string textInCodePointRange(const std::string &text,
                                 unsigned int start,
                                 unsigned int end) {
    const auto length = static_cast<unsigned int>(utf8::length(text));
    if (start > end || end > length) {
        return "";
    }
    const auto startByte = static_cast<size_t>(
        utf8::ncharByteLength(text.begin(), start));
    const auto endByte = static_cast<size_t>(
        utf8::ncharByteLength(text.begin(), end));
    return text.substr(startByte, endByte - startByte);
}

struct ReplacementSelectionState {
    bool hasSelection = false;
    bool autocompleteSelection = false;
    bool safe = true;
    unsigned int length = 0;
};

ReplacementSelectionState replacementSelectionState(
    unsigned int cursor, unsigned int anchor,
    const std::string &beforeCursor, const std::string &oldRendered) {
    ReplacementSelectionState result;
    result.hasSelection = cursor != anchor;
    result.autocompleteSelection =
        result.hasSelection && anchor > cursor &&
        endsWith(beforeCursor, oldRendered);
    result.safe = !result.hasSelection || result.autocompleteSelection;
    result.length = cursor > anchor ? cursor - anchor : anchor - cursor;
    return result;
}

std::string vowelWithTone(char base, int mark, int tone, bool upper);

std::string normalizeNFC(const std::string &text) {
    // The engine emits only Vietnamese Latin text. Compose the complete set of
    // combining marks accepted by that alphabet so NFC and NFD surrounding
    // text follow the same replacement/backspace path without a new runtime
    // dependency.
    std::string result;
    auto iter = text.begin();
    while (iter != text.end()) {
        const auto next = utf8::nextNChar(iter, 1);
        const auto codePoint = utf8::getChar(iter, text.end());
        const bool asciiVowel =
            codePoint == 'a' || codePoint == 'A' ||
            codePoint == 'e' || codePoint == 'E' ||
            codePoint == 'i' || codePoint == 'I' ||
            codePoint == 'o' || codePoint == 'O' ||
            codePoint == 'u' || codePoint == 'U' ||
            codePoint == 'y' || codePoint == 'Y';
        if (!asciiVowel) {
            result.append(iter, next);
            iter = next;
            continue;
        }

        int mark = 0;
        int tone = TONE_NONE;
        bool consumedMark = false;
        auto clusterEnd = next;
        while (clusterEnd != text.end()) {
            const auto combining = utf8::getChar(clusterEnd, text.end());
            int nextMark = mark;
            int nextTone = tone;
            if (combining == 0x0302) {
                nextMark = 1; // circumflex
            } else if (combining == 0x0306) {
                nextMark = 2; // breve
            } else if (combining == 0x031B) {
                nextMark = 3; // horn
            } else if (combining == 0x0301) {
                nextTone = TONE_SAC;
            } else if (combining == 0x0300) {
                nextTone = TONE_HUYEN;
            } else if (combining == 0x0309) {
                nextTone = TONE_HOI;
            } else if (combining == 0x0303) {
                nextTone = TONE_NGA;
            } else if (combining == 0x0323) {
                nextTone = TONE_NANG;
            } else {
                break;
            }
            mark = nextMark;
            tone = nextTone;
            consumedMark = true;
            clusterEnd = utf8::nextNChar(clusterEnd, 1);
        }
        if (consumedMark) {
            const char base = static_cast<char>(
                std::tolower(static_cast<unsigned char>(codePoint)));
            result += vowelWithTone(
                base, mark, tone,
                std::isupper(static_cast<unsigned char>(codePoint)));
            iter = clusterEnd;
        } else {
            result.append(iter, next);
            iter = next;
        }
    }
    return result;
}

std::vector<std::string> splitCodePoints(const std::string &text) {
    std::vector<std::string> chars;
    auto normalized = normalizeNFC(text);
    auto iter = normalized.begin();
    while (iter != normalized.end()) {
        auto next = utf8::nextNChar(iter, 1);
        chars.emplace_back(iter, next);
        iter = next;
    }
    return chars;
}

std::pair<unsigned int, std::string> replacementDelta(const std::string &oldText,
                                                      const std::string &newText) {
    auto oldChars = splitCodePoints(oldText);
    auto newChars = splitCodePoints(newText);
    size_t common = 0;
    while (common < oldChars.size() && common < newChars.size() &&
           oldChars[common] == newChars[common]) {
        ++common;
    }

    std::string insertText;
    for (size_t i = common; i < newChars.size(); ++i) {
        insertText += newChars[i];
    }
    return {static_cast<unsigned int>(oldChars.size() - common), insertText};
}

bool shouldProtectRawWord(const std::string &raw, const std::string &rawLower) {
    if (raw.empty()) {
        return false;
    }
    static const std::vector<std::string> protectedWords = {
        "python", "javascript", "typescript", "telex", "vni", "terminal",
        "code", "chrome", "firefox", "libreoffice", "telegram", "discord",
        "zalo", "web", "latinh", "password", "desktop", "pre", "windows",
        "google", "version", "linux", "raw", "test", "best", "room",
    };
    static const std::vector<std::string> protectedRoots = {
        "tele", "type", "java", "chrome", "fire", "libre",
        "discord", "zalo", "web", "latin", "terminal", "pre",
    };

    if (std::find(protectedWords.begin(), protectedWords.end(), rawLower) != protectedWords.end()) {
        return true;
    }

    bool asciiOnly = std::all_of(raw.begin(), raw.end(), [](char ch) {
        return static_cast<unsigned char>(ch) < 128;
    });
    if (!asciiOnly) {
        return false;
    }

    if (rawLower == "w") {
        return false;
    }

    bool matchesProtectedPrefix =
        std::any_of(protectedWords.begin(), protectedWords.end(),
                    [&rawLower](const std::string &word) { return startsWith(word, rawLower); });
    if (matchesProtectedPrefix &&
        (rawLower.size() >= 4 ||
         std::isupper(static_cast<unsigned char>(raw.front())))) {
        return true;
    }

    if (!raw.empty() && std::tolower(static_cast<unsigned char>(raw.front())) == 'w') {
        return false;
    }

    return std::any_of(protectedRoots.begin(), protectedRoots.end(),
                       [&rawLower](const std::string &root) { return startsWith(rawLower, root); });
}

int toneFromKey(char ch) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (ch == 's') {
        return TONE_SAC;
    }
    if (ch == 'f') {
        return TONE_HUYEN;
    }
    if (ch == 'r') {
        return TONE_HOI;
    }
    if (ch == 'x') {
        return TONE_NGA;
    }
    if (ch == 'j') {
        return TONE_NANG;
    }
    return TONE_NONE;
}

struct EscapedLiteral {
    char value;
    size_t firstTriggerRawIndex;
    size_t secondTriggerRawIndex;
};

struct ToneActionHistory {
    int activeTone = TONE_NONE;
    char activeKey = 0;
    size_t activeTriggerRawIndex = 0;
    std::vector<size_t> consumedModifierIndexes;
    std::optional<EscapedLiteral> escapedLiteral;
};

enum class RawKeyOwnership {
    LiteralLetter,
    LetterTransformTrigger,
    ToneModifier,
    EscapedLiteralMember,
};

struct ConversionToken {
    size_t rawIndex;
    char value;
    RawKeyOwnership ownership;
};

struct ToneLexResult {
    std::string baseLetters;
    std::vector<ConversionToken> tokens;
    ToneActionHistory history;
    std::vector<EscapedLiteral> escapedLiterals;
    std::vector<size_t> escapedLiteralIndexes;
};

const char *toneName(int tone) {
    switch (tone) {
    case TONE_SAC:
        return "sac";
    case TONE_HUYEN:
        return "huyen";
    case TONE_HOI:
        return "hoi";
    case TONE_NGA:
        return "nga";
    case TONE_NANG:
        return "nang";
    default:
        return "none";
    }
}

static bool isValidVietnameseInitialStr(const std::string &raw, size_t keyIndex) {
    if (raw.empty() || keyIndex >= raw.size()) return true;
    static const std::unordered_set<std::string> validInitials = {
        "", "b", "c", "ch", "d", "g", "gh", "gi", "h", "k", "kh", "l", "m",
        "n", "ng", "ngh", "nh", "p", "ph", "q", "qu", "r", "s", "t", "th",
        "tr", "v", "x"
    };
    size_t firstVowelPos = std::string::npos;
    for (size_t i = 0; i < keyIndex; ++i) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') {
            firstVowelPos = i;
            break;
        }
        if (c == 'w') {
            std::string prefixBeforeW;
            for (size_t k = 0; k < i; ++k) {
                prefixBeforeW.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[k]))));
            }
            std::string collapsedPrefix;
            for (size_t k = 0; k < prefixBeforeW.size(); ++k) {
                if (k + 1 < prefixBeforeW.size() && prefixBeforeW[k] == prefixBeforeW[k + 1]) {
                    collapsedPrefix.push_back(prefixBeforeW[k]);
                    ++k;
                } else {
                    collapsedPrefix.push_back(prefixBeforeW[k]);
                }
            }
            if (!collapsedPrefix.empty() && validInitials.count(collapsedPrefix) > 0) {
                firstVowelPos = i;
                break;
            }
        }
    }
    if (firstVowelPos == std::string::npos) return true;
    std::string initial;
    for (size_t i = 0; i < firstVowelPos; ++i) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));
        initial.push_back(c);
    }

    std::string collapsed;
    for (size_t i = 0; i < initial.size(); ++i) {
        if (i + 1 < initial.size() && initial[i] == initial[i + 1]) {
            collapsed.push_back(initial[i]);
            ++i;
        } else {
            collapsed.push_back(initial[i]);
        }
    }

    return validInitials.count(collapsed) > 0;
}

ToneLexResult lexAndReduceToneActions(const std::string &raw) {
    ToneLexResult result;
    bool vowelSeen = false;
    char escapeSequenceKey = 0;
    size_t lastEscapeRawIndex = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const char key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(raw[i])));
        const bool establishesVowel = isRawVowel(key) ||
            (key == 'w' && !(i == 0 && i + 1 < raw.size() &&
                             std::tolower(static_cast<unsigned char>(raw[1])) == 'w'));
        if (vowelSeen && isToneKey(key) &&
            escapeSequenceKey == key && i == lastEscapeRawIndex + 1) {
            result.baseLetters.push_back(raw[i]);
            result.tokens.push_back(
                {i, raw[i], RawKeyOwnership::EscapedLiteralMember});
            result.escapedLiteralIndexes.push_back(i);
            lastEscapeRawIndex = i;
            continue;
        }
        escapeSequenceKey = 0;
        if (!vowelSeen || !isToneKey(key) || !isValidVietnameseInitialStr(raw, i)) {
            result.baseLetters.push_back(raw[i]);
            result.tokens.push_back(
                {i, raw[i], RawKeyOwnership::LiteralLetter});
            if (establishesVowel) {
                vowelSeen = true;
            }
            continue;
        }

        auto &history = result.history;
        if (history.activeTone != TONE_NONE &&
            history.activeKey == key) {
            const EscapedLiteral escaped{
                raw[i], i, i};
            history.escapedLiteral = escaped;
            result.escapedLiterals.push_back(escaped);
            result.tokens.push_back(
                {i, raw[i], RawKeyOwnership::EscapedLiteralMember});
            result.escapedLiteralIndexes.push_back(i);
            result.baseLetters.push_back(raw[i]);
            history.activeTone = TONE_NONE;
            history.activeKey = 0;
            escapeSequenceKey = key;
            lastEscapeRawIndex = i;
            continue;
        }
        result.tokens.push_back(
            {i, raw[i], RawKeyOwnership::ToneModifier});
        history.activeTone = toneFromKey(key);
        history.activeKey = key;
        history.activeTriggerRawIndex = i;
        history.consumedModifierIndexes.push_back(i);
        history.escapedLiteral.reset();
    }
    return result;
}

std::string tokenOwnershipText(const std::vector<ConversionToken> &tokens) {
    std::string text = "[";
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            text += ",";
        }
        switch (tokens[i].ownership) {
        case RawKeyOwnership::LiteralLetter:
            text += "Literal(";
            break;
        case RawKeyOwnership::LetterTransformTrigger:
            text += "LetterTrigger(";
            break;
        case RawKeyOwnership::ToneModifier:
            text += "ToneModifier(";
            break;
        case RawKeyOwnership::EscapedLiteralMember:
            text += "EscapedLiteral(";
            break;
        }
        text.push_back(tokens[i].value);
        text += "@" + std::to_string(tokens[i].rawIndex) + ")";
    }
    text += "]";
    return text;
}

std::string rawIndexesText(const std::vector<size_t> &indexes) {
    std::string text = "[";
    for (size_t i = 0; i < indexes.size(); ++i) {
        if (i > 0) {
            text += ",";
        }
        text += std::to_string(indexes[i]);
    }
    text += "]";
    return text;
}

struct ConversionDebugTrace {
    std::string rawBuffer;
    std::string tokens;
    std::vector<size_t> consumedModifierIndexes;
    std::string parsedOnset;
    std::string parsedVowelNucleus;
    std::string parsedCoda;
    int activeTone = TONE_NONE;
    std::string toneTargetIndex = "none";
    std::string cellsBeforeTone;
    std::string candidate;
    bool validVietnamese = false;
    std::string rendered;
    std::string fallbackReason;
};

thread_local ConversionDebugTrace conversionDebugTrace;

std::string vowelWithTone(char base, int mark, int tone, bool upper = false) {
    static const std::unordered_map<std::string, std::vector<std::string>> lowerTable = {
        {"a0", {"a", "á", "à", "ả", "ã", "ạ"}},
        {"a1", {"â", "ấ", "ầ", "ẩ", "ẫ", "ậ"}},
        {"a2", {"ă", "ắ", "ằ", "ẳ", "ẵ", "ặ"}},
        {"e0", {"e", "é", "è", "ẻ", "ẽ", "ẹ"}},
        {"e1", {"ê", "ế", "ề", "ể", "ễ", "ệ"}},
        {"i0", {"i", "í", "ì", "ỉ", "ĩ", "ị"}},
        {"o0", {"o", "ó", "ò", "ỏ", "õ", "ọ"}},
        {"o1", {"ô", "ố", "ồ", "ổ", "ỗ", "ộ"}},
        {"o3", {"ơ", "ớ", "ờ", "ở", "ỡ", "ợ"}},
        {"u0", {"u", "ú", "ù", "ủ", "ũ", "ụ"}},
        {"u3", {"ư", "ứ", "ừ", "ử", "ữ", "ự"}},
        {"y0", {"y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"}},
    };
    static const std::unordered_map<std::string, std::vector<std::string>> upperTable = {
        {"a0", {"A", "Á", "À", "Ả", "Ã", "Ạ"}},
        {"a1", {"Â", "Ấ", "Ầ", "Ẩ", "Ẫ", "Ậ"}},
        {"a2", {"Ă", "Ắ", "Ằ", "Ẳ", "Ẵ", "Ặ"}},
        {"e0", {"E", "É", "È", "Ẻ", "Ẽ", "Ẹ"}},
        {"e1", {"Ê", "Ế", "Ề", "Ể", "Ễ", "Ệ"}},
        {"i0", {"I", "Í", "Ì", "Ỉ", "Ĩ", "Ị"}},
        {"o0", {"O", "Ó", "Ò", "Ỏ", "Õ", "Ọ"}},
        {"o1", {"Ô", "Ố", "Ồ", "Ổ", "Ỗ", "Ộ"}},
        {"o3", {"Ơ", "Ớ", "Ờ", "Ở", "Ỡ", "Ợ"}},
        {"u0", {"U", "Ú", "Ù", "Ủ", "Ũ", "Ụ"}},
        {"u3", {"Ư", "Ứ", "Ừ", "Ử", "Ữ", "Ự"}},
        {"y0", {"Y", "Ý", "Ỳ", "Ỷ", "Ỹ", "Ỵ"}},
    };
    const auto &table = upper ? upperTable : lowerTable;
    auto it = table.find(std::string{base} + std::to_string(mark));
    if (it == table.end()) {
        char text = upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(base))) : base;
        return std::string(1, text);
    }
    return it->second[tone];
}

struct Cell {
    enum class Kind {
        Raw,
        Vowel,
        StrokeD,
    };

    Kind kind = Kind::Raw;
    char raw = 0;
    char base = 0;
    int mark = 0;
    int tone = TONE_NONE;
    bool upper = false;
};

Cell rawCell(char ch) {
    Cell cell;
    cell.kind = Cell::Kind::Raw;
    cell.raw = ch;
    return cell;
}

Cell vowelCell(char base, int mark = 0, int tone = TONE_NONE, bool upper = false) {
    Cell cell;
    cell.kind = Cell::Kind::Vowel;
    cell.base = base;
    cell.mark = mark;
    cell.tone = tone;
    cell.upper = upper;
    return cell;
}

Cell strokeDCell(bool upper = false) {
    Cell cell;
    cell.kind = Cell::Kind::StrokeD;
    cell.upper = upper;
    return cell;
}

bool isVowelCell(const Cell &cell) {
    return cell.kind == Cell::Kind::Vowel;
}

std::string cellText(const Cell &cell) {
    if (cell.kind == Cell::Kind::StrokeD) {
        return cell.upper ? "Đ" : "đ";
    }
    if (cell.kind == Cell::Kind::Vowel) {
        return vowelWithTone(cell.base, cell.mark, cell.tone, cell.upper);
    }
    return std::string(1, cell.raw);
}

std::string cellsText(const std::vector<Cell> &cells) {
    std::string text;
    for (const auto &cell : cells) {
        text += cellText(cell);
    }
    return text;
}

bool hasVowel(const std::vector<Cell> &cells) {
    return std::any_of(cells.begin(), cells.end(), isVowelCell);
}

std::string lowerRaw(const std::string &raw) {
    std::string lower;
    lower.reserve(raw.size());
    for (char ch : raw) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower;
}

struct RawSegment {
    size_t start;
    size_t end;
};

enum class ShapeType {
    Circumflex,
};

struct ShapeAction {
    char triggerKey = 0;
    size_t triggerRawIndex = 0;
    size_t targetVowelRawIndex = 0;
    ShapeType type = ShapeType::Circumflex;
    bool lateAfterCoda = false;
};

struct ParsedCandidate {
    std::string onset;
    std::string vowelNucleus;
    std::string coda;
    std::vector<ShapeAction> shapeActions;
    std::string literalSuffix;
    bool validVietnamese = false;
};

struct DTransformState {
    bool hasTarget = false;
    size_t targetRawIndex = 0;
    bool transformed = false;
    size_t transformTriggerRawIndex = 0;
    bool cancelled = false;
    size_t cancellationTriggerRawIndex = 0;
};

bool isCodaPrefix(const std::string &text) {
    static const std::vector<std::string> codas = {
        "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    return std::any_of(codas.begin(), codas.end(), [&text](const std::string &coda) {
        return startsWith(coda, text);
    });
}

bool isCompleteCoda(const std::string &text) {
    static const std::unordered_set<std::string> codas = {
        "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    return codas.find(text) != codas.end();
}

bool isKnownNucleusBaseSequence(const std::string &text) {
    static const std::unordered_set<std::string> nuclei = {
        "a", "e", "i", "o", "u", "y", "ai", "ao", "au", "ay", "au", "ay",
        "eo", "eu", "ia", "ie", "ieu", "iu", "oa", "oai", "oao", "oay",
        "oe", "oeo", "oi", "ua", "uai", "ue", "ueu", "ui", "uo", "uoi",
        "uu", "uy", "uya", "uyu", "uye", "uyeu", "ye", "yeu",
    };
    return nuclei.find(text) != nuclei.end();
}

ParsedCandidate parseShapeCandidate(const std::string &raw,
                                    size_t rawOffset = 0) {
    ParsedCandidate parsed;
    auto toneLex = lexAndReduceToneActions(raw);
    const auto &letters = toneLex.baseLetters;
    std::vector<size_t> letterToRaw;
    letterToRaw.reserve(letters.size());
    for (const auto &token : toneLex.tokens) {
        if (token.ownership != RawKeyOwnership::ToneModifier) {
            letterToRaw.push_back(rawOffset + token.rawIndex);
        }
    }
    bool vowelSeen = false;
    std::vector<std::pair<char, size_t>> nucleus;

    for (size_t i = 0; i < letters.size(); ++i) {
        const char key = static_cast<char>(std::tolower(
            static_cast<unsigned char>(letters[i])));
        if (isRawVowel(key)) {
            if (!parsed.coda.empty()) {
                auto target = std::find_if(
                    nucleus.rbegin(), nucleus.rend(),
                    [key](const auto &entry) { return entry.first == key; });
                if ((key == 'a' || key == 'e' || key == 'o') &&
                    isCompleteCoda(parsed.coda) && target != nucleus.rend() &&
                    i + 1 == letters.size()) {
                    parsed.shapeActions.push_back(
                        {letters[i], letterToRaw[i],
                         letterToRaw[target->second],
                         ShapeType::Circumflex, true});
                    continue;
                }
                parsed.literalSuffix = letters.substr(i);
                break;
            }

            if ((key == 'a' || key == 'e' || key == 'o') &&
                (i == 0 ||
                 static_cast<char>(std::tolower(static_cast<unsigned char>(
                     letters[i - 1]))) != key) &&
                i + 1 < letters.size() &&
                static_cast<char>(std::tolower(static_cast<unsigned char>(
                    letters[i + 1]))) == key) {
                // Three equal keys are the Telex escape and own no shape
                // action: aaa/eee/ooo must remain two literal letters.
                if (i + 2 >= letters.size() ||
                    static_cast<char>(std::tolower(static_cast<unsigned char>(
                        letters[i + 2]))) != key) {
                    parsed.shapeActions.push_back(
                        {letters[i + 1], letterToRaw[i + 1], letterToRaw[i],
                         ShapeType::Circumflex, false});
                }
            }
            if (key == 'a' || key == 'e' || key == 'o') {
                const auto target = std::find_if(
                    nucleus.rbegin(), nucleus.rend(),
                    [key](const auto &entry) { return entry.first == key; });
                const bool actionAlreadyPlanned = std::any_of(
                    parsed.shapeActions.begin(), parsed.shapeActions.end(),
                    [trigger = letterToRaw[i]](const ShapeAction &action) {
                        return action.triggerRawIndex == trigger;
                    });
                const std::string withCurrent = parsed.vowelNucleus + key;
                if (target != nucleus.rend() && !nucleus.empty() &&
                    nucleus.back().first != key && !actionAlreadyPlanned &&
                    !isKnownNucleusBaseSequence(lowerRaw(withCurrent))) {
                    // A repeated shape key may follow the rest of a nucleus
                    // (ddauaf -> đầu). It is a modifier only when keeping
                    // it would not itself form a known nucleus; thus oao stays
                    // three base vowels instead of becoming ôa.
                    parsed.shapeActions.push_back(
                        {letters[i], letterToRaw[i],
                         letterToRaw[target->second],
                         ShapeType::Circumflex, false});
                }
            }
            const bool currentKeyOwnedByShape = std::any_of(
                parsed.shapeActions.begin(), parsed.shapeActions.end(),
                [current = letterToRaw[i]](const ShapeAction &action) {
                    return action.triggerRawIndex == current;
                });
            if (currentKeyOwnedByShape) {
                // A modifier is an action token, never a second base vowel.
                // This also prevents a later action from targeting a key that
                // has already been consumed.
                continue;
            }
            vowelSeen = true;
            parsed.vowelNucleus.push_back(letters[i]);
            nucleus.push_back({key, i});
            continue;
        }

        if (!std::isalpha(static_cast<unsigned char>(key))) {
            parsed.literalSuffix = letters.substr(i);
            break;
        }
        if (!vowelSeen) {
            parsed.onset.push_back(letters[i]);
            continue;
        }
        const auto nextCoda = parsed.coda + key;
        if (!isCodaPrefix(nextCoda)) {
            parsed.literalSuffix = letters.substr(i);
            break;
        }
        parsed.coda = nextCoda;
    }
    return parsed;
}

std::vector<RawSegment> splitRawSegments(const std::string &raw) {
    if (raw.empty()) {
        return {};
    }

    std::vector<RawSegment> segments;
    size_t segmentStart = 0;
    bool hasVowelInSegment = false;
    std::string coda;
    std::string nucleusBases;

    auto finishSegment = [&](size_t end) {
        if (end > segmentStart) {
            segments.push_back({segmentStart, end});
        }
        segmentStart = end;
        hasVowelInSegment = false;
        coda.clear();
        nucleusBases.clear();
    };

    for (size_t i = 0; i < raw.size(); ++i) {
        char lower =
            static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));

        if (lower == 'w') {
            if (!hasVowelInSegment) {
                hasVowelInSegment = true;
            }
            continue;
        }
        if (isToneKey(lower) && hasVowelInSegment) {
            continue;
        }
        if (isRawVowel(lower)) {
            if (hasVowelInSegment && !coda.empty()) {
                const bool canBeLateShapeModifier =
                    (lower == 'a' || lower == 'e' || lower == 'o') &&
                    isCompleteCoda(coda) &&
                    nucleusBases.find(lower) != std::string::npos &&
                    std::all_of(
                        raw.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        raw.end(), [](char suffixKey) {
                            return isToneKey(static_cast<char>(std::tolower(
                                static_cast<unsigned char>(suffixKey))));
                        });
                if (canBeLateShapeModifier) {
                    // Free-order Telex: the repeated a/e/o may modify the
                    // matching nucleus through a complete coda (cana -> cân).
                    // Candidate validation later decides modifier vs literal.
                    continue;
                }
                finishSegment(i);
            }
            hasVowelInSegment = true;
            coda.clear();
            nucleusBases.push_back(lower);
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(lower))) {
            finishSegment(i);
            segmentStart = i + 1;
            continue;
        }
        if (!hasVowelInSegment) {
            continue;
        }

        auto nextCoda = coda + lower;
        if (!isCodaPrefix(nextCoda)) {
            finishSegment(i);
            continue;
        }
        coda = nextCoda;
    }

    if (segmentStart < raw.size()) {
        segments.push_back({segmentStart, raw.size()});
    }
    return segments;
}

DTransformState traceDTransform(const std::string &raw) {
    DTransformState state;

    // The first onset d owns the modifier transaction for the whole active
    // word. Later segment parsing must not replace it with a newer target.
    if (!raw.empty() &&
        std::tolower(static_cast<unsigned char>(raw.front())) == 'd') {
        state.hasTarget = true;
        state.targetRawIndex = 0;
    }
    if (!state.hasTarget) {
        return state;
    }

    for (size_t i = state.targetRawIndex + 1; i < raw.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(raw[i])) != 'd') {
            continue;
        }
        if (!state.transformed && !state.cancelled) {
            state.transformed = true;
            state.transformTriggerRawIndex = i;
        } else if (state.transformed) {
            state.transformed = false;
            state.cancelled = true;
            state.cancellationTriggerRawIndex = i;
        }
    }
    return state;
}

bool hasVowelInRawRange(const std::string &raw, const RawSegment &segment) {
    for (size_t i = segment.start; i < segment.end; ++i) {
        if (isRawVowel(raw[i])) {
            return true;
        }
    }
    return false;
}

bool isStructurallyCompleteRawWord(const std::string &raw) {
    const auto segments = splitRawSegments(raw);
    return !segments.empty() &&
           std::all_of(segments.begin(), segments.end(),
                       [&raw](const RawSegment &segment) {
                           return hasVowelInRawRange(raw, segment);
                       });
}

bool containsNonAscii(const std::string &text) {
    return std::any_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) >= 128;
    });
}

struct SegmentTrace {
    size_t start = 0;
    std::string raw;
    std::string onset;
    std::string vowel;
    std::string coda;
    std::string candidate;
    bool applied = false;
    bool undone = false;
    std::string triggerKey;
    std::string triggerRawIndex = "none";
    bool stillValid = false;
    std::string consumedRawIndexes = "[]";
    std::string fallbackReason;
};

SegmentTrace traceActiveSegment(const std::string &raw, const std::string &rendered) {
    SegmentTrace trace;
    auto segments = splitRawSegments(raw);
    if (segments.empty()) {
        return trace;
    }

    const auto &active = segments.back();
    trace.start = active.start;
    trace.raw = raw.substr(active.start, active.end - active.start);
    bool seenVowel = false;
    size_t parseStart = 0;
    const auto traceLower = lowerRaw(trace.raw);
    bool giOnset = false;
    if (startsWith(traceLower, "gi")) {
        trace.onset = trace.raw.substr(0, 2);
        parseStart = 2;
        giOnset = true;
    } else if (startsWith(traceLower, "qu")) {
        trace.onset = trace.raw.substr(0, 2);
        parseStart = 2;
    }
    for (size_t rawIndex = parseStart; rawIndex < trace.raw.size(); ++rawIndex) {
        char ch = trace.raw[rawIndex];
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower == 'w') {
            if (giOnset && !seenVowel) {
                trace.vowel += "ư";
                seenVowel = true;
            }
            continue;
        }
        if (seenVowel && isToneKey(lower)) {
            continue;
        }
        if (isRawVowel(lower)) {
            seenVowel = true;
            trace.vowel.push_back(ch);
        } else if (!seenVowel) {
            trace.onset.push_back(ch);
        } else {
            trace.coda.push_back(ch);
        }
    }

    auto lower = lowerRaw(trace.raw);
    size_t leadingD = 0;
    while (leadingD < lower.size() && lower[leadingD] == 'd') {
        ++leadingD;
    }
    if (leadingD > 0 && leadingD == lower.size()) {
        trace.candidate = "dd->đ";
        trace.applied = leadingD >= 2 && leadingD % 2 == 0 && segments.size() == 1;
        trace.undone = leadingD >= 3 || (segments.size() > 1 && leadingD >= 2);
    } else if (lower.size() >= 2 && lower.back() == lower[lower.size() - 2] &&
               (lower.back() == 'a' || lower.back() == 'e' || lower.back() == 'o')) {
        trace.candidate = std::string(2, lower.back());
        trace.undone = lower.size() >= 3 && lower[lower.size() - 3] == lower.back();
        trace.applied = !trace.undone;
    } else if (!lower.empty() && lower.back() == 'w') {
        trace.candidate = "w";
        trace.undone = lower.size() >= 2 && lower[lower.size() - 2] == 'w';
        trace.applied = !trace.undone && rendered != raw;
    } else if (lower.size() >= 2 && isToneKey(lower.back())) {
        trace.candidate = std::string(1, lower.back());
        trace.undone = lower[lower.size() - 2] == lower.back();
        trace.applied = !trace.undone && rendered != raw;
    }

    const auto dState = traceDTransform(raw);
    if (dState.hasTarget &&
        (dState.transformed || dState.cancelled)) {
        trace.triggerKey = "d";
        trace.triggerRawIndex =
            std::to_string(dState.transformTriggerRawIndex);
        trace.applied = rendered != raw;
        trace.stillValid = dState.transformed && trace.applied;
        if (trace.applied) {
            std::vector<size_t> dIndexes;
            for (size_t i = dState.targetRawIndex; i < raw.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(raw[i])) == 'd') {
                    dIndexes.push_back(i);
                }
            }
            std::vector<size_t> consumed;
            if (!dState.cancelled) {
                consumed.push_back(dState.transformTriggerRawIndex);
            } else {
                std::string resolved;
                size_t ordinal = 0;
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(raw[i])) != 'd') {
                        resolved.push_back(raw[i]);
                        continue;
                    }
                    ++ordinal;
                    if (ordinal == 1 || ordinal == 3) {
                        resolved.push_back(raw[i]);
                    }
                }
                const bool adjacent =
                    dIndexes.size() >= 2 &&
                    dIndexes[1] == dState.targetRawIndex + 1;
                const bool atomicFallback =
                    !adjacent && !isStructurallyCompleteRawWord(resolved);
                if (!atomicFallback && dIndexes.size() >= 2) {
                    consumed.push_back(dIndexes[1]);
                }
                for (size_t ordinalIndex = 3;
                     ordinalIndex < dIndexes.size(); ++ordinalIndex) {
                    consumed.push_back(dIndexes[ordinalIndex]);
                }
            }
            trace.consumedRawIndexes = "[";
            for (size_t i = 0; i < consumed.size(); ++i) {
                if (i > 0) {
                    trace.consumedRawIndexes += ",";
                }
                trace.consumedRawIndexes += std::to_string(consumed[i]);
            }
            trace.consumedRawIndexes += "]";
        }
    } else {
        bool vowelSeen = false;
        bool literalToneBeforeNucleus = false;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (isRawVowel(raw[i])) {
                vowelSeen = true;
            } else if (vowelSeen && isToneKey(raw[i])) {
                trace.triggerKey = std::string(1, raw[i]);
                trace.triggerRawIndex = std::to_string(i);
            } else if (!vowelSeen && i > 0 && isToneKey(raw[i])) {
                literalToneBeforeNucleus = true;
            }
        }
        if (!trace.triggerKey.empty()) {
            trace.applied = rendered != raw;
            trace.stillValid = trace.applied;
            if (trace.applied) {
                trace.consumedRawIndexes =
                    "[" + trace.triggerRawIndex + "]";
            }
        } else if (literalToneBeforeNucleus) {
            trace.fallbackReason = "tone-key-before-nucleus-kept-literal";
        }
    }
    if (!trace.triggerKey.empty() && rendered == raw) {
        trace.fallbackReason = "invalid-candidate-restored-trigger";
        trace.applied = false;
        trace.stillValid = false;
        trace.consumedRawIndexes = "[]";
    }
    return trace;
}

bool endsWithBases(const std::vector<Cell> &cells, const std::string &bases) {
    if (cells.size() < bases.size()) {
        return false;
    }
    size_t offset = cells.size() - bases.size();
    for (size_t i = 0; i < bases.size(); ++i) {
        const auto &cell = cells[offset + i];
        if (!isVowelCell(cell) || cell.base != bases[i]) {
            return false;
        }
    }
    return true;
}

bool isRawConsonant(const Cell &cell) {
    if (cell.kind != Cell::Kind::Raw) {
        return false;
    }
    return std::isalpha(static_cast<unsigned char>(cell.raw)) && !isRawVowel(cell.raw);
}

char lowerAscii(char ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string lowerRawCellText(const Cell &cell) {
    if (cell.kind == Cell::Kind::StrokeD) {
        return "d";
    }
    if (cell.kind != Cell::Kind::Raw || !std::isalpha(static_cast<unsigned char>(cell.raw))) {
        return "";
    }
    return std::string(1, lowerAscii(cell.raw));
}

bool isValidVietnameseSyllableCells(const std::vector<Cell> &cells) {
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        return false;
    }

    static const std::unordered_set<std::string> initials = {
        "",   "b",  "c",  "ch", "d",  "g",  "gh", "gi", "h",  "k",
        "kh", "l",  "m",  "n",  "ng", "ngh", "nh", "p",  "ph", "q",
        "qu", "r",  "s",  "t",  "th", "tr", "v",  "x",
    };
    static const std::unordered_set<std::string> codas = {
        "", "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    static const std::unordered_set<std::string> nuclei = {
        "a0", "a1", "a2", "e0", "e1", "i0", "o0", "o1", "o3", "u0", "u3", "y0",
        "a0i0", "a0o0", "a0u0", "a0y0", "a1u0", "a1y0", "e0o0", "e1u0",
        "i0a0", "i0e1", "i0e1u0", "i0u0", "o0a0", "o0a0i0", "o0a0o0",
        "o0a0y0", "o0a2", "o0e0", "o0e0o0", "o0i0", "o1i0", "o3i0",
        "u0a0", "u0a0i0", "u0a1", "u0e1", "u0e1u0", "u0o1", "u0o1i0", "u0i0",
        "u0u0", "u0y0", "u0y0a0", "u0y0u0", "u0y0e1", "u0y0e1u0",
        "u3a0", "u3i0", "u3o3", "u3o3i0", "u3u0",
        "y0e1", "y0e1u0",
    };

    size_t firstVowel = vowels.front();
    size_t lastVowel = vowels.back();
    std::string initial;
    for (size_t i = 0; i < firstVowel; ++i) {
        auto text = lowerRawCellText(cells[i]);
        if (text.empty()) {
            return false;
        }
        initial += text;
    }

    if (initial == "q" && firstVowel < lastVowel && cells[firstVowel].base == 'u' &&
        cells[firstVowel].mark == 0) {
        initial = "qu";
        ++firstVowel;
    } else if (initial == "g" && firstVowel < lastVowel && cells[firstVowel].base == 'i' &&
               cells[firstVowel].mark == 0) {
        initial = "gi";
        ++firstVowel;
    }

    if (initials.find(initial) == initials.end()) {
        return false;
    }

    std::string nucleus;
    for (size_t i = firstVowel; i <= lastVowel; ++i) {
        if (!isVowelCell(cells[i])) {
            return false;
        }
        nucleus.push_back(cells[i].base);
        nucleus.push_back(static_cast<char>('0' + cells[i].mark));
    }
    if (nuclei.find(nucleus) == nuclei.end()) {
        return false;
    }

    std::string coda;
    for (size_t i = lastVowel + 1; i < cells.size(); ++i) {
        if (!isRawConsonant(cells[i])) {
            return false;
        }
        coda.push_back(lowerAscii(cells[i].raw));
    }
    if (nucleus == "y0" && !coda.empty()) {
        return false;
    }
    return codas.find(coda) != codas.end();
}

bool hasVietnameseMarkedCell(const std::vector<Cell> &cells) {
    return std::any_of(cells.begin(), cells.end(), [](const Cell &cell) {
        return cell.kind == Cell::Kind::StrokeD ||
               (cell.kind == Cell::Kind::Vowel && (cell.mark != 0 || cell.tone != TONE_NONE));
    });
}

bool shouldApplyNangOnWEscape(const std::vector<Cell> &cells) {
    if (cells.size() < 3) {
        return false;
    }
    const auto &last = cells.back();
    if (last.kind != Cell::Kind::Raw || lowerAscii(last.raw) != 'c') {
        return false;
    }

    std::vector<const Cell *> vowels;
    for (const auto &cell : cells) {
        if (isVowelCell(cell)) {
            vowels.push_back(&cell);
        }
    }
    if (vowels.size() < 2) {
        return false;
    }
    const auto *previous = vowels[vowels.size() - 2];
    const auto *current = vowels.back();
    return previous->base == 'u' && previous->mark == 0 && current->base == 'o' &&
           current->mark == 0;
}

bool isStandaloneStrokeD(const std::vector<Cell> &cells) {
    return cells.size() == 1 && cells.front().kind == Cell::Kind::StrokeD;
}

bool isPotentialVietnamesePrefixCells(const std::vector<Cell> &cells) {
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        return isStandaloneStrokeD(cells);
    }

    static const std::unordered_set<std::string> initials = {
        "",   "b",  "c",  "ch", "d",  "g",  "gh", "gi", "h",  "k",
        "kh", "l",  "m",  "n",  "ng", "ngh", "nh", "p",  "ph", "q",
        "qu", "r",  "s",  "t",  "th", "tr", "v",  "x",
    };
    static const std::vector<std::string> nucleusBases = {
        "a", "e", "i", "o", "u", "y", "ai", "ao", "au", "ay", "eo", "eu",
        "ia", "ie", "ieu", "iu", "oa", "oai", "oao", "oay", "oe", "oeo",
        "oi", "ua", "uo", "uoi", "ui", "uu", "uy", "uye", "uyeu", "ua",
        "ui", "uo", "uoi", "uu", "ye", "yeu",
    };

    size_t firstVowel = vowels.front();
    size_t lastVowel = vowels.back();
    std::string initial;
    for (size_t i = 0; i < firstVowel; ++i) {
        auto text = lowerRawCellText(cells[i]);
        if (text.empty()) {
            return false;
        }
        initial += text;
    }

    if (initial == "q" && firstVowel < lastVowel && cells[firstVowel].base == 'u' &&
        cells[firstVowel].mark == 0) {
        initial = "qu";
        ++firstVowel;
    } else if (initial == "g" && firstVowel < lastVowel && cells[firstVowel].base == 'i' &&
               cells[firstVowel].mark == 0) {
        initial = "gi";
        ++firstVowel;
    }
    if (initials.find(initial) == initials.end()) {
        return false;
    }

    std::string baseSequence;
    for (size_t i = firstVowel; i <= lastVowel; ++i) {
        if (!isVowelCell(cells[i])) {
            return false;
        }
        baseSequence.push_back(cells[i].base);
    }

    bool possibleNucleus = std::any_of(nucleusBases.begin(), nucleusBases.end(),
                                       [&baseSequence](const std::string &nucleus) {
                                           return startsWith(nucleus, baseSequence);
                                       });
    if (!possibleNucleus) {
        return false;
    }

    std::string trailingConsonants;
    for (size_t i = lastVowel + 1; i < cells.size(); ++i) {
        if (!isRawConsonant(cells[i])) {
            return false;
        }
        trailingConsonants.push_back(lowerAscii(cells[i].raw));
    }

    static const std::vector<std::string> codaPrefixes = {
        "", "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    return std::any_of(codaPrefixes.begin(), codaPrefixes.end(),
                       [&trailingConsonants](const std::string &coda) {
                           return startsWith(coda, trailingConsonants);
                       });
}

size_t firstNucleusCellIndex(const std::vector<Cell> &cells) {
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        return cells.size();
    }
    if (vowels.size() >= 2 && vowels.front() > 0) {
        std::string prefix;
        for (size_t i = 0; i < vowels.front(); ++i) {
            prefix += lowerRawCellText(cells[i]);
        }
        const auto &first = cells[vowels.front()];
        if ((endsWith(prefix, "g") && first.base == 'i' && first.mark == 0) ||
            (endsWith(prefix, "q") && first.base == 'u' && first.mark == 0)) {
            return vowels[1];
        }
    }
    return vowels.front();
}

struct CellSyllableParts {
    std::string onset;
    std::string nucleus;
    std::string coda;
    std::vector<size_t> nucleusIndexes;
};

CellSyllableParts parseCellSyllableParts(const std::vector<Cell> &cells) {
    CellSyllableParts parts;
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        for (const auto &cell : cells) {
            parts.onset += cellText(cell);
        }
        return parts;
    }

    size_t first = vowels.front();
    const size_t last = vowels.back();
    for (size_t i = 0; i < first; ++i) {
        parts.onset += cellText(cells[i]);
    }
    if (vowels.size() >= 2) {
        const auto &leading = cells[first];
        if ((lowerRaw(parts.onset) == "q" && leading.base == 'u' && leading.mark == 0) ||
            (lowerRaw(parts.onset) == "g" && leading.base == 'i' && leading.mark == 0)) {
            parts.onset += cellText(leading);
            first = vowels[1];
        }
    }
    for (size_t i = first; i <= last; ++i) {
        if (isVowelCell(cells[i])) {
            parts.nucleus += cellText(cells[i]);
            parts.nucleusIndexes.push_back(i);
        }
    }
    for (size_t i = last + 1; i < cells.size(); ++i) {
        parts.coda += cellText(cells[i]);
    }
    return parts;
}

std::optional<size_t> applyToneToCells(std::vector<Cell> &cells, int tone) {
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        return std::nullopt;
    }

    if (vowels.size() >= 2 && vowels.front() > 0) {
        std::string prefix;
        for (size_t i = 0; i < vowels.front(); ++i) {
            prefix += lowerRawCellText(cells[i]);
        }
        const auto &first = cells[vowels.front()];
        if ((endsWith(prefix, "g") && first.base == 'i' && first.mark == 0) ||
            (endsWith(prefix, "q") && first.base == 'u' && first.mark == 0)) {
            vowels.erase(vowels.begin());
        }
    }

    size_t target = vowels.back();
    if (vowels.size() >= 2) {
        for (size_t i = 1; i < vowels.size(); ++i) {
            const auto &prev = cells[vowels[i - 1]];
            const auto &cur = cells[vowels[i]];
            if (prev.base == 'u' && prev.mark == 3 && cur.base == 'o' && cur.mark == 3) {
                target = vowels[i];
                cells[target].tone = tone;
                return target;
            }
        }

        const auto &lastVowel = cells[vowels.back()];
        if (lastVowel.base == 'i' || lastVowel.base == 'y' || lastVowel.base == 'u') {
            target = vowels[vowels.size() - 2];
            cells[target].tone = tone;
            return target;
        }

        size_t last = vowels.back();
        std::vector<size_t> lastGroup;
        for (auto idx : vowels) {
            if (idx + 2 >= last) {
                lastGroup.push_back(idx);
            }
        }
        for (auto idx : lastGroup) {
            if (cells[idx].mark != 0) {
                target = idx;
                cells[target].tone = tone;
                return target;
            }
        }
        std::string nucleusBases;
        for (const auto index : vowels) {
            nucleusBases.push_back(cells[index].base);
        }
        if (nucleusBases == "ay" && tone == TONE_SAC) {
            cells[vowels[0]].mark = 1;
            target = vowels[0];
            cells[target].tone = tone;
            return target;
        }

        static const std::unordered_map<std::string, size_t> openTargets = {
            {"oa", 1}, {"oe", 1}, {"uy", 1}, {"ue", 1}, {"uo", 1}, {"ua", 0},
            {"uya", 1}, {"uyu", 1}, {"oai", 1}, {"uai", 1}, {"oao", 1},
        };
        const auto explicitTarget = openTargets.find(nucleusBases);
        if (last + 1 == cells.size() && explicitTarget != openTargets.end() &&
            explicitTarget->second < vowels.size()) {
            target = vowels[explicitTarget->second];
        } else if (last + 1 < cells.size() && lastGroup.size() > 1) {
            target = lastGroup.back();
        } else {
            target = lastGroup.front();
        }
    }

    cells[target].tone = tone;
    return target;
}

} // namespace

void UniikiState::reset() {
    ++contextGeneration;
    rawBuffer.clear();
    displayText.clear();
    lastRenderedText.clear();
    lastAppliedRawBuffer.clear();
    recoverRawBuffer.clear();
    recoverRenderedText.clear();
    recoverSuffix.clear();
    directActive = false;
    isInternalCommit = false;
    replacementInProgress = false;
    pendingKeys.clear();
    appliedEventIds.clear();
    replacement = ReplacementTransaction();
    revision = 0;
    appliedRevision = 0;
    desiredEventId = 0;
    desiredPhysicalKey.clear();
    nextCommitId = 0;
}

UniikiEngine::UniikiEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &) { return new UniikiState; }) {
    instance_->inputContextManager().registerProperty("uniikiState", &factory_);
    surroundingUpdatedWatcher_ = instance_->watchEvent(
        EventType::InputContextSurroundingTextUpdated, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &updated = static_cast<SurroundingTextUpdatedEvent &>(event);
            auto *ic = updated.inputContext();
            auto *state = ic->propertyFor(&factory_);
            ++state->surroundingRevision;
            const auto &surrounding = ic->surroundingText();
            auto beforeCursor =
                surrounding.isValid() ? textBeforeCursor(surrounding) : std::string();
            // Client updates are observations only. They must never own the
            // key-pipeline lock: browsers may omit, delay or reorder them when
            // inline autocomplete changes its selection.
            FCITX_DEBUG() << "UniikiTrace SURROUNDING_OBSERVED"
                         << " contextId=" << reinterpret_cast<uintptr_t>(ic)
                         << " contextGeneration=" << state->contextGeneration
                         << " surroundingValid=" << surrounding.isValid()
                         << " surroundingText="
                         << (surrounding.isValid() ? surrounding.text()
                                                   : std::string("invalid"))
                         << " beforeCursor=" << beforeCursor
                         << " cursor="
                         << (surrounding.isValid()
                                 ? std::to_string(surrounding.cursor())
                                 : std::string("invalid"))
                         << " anchor="
                         << (surrounding.isValid()
                                 ? std::to_string(surrounding.anchor())
                                 : std::string("invalid"))
                         << " processing=" << state->replacementInProgress
                         << " renderInFlight=" << state->replacementInProgress
                         << " waitingForSurroundingUpdate=0"
                         << " queueLength=" << state->pendingKeys.size();
        });
    focusOutWatcher_ = instance_->watchEvent(
        EventType::InputContextFocusOut, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &focusOut = static_cast<FocusOutEvent &>(event);
            auto *ic = focusOut.inputContext();
            auto *state = ic->propertyFor(&factory_);
            const auto oldGeneration = state->contextGeneration;
            state->focused = false;
            state->reset();
            FCITX_DEBUG() << "UniikiTrace CONTEXT_RESET"
                         << " reason=focus-out"
                         << " contextId=" << reinterpret_cast<uintptr_t>(ic)
                         << " oldGeneration=" << oldGeneration
                         << " contextGeneration=" << state->contextGeneration
                         << " processing=0 renderInFlight=0"
                         << " waitingForSurroundingUpdate=0 queueLength=0";
        });
}

std::string UniikiEngine::subModeLabelImpl(const InputMethodEntry &, InputContext &) {
    return lang_ == "VI" ? "VI" : "EN";
}

std::string UniikiEngine::subModeIconImpl(const InputMethodEntry &, InputContext &) {
    return lang_ == "VI" ? "uniiki-vi" : "uniiki-en";
}

void UniikiEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    auto sym = event.key().sym();
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);

    bool isRelease = event.isRelease();
    bool isShift = (sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R);
    bool isCtrl = (sym == FcitxKey_Control_L || sym == FcitxKey_Control_R);

    if (isShift) {
        if (!isRelease) {
            ctrl_shift_shift_pressed_ = true;
            if (ctrl_shift_ctrl_pressed_) {
                ctrl_shift_combo_active_ = true;
                ctrl_shift_interrupted_ = false;
            }
        } else {
            ctrl_shift_shift_pressed_ = false;
            if (ctrl_shift_combo_active_ && !ctrl_shift_interrupted_) {
                lang_ = (lang_ == "VI") ? "EN" : "VI";
                ctrl_shift_combo_active_ = false;
                if (ic) {
                    ic->updateUserInterface(UserInterfaceComponent::StatusArea);
                }
            }
            if (!ctrl_shift_ctrl_pressed_ && !ctrl_shift_shift_pressed_) {
                ctrl_shift_combo_active_ = false;
                ctrl_shift_interrupted_ = false;
            }
        }
        return;
    }

    if (isCtrl) {
        if (!isRelease) {
            ctrl_shift_ctrl_pressed_ = true;
            if (ctrl_shift_shift_pressed_) {
                ctrl_shift_combo_active_ = true;
                ctrl_shift_interrupted_ = false;
            }
        } else {
            ctrl_shift_ctrl_pressed_ = false;
            if (ctrl_shift_combo_active_ && !ctrl_shift_interrupted_) {
                lang_ = (lang_ == "VI") ? "EN" : "VI";
                ctrl_shift_combo_active_ = false;
                if (ic) {
                    ic->updateUserInterface(UserInterfaceComponent::StatusArea);
                }
            }
            if (!ctrl_shift_ctrl_pressed_ && !ctrl_shift_shift_pressed_) {
                ctrl_shift_combo_active_ = false;
                ctrl_shift_interrupted_ = false;
            }
        }
        return;
    }

    if (!isRelease && !isShift && !isCtrl) {
        if (ctrl_shift_ctrl_pressed_ || ctrl_shift_shift_pressed_ || ctrl_shift_combo_active_) {
            ctrl_shift_interrupted_ = true;
        }
    }

    if (lang_ == "EN") {
        if (state) {
            state->reset();
        }
        return;
    }

    auto eventId = nextKeyEventId();
    const auto keyStartedAt = monotonicMicros();
    auto physicalKey = event.key().toString();
    auto keyStateValue = event.key().states().toInteger();
    bool isRepeat = (keyStateValue & static_cast<uint32_t>(KeyState::Repeat)) != 0;
    bool hasNonRepeatState = (keyStateValue & ~static_cast<uint32_t>(KeyState::Repeat)) != 0;
    auto rawBufferBefore = state->rawBuffer;
    const bool internalCommitAtEntry = state->isInternalCommit;
    auto candidateBefore = state->displayText;
    auto renderedTextBefore = state->lastRenderedText;
    const bool processingBefore = state->replacementInProgress;
    const auto queueLengthBefore = state->pendingKeys.size();
    const auto contextGenerationBefore = state->contextGeneration;
    bool handledByEngine = false;
    bool forwardedToClient = true;
    bool consumed = false;
    bool commitCalled = false;
    bool deleteCalled = false;
    bool preeditCalled = false;
    bool resetCalled = false;
    bool consumedAsTelex = false;
    bool queued = false;
    bool dropped = false;
    std::string resetReason;
    std::string stateBefore = state->rawBuffer.empty() ? "IDLE" : "ACTIVE_COMPOSITION";

    auto makeTraceRecord = [&](const char *stage) {
        UniikiState::TraceRecord record;
        record.timestampMicros = monotonicMicros();
        record.threadId = static_cast<uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        record.eventId = eventId;
        record.contextId = reinterpret_cast<uintptr_t>(ic);
        record.contextGeneration = state->contextGeneration;
        record.transactionId = state->replacement.commitId;
        record.eventType = event.isRelease() ? "key-release" : "key-press";
        record.key = physicalKey;
        record.functionStage = stage;
        record.rawBefore = rawBufferBefore;
        record.rawAfter = state->rawBuffer;
        record.desiredRendered = state->displayText;
        record.appliedRendered = state->lastRenderedText;
        record.parserStateHash = stateHashText(*state);
        record.desiredRevision = state->revision;
        record.appliedRevision = state->appliedRevision;
        record.surroundingRevision = state->surroundingRevision;
        record.queueLength = state->pendingKeys.size();
        record.isPress = !event.isRelease();
        record.isRelease = event.isRelease();
        record.isRepeat = isRepeat;
        record.processing = state->replacementInProgress;
        record.renderInFlight = state->replacementInProgress;
        record.focusState = state->focused;
        const auto &surrounding = ic->surroundingText();
        record.cursor = surrounding.isValid()
                            ? std::to_string(surrounding.cursor())
                            : "invalid";
        record.anchor = surrounding.isValid()
                            ? std::to_string(surrounding.anchor())
                            : "invalid";
        return record;
    };
    appendTrace(*state, makeTraceRecord("keyEvent:enter"));
    ScopeExit keyHandlerExit([&]() {
        appendTrace(*state, makeTraceRecord("keyEvent:exit"));
        const auto duration = monotonicMicros() - keyStartedAt;
        if (duration > KEY_HANDLER_SLOW_MICROS) {
            FCITX_ERROR() << "UniikiTrace KEY_HANDLER_SLOW"
                          << " eventId=" << eventId
                          << " durationMicros=" << duration
                          << " rawBuffer=" << state->rawBuffer
                          << " queueLength=" << state->pendingKeys.size()
                          << " processing=" << state->replacementInProgress;
            dumpTraceRing(*state, "KEY_HANDLER_SLOW");
        }
        if (state->replacementInProgress && !queued &&
            !internalCommitAtEntry) {
            FCITX_ERROR() << "UniikiTrace PROCESSING_STUCK"
                          << " eventId=" << eventId
                          << " rawBuffer=" << state->rawBuffer
                          << " queueLength=" << state->pendingKeys.size();
            dumpTraceRing(*state, "PROCESSING_STUCK");
            state->isInternalCommit = false;
            state->replacementInProgress = false;
        }
    });

    auto logKeyTrace = [&]() {
        std::string stateAfter = state->rawBuffer.empty() ? "IDLE" : "ACTIVE_COMPOSITION";
        bool isLetter = sym >= FcitxKey_A && sym <= FcitxKey_z;
        int ownershipCount = (handledByEngine ? 1 : 0) + (forwardedToClient ? 1 : 0) + (queued ? 1 : 0);
        bool ownershipOk = ownershipCount == 1;
        const char *ownership = ownershipOk ? "KEY_OWNERSHIP_OK"
                              : (ownershipCount > 1 ? "KEY_DOUBLE_OWNERSHIP" : "KEY_DROPPED");
        std::string unicode;
        if (sym >= FcitxKey_space && sym <= FcitxKey_asciitilde) {
            unicode = std::string(1, static_cast<char>(sym));
        }
        const auto [traceDeleteUnits, traceInsertText] =
            replacementDelta(renderedTextBefore, state->displayText);
        const auto traceOperation =
            traceDeleteUnits > 0
                ? UniikiState::RenderOperation::ReplaceSuffix
                : (traceInsertText.empty()
                       ? UniikiState::RenderOperation::NoVisibleChange
                       : UniikiState::RenderOperation::AppendLiteral);
        const auto &surrounding = ic->surroundingText();
        FCITX_DEBUG() << "UniikiTrace KEY_EVENT"
                     << " eventId=" << eventId
                     << " contextId=" << reinterpret_cast<uintptr_t>(ic)
                     << " contextGeneration=" << contextGenerationBefore
                     << " eventType="
                     << (event.isRelease() ? "key-release" : "key-press")
                     << " handlerInstanceId="
                     << reinterpret_cast<uintptr_t>(this)
                     << " physicalKey=" << physicalKey
                     << " unicode=" << unicode
                     << " keyCode=" << static_cast<uint32_t>(sym)
                     << " keyState=" << keyStateValue
                     << " isPress=" << boolText(!event.isRelease())
                     << " isRelease=" << boolText(event.isRelease())
                     << " isRepeat=" << boolText(isRepeat)
                     << " repeat=" << boolText(isRepeat)
                     << " timeStamp=" << event.time()
                     << " rawBufferBefore=" << rawBufferBefore
                     << " rawBufferAfter=" << state->rawBuffer
                     << " candidateBefore=" << candidateBefore
                     << " candidateAfter=" << state->displayText
                     << " renderedTextBefore=" << renderedTextBefore
                     << " renderedTextAfter=" << state->lastRenderedText
                     << " oldRendered=" << renderedTextBefore
                     << " newRendered=" << state->displayText
                     << " operation=" << renderOperationText(traceOperation)
                     << " deleteUnits=" << traceDeleteUnits
                     << " insertText=" << traceInsertText
                     << " processingBefore=" << processingBefore
                     << " renderInFlightBefore=" << processingBefore
                     << " waitingForSurroundingUpdate=0"
                     << " queueLengthBefore=" << queueLengthBefore
                     << " queueLengthAfter=" << state->pendingKeys.size()
                     << " processingAfter=" << state->replacementInProgress
                     << " renderInFlightAfter=" << state->replacementInProgress
                     << " surroundingText="
                     << (surrounding.isValid() ? surrounding.text()
                                               : std::string("invalid"))
                     << " cursor="
                     << (surrounding.isValid()
                             ? std::to_string(surrounding.cursor())
                             : std::string("invalid"))
                     << " anchor="
                     << (surrounding.isValid()
                             ? std::to_string(surrounding.anchor())
                             : std::string("invalid"))
                     << " handledByEngine=" << boolText(handledByEngine)
                     << " consumed=" << boolText(consumed)
                     << " forwardedToClient=" << boolText(forwardedToClient)
                     << " commitCalled=" << boolText(commitCalled)
                     << " deleteCalled=" << boolText(deleteCalled)
                     << " preeditCalled=" << boolText(preeditCalled)
                     << " resetCalled=" << boolText(resetCalled)
                     << " resetReason=" << resetReason
                     << " ownership=" << ownership
                     << " letterOwnershipInvariant=" << boolText(!isLetter || ownershipOk);
        FCITX_DEBUG() << "UniikiTrace KEY_EVENT_RESULT"
                     << " eventId=" << eventId
                     << " physicalKey=" << physicalKey
                     << " stateBefore=" << stateBefore
                     << " stateAfter=" << stateAfter
                     << " rawBefore=" << rawBufferBefore
                     << " rawAfter=" << state->rawBuffer
                     << " candidate=" << state->displayText
                     << " consumedAsTelex=" << boolText(consumedAsTelex)
                     << " consumedByEngine=" << boolText(handledByEngine)
                     << " forwardedToClient=" << boolText(forwardedToClient)
                     << " queued=" << boolText(queued)
                     << " dropped=" << boolText(dropped)
                     << " preeditText="
                     << " commitCalled=" << boolText(commitCalled)
                     << " deleteCalled=" << boolText(deleteCalled)
                     << " resetReason=" << resetReason;
        auto capabilities = ic->capabilityFlags();
        FCITX_DEBUG() << "UniikiTrace INPUT_CONTEXT_CAPABILITY"
                     << " eventId=" << eventId
                     << " Preedit=" << boolText(capabilities.test(CapabilityFlag::Preedit))
                     << " ClientSideInputPanel="
                     << boolText(capabilities.test(CapabilityFlag::ClientSideInputPanel))
                     << " SurroundingText="
                     << boolText(capabilities.test(CapabilityFlag::SurroundingText))
                     << " PasswordOrSensitive="
                     << boolText(capabilities.test(CapabilityFlag::PasswordOrSensitive))
                     << " FormattedPreedit="
                     << boolText(capabilities.test(CapabilityFlag::FormattedPreedit))
                     << " rawCapabilities=" << capabilities.toInteger();
        if (ownershipCount == 0) {
            FCITX_DEBUG() << "UniikiTrace KEY_DROPPED"
                         << " eventId=" << eventId
                         << " physicalKey=" << physicalKey;
        }
        if (ownershipCount > 1) {
            FCITX_DEBUG() << "UniikiTrace KEY_DOUBLE_OWNERSHIP"
                         << " eventId=" << eventId
                         << " physicalKey=" << physicalKey;
        }
        auto segment = traceActiveSegment(state->rawBuffer, state->displayText);
        FCITX_DEBUG() << "UniikiTrace TELEX_PARSE"
                     << " eventId=" << eventId
                     << " key=" << unicode
                     << " rawBuffer=" << state->rawBuffer
                     << " activeSegmentStart=" << segment.start
                     << " activeSegmentRaw=" << segment.raw
                     << " parsedOnset=" << segment.onset
                     << " parsedVowel=" << segment.vowel
                     << " parsedCoda=" << segment.coda
                     << " candidateTransform=" << segment.candidate
                     << " candidateRaw=" << state->displayText
                     << " triggerKey=" << segment.triggerKey
                     << " triggerRawIndex=" << segment.triggerRawIndex
                     << " transformApplied=" << boolText(segment.applied)
                     << " transformStillValid=" << boolText(segment.stillValid)
                     << " transformUndone=" << boolText(segment.undone)
                     << " consumedRawIndexes=" << segment.consumedRawIndexes
                     << " renderedText=" << state->displayText
                     << " fallbackReason=" << segment.fallbackReason;
    };

    if (event.isRelease()) {
        logKeyTrace();
        return;
    }

    if (hasNonRepeatState) {
        logKeyTrace();
        return;
    }

    if (state->isInternalCommit) {
        logKeyTrace();
        return;
    }
    if (state->replacementInProgress) {
        // Re-entrant physical input is owned exactly once and replayed in
        // order as soon as the synchronous dispatch scope closes.
        event.filterAndAccept();
        state->pendingKeys.push_back({eventId, event.key(), event.rawKey(), event.isRelease(), event.time()});
        handledByEngine = false;
        forwardedToClient = false;
        consumed = true;
        queued = true;
        FCITX_DEBUG() << "UniikiTrace KEY_QUEUED"
                     << " eventId=" << eventId
                     << " physicalKey=" << event.key().toString()
                     << " queued=1"
                     << " queueLength=" << state->pendingKeys.size();
        FCITX_DEBUG() << "UniikiTrace ASSERT_REPLACEMENT_PENDING physicalKey="
                     << event.key().toString()
                     << " rawBuffer=" << state->rawBuffer
                     << " renderedText=" << state->lastRenderedText
                     << " revision=" << state->revision;
        logKeyTrace();
        return;
    }
    if (state->directActive && !state->lastRenderedText.empty() &&
        state->appliedRevision == state->revision) {
        const auto &surrounding = ic->surroundingText();
        auto beforeCursor =
            surrounding.isValid() ? textBeforeCursor(surrounding) : std::string();
        if (surrounding.isValid() &&
            !endsWith(beforeCursor, state->lastRenderedText)) {
            FCITX_DEBUG() << "UniikiTrace STATE_RESET"
                         << " reason=surrounding-mismatch"
                         << " expectedSuffix=" << state->lastRenderedText
                         << " actualBeforeCursor=" << beforeCursor;
            state->reset();
            resetCalled = true;
            resetReason = "surrounding mismatch";
        }
    }
    if (state->rawBuffer.empty() &&
        sym >= FcitxKey_A && sym <= FcitxKey_z) {
        const auto &surrounding = ic->surroundingText();
        if (surrounding.isValid() &&
            surrounding.anchor() > surrounding.cursor()) {
            const auto beforeCursor = textBeforeCursor(surrounding);
            std::string typedPrefix;
            for (auto it = beforeCursor.rbegin(); it != beforeCursor.rend();
                 ++it) {
                if (!isAsciiWordChar(*it)) {
                    break;
                }
                typedPrefix.insert(typedPrefix.begin(), *it);
            }
            if (!typedPrefix.empty() &&
                evaluateTelex(typedPrefix) == typedPrefix) {
                state->rawBuffer = typedPrefix;
                state->displayText = typedPrefix;
                state->lastRenderedText = typedPrefix;
                state->lastAppliedRawBuffer = typedPrefix;
                state->appliedRevision = state->revision;
                state->directActive = true;
                FCITX_DEBUG()
                    << "UniikiTrace AUTOCOMPLETE_STATE_RECOVERED"
                    << " eventId=" << eventId
                    << " typedPrefix=" << typedPrefix
                    << " surroundingText=" << surrounding.text()
                    << " cursor=" << surrounding.cursor()
                    << " anchor=" << surrounding.anchor();
            }
        }
    }
    bool directCommit = canUseDirectCommit(ic, *state);
    if (!directCommit && state->directActive) {
        auto capabilities = ic->capabilityFlags();
        const auto &surrounding = ic->surroundingText();
        FCITX_DEBUG() << "UniikiTrace DIRECT_REPLACE_UNSUPPORTED"
                     << " physicalKey=" << event.key().toString()
                     << " capabilities=" << capabilities.toInteger()
                     << " hasSurroundingTextCapability="
                     << capabilities.test(CapabilityFlag::SurroundingText)
                     << " surroundingValid=" << surrounding.isValid()
                     << " cursor=" << (surrounding.isValid() ? std::to_string(surrounding.cursor())
                                                              : std::string("invalid"))
                     << " anchor=" << (surrounding.isValid() ? std::to_string(surrounding.anchor())
                                                              : std::string("invalid"))
                     << " oldRendered=" << state->lastRenderedText;
    }

    if (sym == FcitxKey_BackSpace && !state->rawBuffer.empty()) {
        event.filterAndAccept();
        handledByEngine = true;
        forwardedToClient = false;
        consumed = true;
        auto rawBefore = state->rawBuffer;
        auto lastRenderedBefore = state->lastRenderedText;
        const auto provenance = conversionWithProvenance(state->rawBuffer);
        std::vector<size_t> removedRawIndexes;
        if (!provenance.graphemes.empty()) {
            removedRawIndexes = provenance.graphemes.back().rawIndexes;
        }
        char poppedChar = rawBefore.back();
        bool isLiteralEnding = false;
        if (!lastRenderedBefore.empty() && static_cast<unsigned char>(poppedChar) < 128) {
            char lastChar = lastRenderedBefore.back();
            if (static_cast<unsigned char>(lastChar) < 128) {
                isLiteralEnding = (std::tolower(static_cast<unsigned char>(lastChar)) == 
                                   std::tolower(static_cast<unsigned char>(poppedChar)));
            }
        }

        state->rawBuffer = rawAfterVisualBackspace(state->rawBuffer);
        if (isLiteralEnding) {
            state->displayText = lastRenderedBefore.substr(0, lastRenderedBefore.length() - 1);
        } else {
            state->displayText =
                state->rawBuffer.empty() ? std::string() : evaluateTelex(state->rawBuffer);
        }
        FCITX_DEBUG() << "UniikiTrace VISUAL_BACKSPACE"
                     << " eventId=" << eventId
                     << " rawBefore=" << rawBefore
                     << " renderedBefore=" << lastRenderedBefore
                     << " removedGrapheme="
                     << (provenance.graphemes.empty()
                             ? std::string()
                             : provenance.graphemes.back().text)
                     << " removedRawIndexes="
                     << rawIndexesText(removedRawIndexes)
                     << " rawAfter=" << state->rawBuffer
                     << " renderedAfter=" << state->displayText;
        ++state->revision;
        state->desiredEventId = eventId;
        state->desiredPhysicalKey = "BackSpace";
        if (directCommit) {
            const auto renderDelta =
                replacementDelta(lastRenderedBefore, state->displayText);
            deleteCalled = renderDelta.first > 0;
            commitCalled = !renderDelta.second.empty();
            renderDirectCommit(ic, *state, "BackSpace", rawBefore, lastRenderedBefore, eventId);
            resetCalled = true;
            resetReason = "BackSpace rendered grapheme";
        } else {
            state->reset();
            resetCalled = true;
            resetReason = "BackSpace";
            InternalCommitGuard internalCommit(*state);
            ic->forwardKey(event.rawKey(), false, event.time());
        }
        logKeyTrace();
        return;
    }

    if (sym == FcitxKey_BackSpace && state->rawBuffer.empty() &&
        state->recoverSuffix == " " && !state->recoverRawBuffer.empty()) {
        state->rawBuffer = state->recoverRawBuffer;
        state->displayText = state->recoverRenderedText;
        state->lastRenderedText = state->recoverRenderedText;
        state->directActive = true;
        state->recoverRawBuffer.clear();
        state->recoverRenderedText.clear();
        state->recoverSuffix.clear();
        resetReason = "Space BackSpace recovery";
        FCITX_DEBUG() << "UniikiTrace RESTORE_AFTER_SPACE_BACKSPACE"
                     << " eventId=" << eventId
                     << " rawBufferAfter=" << state->rawBuffer
                     << " renderedTextAfter=" << state->lastRenderedText;
        logKeyTrace();
        return;
    }

    if (!state->recoverSuffix.empty()) {
        state->recoverRawBuffer.clear();
        state->recoverRenderedText.clear();
        state->recoverSuffix.clear();
    }

    if (sym == FcitxKey_space) {
        if (!state->rawBuffer.empty()) {
            event.filterAndAccept();
            handledByEngine = true;
            forwardedToClient = false;
            consumed = true;
            commitCalled = true;
            resetCalled = true;
            resetReason = "Space committed";
            finishDirectComposition(ic, *state, " ", true);
        }
        logKeyTrace();
        return;
    }

    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter || sym == FcitxKey_Tab) {
        if (!state->rawBuffer.empty()) {
            handledByEngine = false;
            forwardedToClient = true;
            consumed = false;
            resetCalled = true;
            resetReason = sym == FcitxKey_Tab ? "Tab" : "Enter";
            finishDirectComposition(ic, *state, "", false);
        }
        logKeyTrace();
        return;
    }

    if (sym < FcitxKey_space || sym > FcitxKey_asciitilde) {
        if (!state->rawBuffer.empty()) {
            handledByEngine = false;
            forwardedToClient = true;
            consumed = false;
            resetCalled = true;
            resetReason = "cursor movement";
            finishDirectComposition(ic, *state, "", false);
        }
        logKeyTrace();
        return;
    }

    char ch = static_cast<char>(sym);
    if (!isAsciiWordChar(ch)) {
        if (!state->rawBuffer.empty()) {
            event.filterAndAccept();
            handledByEngine = true;
            forwardedToClient = false;
            consumed = true;
            commitCalled = true;
            resetCalled = true;
            resetReason = "punctuation committed";
            finishDirectComposition(ic, *state, std::string(1, ch), true);
        }
        logKeyTrace();
        return;
    }

    auto rawBefore = state->rawBuffer;
    auto lastRenderedBefore = state->lastRenderedText;
    if (!state->appliedEventIds.insert(eventId).second) {
        event.filterAndAccept();
        handledByEngine = true;
        forwardedToClient = false;
        consumed = true;
        dropped = true;
        FCITX_DEBUG() << "UniikiTrace DUPLICATE_EVENT_DISCARDED"
                     << " eventId=" << eventId
                     << " physicalKey=" << physicalKey;
        logKeyTrace();
        return;
    }
    auto result = processChar(*state, ch);
    if (!result.handled) {
        logKeyTrace();
        return;
    }

    event.filterAndAccept();
    handledByEngine = true;
    forwardedToClient = false;
    consumed = true;
    consumedAsTelex = true;
    state->displayText = result.text;
    ++state->revision;
    state->desiredEventId = eventId;
    state->desiredPhysicalKey = std::string(1, ch);
    if (directCommit) {
        const auto renderDelta =
            replacementDelta(lastRenderedBefore, state->displayText);
        deleteCalled = renderDelta.first > 0;
        commitCalled = !renderDelta.second.empty();
        renderDirectCommit(ic, *state, std::string(1, ch), rawBefore, lastRenderedBefore, eventId);
        processPendingKeys(ic, *state);
    } else {
        const auto renderDelta =
            replacementDelta(lastRenderedBefore, state->displayText);
        deleteCalled = renderDelta.first > 0;
        commitCalled = !renderDelta.second.empty();
        renderDirectCommit(ic, *state, std::string(1, ch), rawBefore, lastRenderedBefore, eventId);
        processPendingKeys(ic, *state);
    }
    logKeyTrace();
}

void UniikiEngine::activate(const InputMethodEntry &, InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
    state->focused = true;
}

void UniikiEngine::deactivate(const InputMethodEntry &, InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->focused = false;
    state->reset();
}

void UniikiEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->reset();
}

void UniikiEngine::processPendingKeys(InputContext *ic, UniikiState &state) const {
    static thread_local size_t drainDepth = 0;
    ++drainDepth;
    ScopeExit drainDepthExit([&]() { --drainDepth; });
    auto literalQueueFallback = [&](const char *reason) {
        FCITX_ERROR() << "UniikiTrace " << reason
                      << " queueLength=" << state.pendingKeys.size()
                      << " processing=" << state.replacementInProgress
                      << " desiredRevision=" << state.revision
                      << " appliedRevision=" << state.appliedRevision;
        dumpTraceRing(state, reason);
        auto pending = std::move(state.pendingKeys);
        state.reset();
        InternalCommitGuard internalCommit(state);
        for (const auto &key : pending) {
            const auto sym = key.key.sym();
            if (!key.isRelease && sym >= FcitxKey_space &&
                sym <= FcitxKey_asciitilde) {
                ic->commitString(std::string(1, static_cast<char>(sym)));
            }
        }
    };
    if (drainDepth > 64) {
        literalQueueFallback("QUEUE_STATE_CYCLE");
        return;
    }
    if (state.replacementInProgress) {
        return;
    }
    if (state.appliedRevision < state.revision) {
        scheduleLatestRender(ic, state);
        return;
    }
    const size_t maxSteps = state.pendingKeys.size() * 4 + 16;
    size_t stepCount = 0;
    while (!state.replacementInProgress && !state.pendingKeys.empty()) {
        if (++stepCount > maxSteps) {
            literalQueueFallback("QUEUE_NO_PROGRESS");
            return;
        }
        auto pending = state.pendingKeys.front();
        state.pendingKeys.pop_front();
        auto remaining = std::move(state.pendingKeys);
        processPendingKey(ic, state, pending);
        if (state.pendingKeys.empty()) {
            state.pendingKeys = std::move(remaining);
        } else {
            state.pendingKeys.insert(state.pendingKeys.end(),
                                     remaining.begin(), remaining.end());
        }
    }
}

void UniikiEngine::scheduleLatestRender(InputContext *ic,
                                        UniikiState &state) const {
    if (state.replacementInProgress ||
        state.appliedRevision >= state.revision) {
        return;
    }
    FCITX_DEBUG() << "UniikiTrace RENDER_SCHEDULE"
                 << " eventId=" << state.desiredEventId
                 << " desiredRevision=" << state.revision
                 << " appliedRevision=" << state.appliedRevision
                 << " desiredRendered=" << state.displayText
                 << " appliedRendered=" << state.lastRenderedText
                 << " renderInFlight=0"
                 << " coalesced="
                 << boolText(state.revision > state.appliedRevision + 1);
    renderDirectCommit(ic, state, state.desiredPhysicalKey,
                       state.lastAppliedRawBuffer, state.lastRenderedText,
                       state.desiredEventId);
}

void UniikiEngine::completeReplacement(InputContext *ic, UniikiState &state,
                                       const char *source) const {
    if (!state.replacementInProgress) {
        return;
    }
    auto &replacement = state.replacement;
    if (replacement.contextGeneration != state.contextGeneration) {
        FCITX_ERROR() << "UniikiTrace STALE_CONTEXT_CALLBACK"
                      << " eventId=" << replacement.id
                      << " transactionGeneration="
                      << replacement.contextGeneration
                      << " currentGeneration=" << state.contextGeneration;
        state.isInternalCommit = false;
        state.replacementInProgress = false;
        replacement.completed = true;
        return;
    }
    const auto &surrounding = ic->surroundingText();
    const auto visibleAfter =
        surrounding.isValid() ? textBeforeCursor(surrounding)
                              : replacement.expectedAfterCommit;
    state.lastRenderedText = replacement.newRendered;
    state.lastAppliedRawBuffer = replacement.rawAfter;
    state.appliedRevision = replacement.inputVersion;
    state.directActive = !replacement.newRendered.empty();
    FCITX_DEBUG() << "UniikiTrace REPLACE_END"
                 << " eventId=" << replacement.id
                 << " application=" << replacement.application
                 << " rawBefore=" << replacement.rawBefore
                 << " rawAfter=" << replacement.rawAfter
                 << " converterOutput=" << replacement.converterOutput
                 << " oldRendered=" << replacement.oldRendered
                 << " newRendered=" << replacement.newRendered
                 << " surroundingText=" << replacement.surroundingTextBefore
                 << " cursor=" << replacement.cursorBefore
                 << " anchor=" << replacement.anchorBefore
                 << " selectionLength=" << replacement.selectionLength
                 << " suffixMatches="
                 << (replacement.suffixMatches ? "true" : "false")
                 << " deleteOffset=" << replacement.deleteOffset
                 << " deleteSize=" << replacement.deleteUnits
                 << " commitText=" << replacement.commitText
                 << " visibleTextAfter=" << visibleAfter
                 << " surroundingTextAfter="
                 << (surrounding.isValid() ? surrounding.text()
                                           : std::string("invalid"))
                 << " cursorAfter="
                 << (surrounding.isValid()
                         ? std::to_string(surrounding.cursor())
                         : std::string("invalid"))
                 << " anchorAfter="
                 << (surrounding.isValid()
                         ? std::to_string(surrounding.anchor())
                         : std::string("invalid"))
                 << " replacementSucceeded=true"
                 << " fallbackUsed=false"
                 << " transactionId=" << replacement.commitId
                 << " stateVersion=" << replacement.inputVersion
                 << " source=" << source;
    FCITX_DEBUG() << "UniikiTrace REPLACEMENT_COMPLETE"
                 << " eventId=" << replacement.id
                 << " inputVersion=" << replacement.inputVersion
                 << " currentVersion=" << state.revision
                 << " commitId=" << replacement.commitId
                 << " source=" << source
                 << " queueLength=" << state.pendingKeys.size()
                 << " desiredRendered=" << state.displayText
                 << " desiredRevision=" << state.revision
                 << " appliedRendered=" << state.lastRenderedText
                 << " appliedRevision=" << state.appliedRevision
                 << " renderInFlight=0"
                 << " coalesced="
                 << boolText(state.appliedRevision < state.revision)
                 << " transactionDurationMicros="
                 << (monotonicMicros() - replacement.startedAtMicros);
    FCITX_DEBUG() << "UniikiTrace CLIENT_SYNC"
                 << " eventId=" << replacement.id
                 << " commitId=" << replacement.commitId
                 << " desiredRendered=" << state.displayText
                 << " desiredRevision=" << state.revision
                 << " appliedRendered=" << state.lastRenderedText
                 << " appliedRevision=" << state.appliedRevision
                 << " surroundingValid=" << surrounding.isValid()
                 << " exactAck=1";
    state.isInternalCommit = false;
    state.replacementInProgress = false;
    replacement.completed = true;
    processPendingKeys(ic, state);
#ifndef NDEBUG
    if (state.replacementInProgress) {
        FCITX_ERROR() << "UniikiTrace PROCESSING_STUCK";
        FCITX_ERROR() << "UniikiTrace RENDER_IN_FLIGHT_STUCK";
    }
    if (!state.pendingKeys.empty()) {
        FCITX_ERROR() << "UniikiTrace KEY_QUEUE_NOT_DRAINED"
                      << " queueLength=" << state.pendingKeys.size();
    }
#endif
}

void UniikiEngine::abortReplacement(InputContext *ic, UniikiState &state,
                                    const char *reason) const {
    if (!state.replacementInProgress) {
        return;
    }
    FCITX_DEBUG() << "UniikiTrace REPLACE_ABORT"
                 << " eventId=" << state.replacement.id
                 << " transactionId=" << state.replacement.commitId
                 << " reason=" << reason
                 << " commitText=" << state.replacement.commitText
                 << " stateUpdated=0"
                 << " replacementSucceeded=false"
                 << " fallbackUsed=false"
                 << " sourceStatePreserved=true"
                 << " queueLength=" << state.pendingKeys.size();
    auto pending = std::move(state.pendingKeys);
    state.reset();
    state.pendingKeys = std::move(pending);
    processPendingKeys(ic, state);
}

void UniikiEngine::processPendingKey(InputContext *ic, UniikiState &state,
                                     const UniikiState::PendingKey &pending) const {
    auto sym = pending.key.sym();
    auto physicalKey = pending.key.toString();
    auto stateBefore = state.rawBuffer.empty() ? "IDLE" : "ACTIVE_COMPOSITION";
    auto rawBefore = state.rawBuffer;
    auto renderedBefore = state.lastRenderedText;
    bool consumedAsTelex = false;
    bool forwarded = false;
    bool deleteCalled = false;
    bool commitCalled = false;
    std::string resetReason;

    if (!state.appliedEventIds.insert(pending.eventId).second) {
        FCITX_DEBUG() << "UniikiTrace DUPLICATE_EVENT_DISCARDED"
                     << " eventId=" << pending.eventId
                     << " physicalKey=" << physicalKey
                     << " source=queued";
        return;
    }

    auto logPendingResult = [&]() {
        auto stateAfter = state.rawBuffer.empty() ? "IDLE" : "ACTIVE_COMPOSITION";
        FCITX_DEBUG() << "UniikiTrace KEY_EVENT_RESULT"
                     << " eventId=" << pending.eventId
                     << " physicalKey=" << physicalKey
                     << " stateBefore=" << stateBefore
                     << " stateAfter=" << stateAfter
                     << " rawBefore=" << rawBefore
                     << " rawAfter=" << state.rawBuffer
                     << " renderedBefore=" << renderedBefore
                     << " renderedAfter=" << state.lastRenderedText
                     << " candidate=" << state.displayText
                     << " consumedAsTelex=" << boolText(consumedAsTelex)
                     << " consumedByEngine=" << boolText(consumedAsTelex)
                     << " forwardedToClient=" << boolText(forwarded)
                     << " queued=0"
                     << " dropped=0"
                     << " preeditText="
                     << " deleteCalled=" << boolText(deleteCalled)
                     << " commitCalled=" << boolText(commitCalled)
                     << " resetReason=" << resetReason
                     << " source=queued";
    };

    if (pending.isRelease) {
        forwarded = true;
        ic->forwardKey(pending.rawKey, true, pending.time);
        logPendingResult();
        return;
    }

    if (sym == FcitxKey_space) {
        if (!state.rawBuffer.empty()) {
            resetReason = "Space committed";
            finishDirectComposition(ic, state, " ", true);
        } else {
            forwarded = true;
            ic->forwardKey(pending.rawKey, false, pending.time);
        }
        logPendingResult();
        return;
    }

    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter || sym == FcitxKey_Tab) {
        if (!state.rawBuffer.empty()) {
            resetReason = sym == FcitxKey_Tab ? "Tab" : "Enter";
            finishDirectComposition(ic, state, "", false);
        }
        forwarded = true;
        ic->forwardKey(pending.rawKey, false, pending.time);
        logPendingResult();
        return;
    }

    if (sym < FcitxKey_space || sym > FcitxKey_asciitilde) {
        if (!state.rawBuffer.empty()) {
            resetReason = "cursor movement";
            finishDirectComposition(ic, state, "", false);
        }
        forwarded = true;
        ic->forwardKey(pending.rawKey, false, pending.time);
        logPendingResult();
        return;
    }

    char ch = static_cast<char>(sym);
    if (!isAsciiWordChar(ch)) {
        if (!state.rawBuffer.empty()) {
            resetReason = "punctuation committed";
            finishDirectComposition(ic, state, std::string(1, ch), true);
        } else {
            forwarded = true;
            ic->forwardKey(pending.rawKey, false, pending.time);
        }
        logPendingResult();
        return;
    }

    auto rawBeforeLocal = state.rawBuffer;
    auto renderedBeforeLocal = state.lastRenderedText;
    auto result = processChar(state, ch);
    state.displayText = result.text;
    ++state.revision;
    state.desiredEventId = pending.eventId;
    state.desiredPhysicalKey = physicalKey;
    consumedAsTelex = true;
    deleteCalled = !renderedBeforeLocal.empty();
    commitCalled = !state.displayText.empty();
    renderDirectCommit(ic, state, physicalKey, rawBeforeLocal, renderedBeforeLocal, pending.eventId);
    logPendingResult();
}

UniikiEngine::ProcessResult UniikiEngine::processChar(UniikiState &state, char ch) const {
    const auto converterStartedAt = monotonicMicros();
    auto current = state.displayText;
    auto nextRaw = state.rawBuffer + ch;
    std::string next;
    try {
        next = evaluateTelex(nextRaw);
    } catch (const std::exception &error) {
        FCITX_ERROR() << "UniikiTrace CONVERTER_EXCEPTION"
                      << " rawBuffer=" << nextRaw
                      << " error=" << error.what()
                      << " fallback=literal";
        dumpTraceRing(state, "CONVERTER_EXCEPTION");
        next = nextRaw;
    } catch (...) {
        FCITX_ERROR() << "UniikiTrace CONVERTER_EXCEPTION"
                      << " rawBuffer=" << nextRaw
                      << " error=unknown fallback=literal";
        dumpTraceRing(state, "CONVERTER_EXCEPTION");
        next = nextRaw;
    }
    const auto converterDuration = monotonicMicros() - converterStartedAt;
    if (converterDuration > CONVERTER_SLOW_MICROS) {
        FCITX_ERROR() << "UniikiTrace CONVERTER_SLOW"
                      << " durationMicros=" << converterDuration
                      << " rawBuffer=" << nextRaw;
        dumpTraceRing(state, "CONVERTER_SLOW");
    }

    state.rawBuffer = nextRaw;

    if (next == current + ch) {
        return {true, false, next, true, true, true};
    }
    return {true, true, next, true, true, true};
}

bool UniikiEngine::canUseDirectCommit(InputContext *ic, const UniikiState &state) const {
    return true;
}

unsigned int UniikiEngine::surroundingDeleteUnits(const std::string &utf8Text) const {
    return static_cast<unsigned int>(utf8::length(normalizeNFC(utf8Text)));
}

void UniikiEngine::renderDirectCommit(InputContext *ic, UniikiState &state,
                                      const std::string &physicalKey,
                                      const std::string &rawBefore,
                                      const std::string &lastRenderedBefore,
                                      uint64_t eventId) const {
    const auto renderStartedAt = monotonicMicros();
    ScopeExit renderHandlerExit([&]() {
        const auto duration = monotonicMicros() - renderStartedAt;
        if (duration > RENDER_HANDLER_SLOW_MICROS) {
            FCITX_ERROR() << "UniikiTrace RENDER_HANDLER_SLOW"
                          << " eventId=" << eventId
                          << " durationMicros=" << duration
                          << " rawBuffer=" << state.rawBuffer
                          << " queueLength=" << state.pendingKeys.size()
                          << " processing=" << state.replacementInProgress;
            dumpTraceRing(state, "RENDER_HANDLER_SLOW");
        }
    });
    auto candidate = normalizeNFC(state.displayText);
    const auto inputVersion = state.revision;
    const auto commitId = ++state.nextCommitId;
    auto [deleteCount, insertText] = replacementDelta(lastRenderedBefore, candidate);
    const auto operation =
        deleteCount > 0
            ? UniikiState::RenderOperation::ReplaceSuffix
            : (insertText.empty()
                   ? UniikiState::RenderOperation::NoVisibleChange
                   : UniikiState::RenderOperation::AppendLiteral);
    const auto oldChars = splitCodePoints(lastRenderedBefore);
    const auto newChars = splitCodePoints(candidate);
    size_t commonLength = 0;
    while (commonLength < oldChars.size() && commonLength < newChars.size() &&
           oldChars[commonLength] == newChars[commonLength]) {
        ++commonLength;
    }
    std::string commonPrefix;
    std::string oldSuffix;
    std::string newSuffix;
    for (size_t i = 0; i < commonLength; ++i) {
        commonPrefix += oldChars[i];
    }
    for (size_t i = commonLength; i < oldChars.size(); ++i) {
        oldSuffix += oldChars[i];
    }
    for (size_t i = commonLength; i < newChars.size(); ++i) {
        newSuffix += newChars[i];
    }
    const auto dBefore = traceDTransform(rawBefore);
    const auto dAfter = traceDTransform(state.rawBuffer);
    bool escapedLiteralCreated = false;
    char escapedLiteralValue = 0;
    std::string activeTransformType = "NONE";
    std::string activeTriggerKey;
    std::string activeTriggerRawIndex = "none";
    std::string cancelTriggerRawIndex = "none";
    if (!rawBefore.empty() &&
        state.rawBuffer.size() == rawBefore.size() + 1) {
        const char first = static_cast<char>(std::tolower(
            static_cast<unsigned char>(rawBefore.back())));
        const char second = static_cast<char>(std::tolower(
            static_cast<unsigned char>(state.rawBuffer.back())));
        const bool isModifier =
            first == 'd' || isToneKey(first) || first == 'w' ||
            first == 'a' || first == 'e' || first == 'o';
        if (first == second && isModifier &&
            lastRenderedBefore != rawBefore) {
            escapedLiteralCreated = true;
            escapedLiteralValue = state.rawBuffer.back();
            activeTriggerKey = std::string(1, rawBefore.back());
            activeTriggerRawIndex =
                std::to_string(rawBefore.size() - 1);
            cancelTriggerRawIndex =
                std::to_string(state.rawBuffer.size() - 1);
            if (first == 'd') {
                activeTransformType = "D_STROKE";
            } else if (isToneKey(first)) {
                activeTransformType = "TONE";
            } else if (first == 'w') {
                activeTransformType = "HORN";
            } else {
                activeTransformType = "CIRCUMFLEX";
            }
        }
    }
    bool escapedWCreated =
        escapedLiteralCreated &&
        std::tolower(static_cast<unsigned char>(escapedLiteralValue)) == 'w';
    std::string matchedVowelCluster;
    std::string firstWRawIndex = "none";
    std::string secondWRawIndex = "none";
    size_t firstWIndex = 0;
    bool hasFirstW = false;
    if (!escapedWCreated && !state.rawBuffer.empty() &&
        std::tolower(static_cast<unsigned char>(
            state.rawBuffer.back())) == 'w' &&
        !candidate.empty() &&
        std::tolower(static_cast<unsigned char>(candidate.back())) == 'w') {
        const auto rawBeforeLower = lowerRaw(rawBefore);
        const auto previousW = rawBeforeLower.rfind('w');
        if (previousW != std::string::npos) {
            const auto throughFirstW = rawBefore.substr(0, previousW + 1);
            if (evaluateTelex(throughFirstW) != throughFirstW) {
                escapedWCreated = true;
                firstWIndex = previousW;
                hasFirstW = true;
                firstWRawIndex = std::to_string(firstWIndex);
                secondWRawIndex =
                    std::to_string(state.rawBuffer.size() - 1);
            }
        }
    }
    if (escapedWCreated) {
        if (!hasFirstW) {
            firstWIndex = rawBefore.size() - 1;
            hasFirstW = true;
            firstWRawIndex = std::to_string(firstWIndex);
            secondWRawIndex =
                std::to_string(state.rawBuffer.size() - 1);
        }
    } else if (!state.rawBuffer.empty() &&
               std::tolower(static_cast<unsigned char>(
                   state.rawBuffer.back())) == 'w' &&
               candidate != state.rawBuffer) {
        firstWIndex = state.rawBuffer.size() - 1;
        hasFirstW = true;
        firstWRawIndex = std::to_string(firstWIndex);
    }
    if (hasFirstW) {
        const auto throughW =
            lowerRaw(state.rawBuffer.substr(0, firstWIndex + 1));
        for (const auto &cluster :
             {std::string("uow"), std::string("uaw"),
              std::string("uw"), std::string("ow"),
              std::string("aw"), std::string("w")}) {
            if (endsWith(throughW, cluster)) {
                matchedVowelCluster = cluster;
                break;
            }
        }
        if (matchedVowelCluster == "w" && endsWith(throughW, "giw")) {
            matchedVowelCluster = "gi+w";
        }
    }
    const bool expandedWToUo =
        !state.rawBuffer.empty() &&
        std::tolower(static_cast<unsigned char>(state.rawBuffer.back())) == 'o' &&
        !rawBefore.empty() &&
        std::tolower(static_cast<unsigned char>(rawBefore.back())) == 'w' &&
        lastRenderedBefore != rawBefore && candidate != state.rawBuffer;
    std::string previousWTransform = "NONE";
    std::string expandedWTransform = "NONE";
    if (expandedWToUo) {
        firstWIndex = rawBefore.size() - 1;
        hasFirstW = true;
        firstWRawIndex = std::to_string(firstWIndex);
        const bool explicitU =
            firstWIndex > 0 &&
            std::tolower(static_cast<unsigned char>(
                state.rawBuffer[firstWIndex - 1])) == 'u';
        matchedVowelCluster = explicitU ? "uwo" : "wo";
        previousWTransform =
            explicitU ? "UW_TO_U_HORN" : "W_TO_U_HORN";
        expandedWTransform =
            explicitU ? "UWO_TO_UO_HORN" : "WO_TO_UO_HORN";
    }
    std::string activeWTransformType = "NONE";
    if (matchedVowelCluster == "uaw") {
        activeWTransformType = "UA_W";
    } else if (matchedVowelCluster == "uow") {
        activeWTransformType = "UO_W";
    } else if (matchedVowelCluster == "uw") {
        activeWTransformType = "U_W";
    } else if (matchedVowelCluster == "ow") {
        activeWTransformType = "O_W";
    } else if (matchedVowelCluster == "aw") {
        activeWTransformType = "A_W";
    } else if (matchedVowelCluster == "gi+w") {
        activeWTransformType = "GI_W";
    } else if (matchedVowelCluster == "w") {
        activeWTransformType = "STANDALONE_W";
    } else if (matchedVowelCluster == "uwo") {
        activeWTransformType = "UWO_W";
    } else if (matchedVowelCluster == "wo") {
        activeWTransformType = "WO_W";
    }
    std::string matchedWRule;
    if (activeWTransformType != "NONE") {
        matchedWRule =
            std::string(escapedWCreated ? "CANCEL_" : "APPLY_") +
            activeWTransformType;
    }
    std::string matchedRawStart = "none";
    std::string matchedRawEnd = "none";
    if (hasFirstW) {
        size_t suffixLength = matchedVowelCluster == "gi+w"
                                  ? 3
                                  : matchedVowelCluster.size();
        if (escapedWCreated) {
            ++suffixLength;
        }
        if (suffixLength <= state.rawBuffer.size()) {
            matchedRawStart =
                std::to_string(state.rawBuffer.size() - suffixLength);
            matchedRawEnd = std::to_string(state.rawBuffer.size());
        }
    }
    const auto toneLexBefore = lexAndReduceToneActions(rawBefore);
    const auto toneLexAfter =
        lexAndReduceToneActions(state.rawBuffer);
    const auto segmentTraceAfter =
        traceActiveSegment(toneLexAfter.baseLetters, candidate);
    const auto &toneBefore = toneLexBefore.history;
    const auto &toneAfter = toneLexAfter.history;
    const char latestRawKey =
        state.rawBuffer.empty()
            ? 0
            : static_cast<char>(std::tolower(
                  static_cast<unsigned char>(state.rawBuffer.back())));
    const bool toneKeyEvent = isToneKey(latestRawKey);
    const bool toneReplacement =
        toneKeyEvent && toneBefore.activeTone != TONE_NONE &&
        toneBefore.activeKey != latestRawKey &&
        toneAfter.activeTone != TONE_NONE;
    const bool toneEscapeCreated =
        toneKeyEvent && toneBefore.activeTone != TONE_NONE &&
        toneBefore.activeKey == latestRawKey &&
        toneAfter.escapedLiteral.has_value();
    const bool canExtendCoda =
        segmentTraceAfter.coda.empty() ||
        isCodaPrefix(lowerRaw(segmentTraceAfter.coda));
    std::string toneTrigger;
    for (char rawChar : state.rawBuffer) {
        const char lowerRawChar = static_cast<char>(
            std::tolower(static_cast<unsigned char>(rawChar)));
        if (isToneKey(lowerRawChar)) {
            toneTrigger.assign(1, lowerRawChar);
        }
    }
    auto capabilities = ic->capabilityFlags();
    const auto &surroundingBefore = ic->surroundingText();
    auto hasSurroundingCapability = capabilities.test(CapabilityFlag::SurroundingText);
    auto surroundingValidBefore = surroundingBefore.isValid();
    auto surroundingTextBefore = surroundingValidBefore ? surroundingBefore.text() : std::string("");
    auto beforeCursor = surroundingValidBefore ? textBeforeCursor(surroundingBefore) : std::string("");
    auto cursorBefore = surroundingValidBefore ? std::to_string(surroundingBefore.cursor())
                                               : std::string("invalid");
    auto anchorBefore = surroundingValidBefore ? std::to_string(surroundingBefore.anchor())
                                               : std::string("invalid");
    const auto selection =
        surroundingValidBefore
            ? replacementSelectionState(
                  surroundingBefore.cursor(), surroundingBefore.anchor(),
                  beforeCursor, lastRenderedBefore)
            : ReplacementSelectionState();
    const bool hasSelection = surroundingValidBefore && selection.hasSelection;
    const bool autocompleteSelection =
        surroundingValidBefore && selection.autocompleteSelection;
    const bool selectionSafe = surroundingValidBefore && selection.safe;
    const auto selectionLength =
        surroundingValidBefore ? selection.length : 0U;
    const auto typedPrefixBefore =
        surroundingValidBefore &&
                surroundingBefore.cursor() >=
                    utf8::length(normalizeNFC(lastRenderedBefore))
            ? lastRenderedBefore
            : std::string();
    const auto autocompleteSuffixBefore =
        autocompleteSelection
            ? textInCodePointRange(
                  surroundingTextBefore, surroundingBefore.cursor(),
                  surroundingBefore.anchor())
            : std::string();
    const auto compositionStart =
        surroundingValidBefore &&
                surroundingBefore.cursor() >=
                    utf8::length(normalizeNFC(lastRenderedBefore))
            ? static_cast<unsigned int>(
                  surroundingBefore.cursor() -
                  utf8::length(normalizeNFC(lastRenderedBefore)))
            : 0U;
    const auto program = ic->program();
    const auto frontend = std::string(ic->frontendName());
    const bool suffixMatches =
        deleteCount == 0 ||
        (surroundingValidBefore &&
         selectionSafe &&
         endsWith(beforeCursor, lastRenderedBefore));
    std::string expectedAfterDelete = beforeCursor;
    if (deleteCount > 0 && suffixMatches) {
        expectedAfterDelete.resize(expectedAfterDelete.size() -
                                   oldSuffix.size());
    }
    const auto expectedAfterCommit = expectedAfterDelete + insertText;
    const auto tracedRendered = evaluateTelex(state.rawBuffer);
    const auto syllableParseTrace = conversionDebugTrace;
    state.replacementInProgress = true;
    state.replacement = UniikiState::ReplacementTransaction();
    auto &replacement = state.replacement;
    replacement.id = eventId;
    replacement.inputVersion = inputVersion;
    replacement.commitId = commitId;
    replacement.startedAtMicros = monotonicMicros();
    replacement.contextGeneration = state.contextGeneration;
    replacement.application = program;
    replacement.frontend = frontend;
    replacement.rawBefore = rawBefore;
    replacement.rawAfter = state.rawBuffer;
    replacement.converterOutput = candidate;
    replacement.oldRendered = lastRenderedBefore;
    replacement.newRendered = candidate;
    replacement.commonPrefix = commonPrefix;
    replacement.oldSuffix = oldSuffix;
    replacement.newSuffix = newSuffix;
    replacement.commitText = insertText;
    replacement.surroundingTextBefore = surroundingTextBefore;
    replacement.beforeCursorBefore = beforeCursor;
    replacement.expectedAfterDelete = expectedAfterDelete;
    replacement.expectedAfterCommit = expectedAfterCommit;
    replacement.expectedAfterAutocompleteStage =
        beforeCursor + insertText;
    replacement.cursorBefore = cursorBefore;
    replacement.anchorBefore = anchorBefore;
    replacement.typedPrefixBefore = typedPrefixBefore;
    replacement.autocompleteSuffixBefore = autocompleteSuffixBefore;
    replacement.compositionStart = compositionStart;
    replacement.selectionLength = selectionLength;
    replacement.autocompleteSelection = autocompleteSelection;
    replacement.deleteOffset = -static_cast<int>(deleteCount);
    replacement.deleteUnits = deleteCount;
    replacement.suffixMatches = suffixMatches;
    replacement.operation = operation;
    ScopeExit replacementCleanup([&]() {
        if (state.replacementInProgress) {
            abortReplacement(ic, state, "scope-exit");
        }
    });
    FCITX_DEBUG() << "UniikiTrace DELETE_UNIT_CHECK"
                 << " eventId=" << eventId
                 << " text=" << lastRenderedBefore
                 << " UTF8_BYTES=" << lastRenderedBefore.size()
                 << " UNICODE_CODEPOINTS=" << utf8::length(normalizeNFC(lastRenderedBefore))
                 << " DELETE_UNITS=" << deleteCount;
    FCITX_DEBUG() << "UniikiTrace REPLACE_BEGIN eventId=" << eventId
                 << " transactionId=" << commitId
                 << " stateVersion=" << inputVersion
                 << " inputVersion=" << inputVersion
                 << " currentVersion=" << state.revision
                 << " commitId=" << commitId
                 << " key=" << physicalKey
                 << " oldRendered=" << lastRenderedBefore
                 << " newCandidate=" << candidate
                 << " surroundingTextBefore=" << surroundingTextBefore
                 << " beforeCursorBefore=" << beforeCursor
                 << " cursorBefore=" << cursorBefore
                 << " anchorBefore=" << anchorBefore
                 << " selectionLength=" << selectionLength
                 << " typedPrefix=" << typedPrefixBefore
                 << " autocompleteSuffix=" << autocompleteSuffixBefore
                 << " compositionStart=" << compositionStart
                 << " replaceStart=" << compositionStart
                 << " replaceLength=" << deleteCount
                 << " capabilities=" << capabilities.toInteger()
                 << " program=" << program
                 << " frontend=" << frontend
                 << " deletionMethod=surrounding-delete-two-phase"
                 << " hasSurroundingTextCapability=" << hasSurroundingCapability
                 << " deleteOffset=" << -static_cast<int>(deleteCount)
                 << " deleteCount=" << deleteCount
                 << " insertText=" << insertText
                 << " operation=" << renderOperationText(operation)
                 << " cursorFromClient=" << cursorBefore
                 << " expectedCursor=unknown"
                 << " queueLength=0"
                 << " replacementInProgress=" << state.replacementInProgress;
    if (autocompleteSelection) {
        FCITX_DEBUG() << "UniikiTrace AUTOCOMPLETE_SELECTION_DETECTED"
                     << " eventId=" << eventId
                     << " application=" << program
                     << " cursor=" << cursorBefore
                     << " anchor=" << anchorBefore
                     << " selectionLength=" << selectionLength
                     << " typedPrefix=" << typedPrefixBefore
                     << " autocompleteSuffix=" << autocompleteSuffixBefore
                     << " compositionStart=" << compositionStart
                     << " ownedPrefixMatches="
                     << boolText(endsWith(beforeCursor, lastRenderedBefore));
    } else if (hasSelection) {
        FCITX_DEBUG() << "UniikiTrace FOREIGN_SELECTION_REJECTED"
                     << " eventId=" << eventId
                     << " cursor=" << cursorBefore
                     << " anchor=" << anchorBefore
                     << " selectionLength=" << selectionLength;
    }
    FCITX_DEBUG() << "UniikiTrace RENDERED_DIFF"
                 << " eventId=" << eventId
                 << " transactionId=" << commitId
                 << " stateVersion=" << inputVersion
                 << " converterOutput=" << candidate
                 << " rawBefore=" << rawBefore
                 << " rawAfter=" << state.rawBuffer
                 << " oldRendered=" << lastRenderedBefore
                 << " newRendered=" << candidate
                 << " commonPrefix=" << commonPrefix
                 << " oldSuffix=" << oldSuffix
                 << " newSuffix=" << newSuffix
                 << " deleteUnits=" << deleteCount
                 << " commitText=" << insertText
                 << " operation=" << renderOperationText(operation)
                 << " visibleTextBefore=" << beforeCursor
                 << " expectedVisibleTextAfter=" << expectedAfterCommit
                 << " dTargetRawIndexBefore="
                 << (dBefore.hasTarget ? std::to_string(dBefore.targetRawIndex)
                                       : std::string("none"))
                 << " dTargetRawIndexAfter="
                 << (dAfter.hasTarget ? std::to_string(dAfter.targetRawIndex)
                                      : std::string("none"))
                 << " dTransformedBefore=" << boolText(dBefore.transformed)
                 << " dTransformedAfter=" << boolText(dAfter.transformed);
    FCITX_DEBUG() << "UniikiTrace MODIFIER_TRANSACTION"
                 << " key=" << physicalKey
                 << " rawBuffer=" << state.rawBuffer
                 << " activeTransformType=" << activeTransformType
                 << " activeTriggerKey=" << activeTriggerKey
                 << " activeTriggerRawIndex=" << activeTriggerRawIndex
                 << " cancelTriggerRawIndex=" << cancelTriggerRawIndex
                 << " escapedLiteralCreated="
                 << boolText(escapedLiteralCreated)
                 << " escapedLiteralValue="
                 << (escapedLiteralCreated
                         ? std::string(1, escapedLiteralValue)
                         : std::string())
                 << " oldRendered=" << lastRenderedBefore
                 << " newRendered=" << candidate
                 << " deleteUnits=" << deleteCount
                 << " commitText=" << insertText;
    FCITX_DEBUG() << "UniikiTrace TONE_TRANSACTION"
                 << " key=" << physicalKey
                 << " rawBuffer=" << state.rawBuffer
                 << " tokens="
                 << tokenOwnershipText(toneLexAfter.tokens)
                 << " baseLetters=" << toneLexAfter.baseLetters
                 << " parsedOnset=" << segmentTraceAfter.onset
                 << " parsedVowel=" << segmentTraceAfter.vowel
                 << " candidateCoda=" << segmentTraceAfter.coda
                 << " canExtendCoda=" << boolText(canExtendCoda)
                 << " oldTone=" << toneName(toneBefore.activeTone)
                 << " newTone=" << toneName(toneAfter.activeTone)
                 << " activeTone=" << toneName(toneAfter.activeTone)
                 << " toneTriggerIndex="
                 << (toneAfter.activeTone == TONE_NONE
                         ? std::string("none")
                         : std::to_string(
                               toneAfter.activeTriggerRawIndex))
                 << " replacement=" << boolText(toneReplacement)
                 << " escapedLiteralCreated="
                 << boolText(toneEscapeCreated)
                 << " escapeTriggered="
                 << boolText(
                        !toneLexAfter.escapedLiteralIndexes.empty())
                 << " modifierOwner="
                 << (toneAfter.consumedModifierIndexes.empty()
                         ? std::string("none")
                         : std::to_string(
                               toneAfter.consumedModifierIndexes.front()))
                 << " removedModifierIndexes="
                 << rawIndexesText(
                        toneAfter.consumedModifierIndexes)
                 << " escapedLiteralIndexes="
                 << rawIndexesText(
                        toneLexAfter.escapedLiteralIndexes)
                 << " consumedToneIndexes="
                 << rawIndexesText(
                        toneAfter.consumedModifierIndexes)
                 << " rendered=" << candidate;
    FCITX_DEBUG() << "UniikiTrace SYLLABLE_PARSE"
                 << " rawBuffer=" << state.rawBuffer
                 << " tokens=" << syllableParseTrace.tokens
                 << " consumedModifierIndexes="
                 << rawIndexesText(
                        syllableParseTrace.consumedModifierIndexes)
                 << " parsedOnset=" << syllableParseTrace.parsedOnset
                 << " parsedVowelNucleus="
                 << syllableParseTrace.parsedVowelNucleus
                 << " parsedCoda=" << syllableParseTrace.parsedCoda
                 << " activeTone="
                 << toneName(syllableParseTrace.activeTone)
                 << " toneTargetIndex="
                 << syllableParseTrace.toneTargetIndex
                 << " rendered=" << candidate
                 << " fallbackReason="
                 << (syllableParseTrace.candidate.empty()
                         ? std::string("pre-parse-raw-policy")
                         : syllableParseTrace.candidate == tracedRendered
                               ? std::string("none")
                               : std::string("invalid-vietnamese-syllable"));
    FCITX_DEBUG() << "UniikiTrace W_TRANSACTION"
                 << " rawBefore=" << rawBefore
                 << " key=" << physicalKey
                 << " rawAfter=" << state.rawBuffer
                 << " converterInput=" << state.rawBuffer
                 << " candidateSuffixes=uoww,uaww,aww,oww,uww,ww|uow,uwo,wo,uaw,uw,ow,aw,w"
                 << " matchedRule=" << matchedWRule
                 << " matchedRawStart=" << matchedRawStart
                 << " matchedRawEnd=" << matchedRawEnd
                 << " matchedVowelCluster=" << matchedVowelCluster
                 << " activeWTransform=" << activeWTransformType
                 << " previousTransform=" << previousWTransform
                 << " expandedTransform=" << expandedWTransform
                 << " firstWRawIndex=" << firstWRawIndex
                 << " secondWRawIndex=" << secondWRawIndex
                 << " escapedLiteralCreated="
                 << boolText(escapedWCreated)
                 << " parsedOnset=" << segmentTraceAfter.onset
                 << " renderedVowel="
                 << (expandedWToUo ? std::string("ươ")
                                   : segmentTraceAfter.vowel)
                 << " toneTrigger=" << toneTrigger
                 << " coda=" << segmentTraceAfter.coda
                 << " rendered=" << candidate
                 << " converterOutput=" << candidate
                 << " oldRendered=" << lastRenderedBefore
                 << " newRendered=" << candidate
                 << " deleteUnits=" << deleteCount
                 << " commitText=" << insertText;

    if (deleteCount > 0 && !suffixMatches) {
        FCITX_DEBUG() << "UniikiTrace SURROUNDING_MISMATCH"
                     << " eventId=" << eventId
                     << " expectedSuffix=" << lastRenderedBefore
                     << " actualBeforeCursor=" << beforeCursor
                     << " surroundingValid=" << surroundingValidBefore
                     << " deleteSkipped=1"
                     << " commitSkipped=1"
                     << " replacementProceedingFromOwnedState=0"
                     << " transactionId=" << commitId
                     << " stateVersion=" << inputVersion
                     << " physicalKey=" << physicalKey;

        // The event is already accepted, so preserve it exactly once instead
        // of leaving the context locked or silently dropping it.
        if (physicalKey.size() == 1 &&
            isAsciiWordChar(physicalKey.front())) {
            InternalCommitGuard internalCommit(state);
            ic->commitString(physicalKey);
            FCITX_DEBUG() << "UniikiTrace MISMATCH_LITERAL_FALLBACK"
                         << " eventId=" << eventId
                         << " key=" << physicalKey
                         << " consumed=1 forwarded=0 committed=1";
        }
        ic->surroundingText().invalidate();
        abortReplacement(ic, state, "surrounding-mismatch");
        return;
    }

    if (deleteCount > 0 && autocompleteSelection) {
        // Materialize/collapse the inline selection, then issue the remaining
        // ordered edits synchronously from Fcitx's point of view. No client
        // callback is required to release this transaction.
        {
            InternalCommitGuard internalCommit(state);
            ic->commitString(insertText);
            const auto stagedDelete =
                surroundingDeleteUnits(oldSuffix) +
                surroundingDeleteUnits(insertText);
            if (stagedDelete > 0) {
                auto capabilities = ic->capabilityFlags();
                if (capabilities.test(CapabilityFlag::SurroundingText)) {
                    ic->deleteSurroundingText(-static_cast<int>(stagedDelete),
                                              stagedDelete);
                } else {
                    for (uint32_t i = 0; i < stagedDelete; ++i) {
                        ic->forwardKey(Key(FcitxKey_BackSpace));
                    }
                }
            }
            if (!insertText.empty()) {
                ic->commitString(insertText);
            }
        }
        FCITX_DEBUG() << "UniikiTrace AUTOCOMPLETE_STAGE_SENT"
                     << " eventId=" << eventId
                     << " transactionId=" << commitId
                     << " method=commit-stage"
                     << " typedPrefix=" << typedPrefixBefore
                     << " autocompleteSuffix=" << autocompleteSuffixBefore
                     << " physicalKeyForwarded=0"
                     << " stagedCommitText=" << insertText
                     << " expectedAfterAutocompleteStage="
                     << replacement.expectedAfterAutocompleteStage
                     << " finalCommitSent=1"
                     << " waitingForSurroundingUpdate=0";
        completeReplacement(ic, state, "autocomplete-dispatched");
        return;
    }

    if (deleteCount > 0) {
        {
            InternalCommitGuard internalCommit(state);
            // Fcitx defines offset and size in UCS-4 code points. The calls
            // are ordered requests; a surrounding update is verification,
            // never an acknowledgement gate.
            auto capabilities = ic->capabilityFlags();
            if (capabilities.test(CapabilityFlag::SurroundingText)) {
                ic->deleteSurroundingText(replacement.deleteOffset, deleteCount);
            } else {
                for (uint32_t i = 0; i < deleteCount; ++i) {
                    ic->forwardKey(Key(FcitxKey_BackSpace));
                }
            }
            if (!insertText.empty()) {
                ic->commitString(insertText);
            }
        }
        FCITX_DEBUG() << "UniikiTrace DELETE_SENT eventId=" << eventId
                     << " transactionId=" << commitId
                     << " deleteOffset=" << replacement.deleteOffset
                     << " deleteSize=" << deleteCount
                     << " deleteUnit=UCS4_CODEPOINT"
                     << " method=surrounding-delete"
                     << " commitSent=" << boolText(!insertText.empty())
                     << " commitText=" << insertText
                     << " waitingForSurroundingUpdate=0";
        completeReplacement(ic, state, "replace-dispatched");
        return;
    }

    {
        InternalCommitGuard internalCommit(state);
        if (!insertText.empty()) {
            ic->commitString(insertText);
        }
    }
    FCITX_DEBUG() << "UniikiTrace COMMIT_SENT eventId=" << eventId
                 << " transactionId=" << commitId
                 << " commitText=" << insertText
                 << " operation=" << renderOperationText(operation)
                 << " afterDeleteAck=not-required"
                 << " waitingForSurroundingUpdate=0";
    completeReplacement(ic, state,
                        operation == UniikiState::RenderOperation::AppendLiteral
                            ? "append-dispatched"
                            : "no-visible-change");
}

void UniikiEngine::fallbackCommitRaw(InputContext *ic, UniikiState &state, const std::string &text,
                                     const std::string &reason) const {
    FCITX_DEBUG() << "UniikiTrace RAW_FALLBACK"
                 << " reason=" << reason
                 << " text=" << text
                 << " rawBuffer=" << state.rawBuffer
                 << " renderedText=" << state.lastRenderedText
                 << " handledByEngine=1"
                 << " consumed=1"
                 << " forwardedToClient=0"
                 << " noPreeditApiCalled=1";
    InternalCommitGuard internalCommit(state);
    ic->commitString(normalizeNFC(text));
    state.reset();
}

void UniikiEngine::finishDirectComposition(InputContext *ic, UniikiState &state,
                                           const std::string &boundary,
                                           bool commitBoundary) const {
    auto recoverRawBuffer = boundary == " " ? state.rawBuffer : std::string("");
    auto recoverRenderedText = boundary == " " ? state.lastRenderedText : std::string("");
    if (commitBoundary && !boundary.empty()) {
        InternalCommitGuard internalCommit(state);
        ic->commitString(normalizeNFC(boundary));
    }
    ic->surroundingText().invalidate();
    state.reset();
    state.recoverRawBuffer = recoverRawBuffer;
    state.recoverRenderedText = recoverRenderedText;
    state.recoverSuffix = boundary;
}

static std::string evaluateRawEscaped(const std::string &raw) {
    if (raw.empty()) return "";
    static const std::unordered_set<char> escapedSet = {
        'd', 'w', 's', 'f', 'r', 'x', 'j', 'z'
    };
    std::string res;
    size_t i = 0;
    size_t n = raw.size();
    while (i < n) {
        char k = raw[i];
        char lk = static_cast<char>(std::tolower(static_cast<unsigned char>(k)));
        if (i + 1 < n && escapedSet.count(lk) &&
            static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i + 1]))) == lk) {
            res.push_back(k);
            i += 2;
        } else {
            res.push_back(k);
            i += 1;
        }
    }
    return res;
}

std::string UniikiEngine::evaluateTelex(const std::string &raw) {
    conversionDebugTrace = {};
    conversionDebugTrace.rawBuffer = raw;
    if (raw.empty()) {
        return "";
    }
    if (raw.size() > MAX_RAW_CONVERSION_LENGTH) {
        conversionDebugTrace.fallbackReason = "raw-length-safety-limit";
        return raw;
    }
    const auto rawLower = lowerRaw(raw);
    if (shouldProtectRawWord(raw, rawLower) ||
        isLikelyEnglishRawShape(rawLower) ||
        hasInvalidWoContinuation(rawLower) ||
        (!isDoubleWEscape(raw) && raw.size() >= 2 && hasUpperAfterFirst(raw)) ||
        std::any_of(raw.begin(), raw.end(), [](char ch) {
            return std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' ||
                   ch == '/' || ch == '@' || ch == ':' || ch == '-';
        })) {
        return evaluateRawEscaped(raw);
    }

    const auto dState = traceDTransform(raw);
    if (!dState.hasTarget ||
        (!dState.transformed && !dState.cancelled &&
         dState.transformTriggerRawIndex == 0)) {
        auto rendered = evaluateTelexCore(raw, true);
        if (rendered != raw) {
            return rendered;
        }

        // Preserve a confirmed Vietnamese prefix when a run of raw vowels no
        // longer belongs to the same syllable. This is deliberately tried only
        // after the whole-word candidate failed, and only for a homogeneous
        // a/e/o literal suffix following a tone action (oroooo -> ỏ + oooo).
        for (size_t modifier = 1; modifier + 1 < raw.size(); ++modifier) {
            const char tone = lowerAscii(raw[modifier]);
            if (!isToneKey(tone)) {
                continue;
            }
            const char suffixVowel = lowerAscii(raw[modifier + 1]);
            if (suffixVowel != 'a' && suffixVowel != 'e' &&
                suffixVowel != 'o') {
                continue;
            }
            const bool homogeneousLiteralSuffix =
                std::all_of(raw.begin() + static_cast<std::ptrdiff_t>(modifier + 1),
                            raw.end(), [suffixVowel](char ch) {
                                return lowerAscii(ch) == suffixVowel;
                            });
            if (!homogeneousLiteralSuffix) {
                continue;
            }
            const auto prefixRaw = raw.substr(0, modifier + 1);
            if (!parseShapeCandidate(prefixRaw).shapeActions.empty()) {
                // The literal-suffix recovery is for a tone-confirmed plain
                // vowel (oroooo -> ỏoooo), not a second chance to keep a
                // partial aa/ee/oo transform from an invalid whole word.
                continue;
            }
            const auto prefixRendered = evaluateTelexCore(prefixRaw, true);
            if (prefixRendered != prefixRaw && containsNonAscii(prefixRendered)) {
                return prefixRendered + raw.substr(modifier + 1);
            }
        }
        return rendered;
    }

    std::string dResolvedRaw;
    dResolvedRaw.reserve(raw.size());
    size_t dOrdinal = 0;
    std::optional<EscapedLiteral> escapedD;
    for (size_t i = 0; i < raw.size(); ++i) {
        const bool isD =
            std::tolower(static_cast<unsigned char>(raw[i])) == 'd';
        if (i < dState.targetRawIndex || !isD) {
            dResolvedRaw.push_back(raw[i]);
            continue;
        }

        ++dOrdinal;
        if (dOrdinal == 1) {
            dResolvedRaw.push_back(raw[i]);
        } else if (dOrdinal == 2) {
            // The modifier key is not rendered while it creates đ.
        } else if (dOrdinal == 3) {
            escapedD = EscapedLiteral{
                raw[i], dState.transformTriggerRawIndex, i};
            dResolvedRaw.push_back(escapedD->value);
        } else {
            // The escape pair is already protected. Later d keys are ordinary
            // literals and cannot be swallowed by the closed transaction.
            dResolvedRaw.push_back(raw[i]);
        }
    }

    auto rendered = evaluateTelexCore(dResolvedRaw, false);
    if (dState.transformed) {
        const char target =
            std::isupper(static_cast<unsigned char>(raw[dState.targetRawIndex]))
                ? 'D'
                : 'd';
        const auto position = rendered.find(target);
        if (position != std::string::npos) {
            rendered.replace(position, 1, target == 'D' ? "Đ" : "đ");
        }
    }
    return rendered;
}

std::string UniikiEngine::evaluateTelexCore(const std::string &raw,
                                            bool enableDTransform) {
    static thread_local size_t recursionDepth = 0;
    ++recursionDepth;
    ScopeExit recursionExit([&]() { --recursionDepth; });
    if (raw.size() > MAX_RAW_CONVERSION_LENGTH) {
        return raw;
    }
    if (recursionDepth > raw.size() + 8) {
        FCITX_ERROR() << "UniikiTrace PARSER_STATE_CYCLE"
                      << " recursionDepth=" << recursionDepth
                      << " rawBuffer=" << raw;
        return raw;
    }
    if (raw.empty()) {
        return "";
    }
    auto rawLower = lowerRaw(raw);
    if (shouldProtectRawWord(raw, rawLower)) {
        return evaluateRawEscaped(raw);
    }

    if (!isDoubleWEscape(raw) && raw.size() >= 2 && hasUpperAfterFirst(raw)) {
        return raw;
    }

    if (std::any_of(raw.begin(), raw.end(), [](char ch) {
            return std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '/' ||
                   ch == '@' || ch == ':' || ch == '-';
        })) {
        return raw;
    }

    auto segments = splitRawSegments(raw);
    if (segments.size() > 1) {
        std::string rendered;
        bool previousSegmentTransformed = false;
        bool hasUnconfirmedShapeOnlySegment = false;
        for (size_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
            const auto &range = segments[segmentIndex];
            auto segmentRaw = raw.substr(range.start, range.end - range.start);
            std::string segmentRendered;

            size_t leadingD = 0;
            while (leadingD < segmentRaw.size() &&
                   std::tolower(static_cast<unsigned char>(segmentRaw[leadingD])) == 'd') {
                ++leadingD;
            }
            if (enableDTransform && segmentIndex > 0 &&
                !previousSegmentTransformed && leadingD >= 2) {
                segmentRendered.append(segmentRaw, 1, leadingD - 1);
                segmentRendered +=
                    evaluateTelexCore(segmentRaw.substr(leadingD), enableDTransform);
            } else {
                segmentRendered = evaluateTelexCore(segmentRaw, enableDTransform);
            }

            rendered += segmentRendered;
            previousSegmentTransformed = containsNonAscii(segmentRendered);

            const auto shape = parseShapeCandidate(segmentRaw, range.start);
            if (segmentRendered != segmentRaw &&
                !shape.shapeActions.empty()) {
                const auto segmentTone = lexAndReduceToneActions(segmentRaw);
                const bool hasToneConfirmation =
                    !segmentTone.history.consumedModifierIndexes.empty();
                const auto lowerSegment = lowerRaw(segmentRaw);
                const bool hasWConfirmation =
                    lowerSegment.find('w') != std::string::npos;
                const bool hasDConfirmation = startsWith(lowerSegment, "dd");
                if (!hasToneConfirmation && !hasWConfirmation &&
                    !hasDConfirmation) {
                    hasUnconfirmedShapeOnlySegment = true;
                }
            }
        }
        if (hasUnconfirmedShapeOnlySegment) {
            // Atomic word-level fallback: never return a hybrid such as
            // delêt/deletê when a shape-only interpretation appeared inside
            // one of several syllable-like segments.
            return evaluateRawEscaped(raw);
        }
        return rendered;
    }

    // Pipeline stage 1-3: lex ownership and reduce all tone actions before
    // onset/nucleus/coda parsing. Tone modifiers never enter letterRaw.
    auto toneLex = lexAndReduceToneActions(raw);
    const auto &letterRaw = toneLex.baseLetters;
    auto shapeCandidate = parseShapeCandidate(raw);
    conversionDebugTrace.consumedModifierIndexes =
        toneLex.history.consumedModifierIndexes;
    conversionDebugTrace.activeTone = toneLex.history.activeTone;
    std::vector<size_t> letterToRaw;
    letterToRaw.reserve(letterRaw.size());
    for (const auto &token : toneLex.tokens) {
        if (token.ownership != RawKeyOwnership::ToneModifier) {
            letterToRaw.push_back(token.rawIndex);
        }
    }
    auto ownsShapeAction = [&](size_t triggerLetterIndex,
                               size_t targetLetterIndex,
                               bool lateAfterCoda) {
        if (triggerLetterIndex >= letterToRaw.size() ||
            targetLetterIndex >= letterToRaw.size()) {
            return false;
        }
        return std::any_of(
            shapeCandidate.shapeActions.begin(),
            shapeCandidate.shapeActions.end(),
            [&](const ShapeAction &action) {
                return action.triggerRawIndex ==
                           letterToRaw[triggerLetterIndex] &&
                       action.targetVowelRawIndex ==
                           letterToRaw[targetLetterIndex] &&
                       action.lateAfterCoda == lateAfterCoda;
            });
    };
    auto ownsLateShapeAction = [&](size_t triggerLetterIndex,
                                   bool lateAfterCoda) {
        if (triggerLetterIndex >= letterToRaw.size()) {
            return false;
        }
        return std::any_of(
            shapeCandidate.shapeActions.begin(),
            shapeCandidate.shapeActions.end(),
            [&](const ShapeAction &action) {
                return action.triggerRawIndex ==
                           letterToRaw[triggerLetterIndex] &&
                       action.lateAfterCoda == lateAfterCoda;
            });
    };
    std::vector<Cell> cells;
    std::vector<EscapedLiteral> escapedLiterals =
        toneLex.escapedLiterals;
    int activeTone = toneLex.history.activeTone;
    bool wModifierActive = false;
    bool wGeneratedVowel = false;
    bool wEscaped = false;
    char wModifierKey = 0;
    size_t wModifierRawIndex = 0;
    std::vector<std::pair<size_t, int>> wModifiedMarks;

    for (size_t i = 0; i < letterRaw.size(); ++i) {
        char ch = letterRaw[i];
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        bool expandsWToUo = false;
        if (wModifierActive && isRawVowel(lower)) {
            bool extendsWAction = false;
            if ((lower == 'a' || lower == 'o') &&
                wModifiedMarks.size() == 1) {
                const auto modifiedIndex = wModifiedMarks.front().first;
                extendsWAction =
                    modifiedIndex < cells.size() &&
                    isVowelCell(cells[modifiedIndex]) &&
                    cells[modifiedIndex].base == 'u';
                expandsWToUo = extendsWAction && lower == 'o';
            }
            if (!extendsWAction) {
                // The previous w transform remains rendered but its action is
                // complete. A later w may start a different longest-cluster
                // transform (for example uw + o + w → ươ).
                wModifierActive = false;
                wGeneratedVowel = false;
                wModifiedMarks.clear();
            }
        }

        if (expandsWToUo) {
            const auto newIndex = cells.size();
            cells.push_back(vowelCell(
                'o', 3, TONE_NONE,
                std::isupper(static_cast<unsigned char>(ch))));
            wModifiedMarks.push_back({newIndex, 0});
            continue;
        }

        if (lower == 'd') {
            if (!enableDTransform) {
                cells.push_back(rawCell(ch));
                continue;
            }
            if (i == 0) {
                size_t runEnd = i;
                while (runEnd < letterRaw.size() &&
                       std::tolower(static_cast<unsigned char>(letterRaw[runEnd])) == 'd') {
                    ++runEnd;
                }
                size_t runLength = runEnd - i;
                if (runLength % 2 == 0) {
                    cells.push_back(
                        strokeDCell(std::isupper(static_cast<unsigned char>(letterRaw.front()))));
                } else {
                    cells.push_back(rawCell(letterRaw.front()));
                }
                for (size_t literal = 0; literal < (runLength - 1) / 2; ++literal) {
                    cells.push_back(rawCell(letterRaw[runLength - 1 - literal]));
                }
                i = runEnd - 1;
                continue;
            }
            cells.push_back(rawCell(ch));
            continue;
        }

        if ((lower == 'a' || lower == 'e' || lower == 'o') &&
            i + 2 < letterRaw.size() &&
            std::tolower(static_cast<unsigned char>(letterRaw[i + 1])) == lower &&
            std::tolower(static_cast<unsigned char>(letterRaw[i + 2])) == lower) {
            cells.push_back(rawCell(ch));
            cells.push_back(rawCell(letterRaw[i + 2]));
            i += 2;
            continue;
        }

        if ((lower == 'a' || lower == 'e' || lower == 'o') &&
            i + 1 < letterRaw.size() &&
            std::tolower(static_cast<unsigned char>(letterRaw[i + 1])) == lower &&
            ownsShapeAction(i + 1, i, false)) {
            if (lower == 'o' && !cells.empty() && isVowelCell(cells.back()) &&
                cells.back().base == 'u' && cells.back().mark == 3) {
                cells.push_back(vowelCell('o', 3));
            } else {
                cells.push_back(vowelCell(lower, 1));
            }
            ++i;
            continue;
        }

        if (lower == 'a' || lower == 'e' || lower == 'o') {
            bool appliedLateMark = false;
            bool seenVowel = false;
            size_t trailingConsonants = 0;
            for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
                if (!isVowelCell(*it)) {
                    if (!seenVowel && isRawConsonant(*it)) {
                        ++trailingConsonants;
                        continue;
                    }
                    break;
                }
                seenVowel = true;
                if (it->base == lower && it->mark == 0) {
                    if (ownsLateShapeAction(
                            i, trailingConsonants > 0)) {
                        it->mark = 1;
                        appliedLateMark = true;
                    }
                    break;
                }
            }
            if (appliedLateMark) {
                continue;
            }
        }

        if (lower == 'w') {
            if (wModifierActive) {
                escapedLiterals.push_back(
                    {wModifierKey, wModifierRawIndex, i});
                if (wGeneratedVowel) {
                    const auto generatedIndex =
                        wModifiedMarks.front().first;
                    const bool extendedGeneratedNucleus =
                        generatedIndex + 1 < cells.size() &&
                        std::any_of(
                            cells.begin() +
                                static_cast<std::ptrdiff_t>(generatedIndex + 1),
                            cells.end(), isVowelCell);
                    if (extendedGeneratedNucleus) {
                        // Standalone w has implicit source u. Once a following
                        // vowel extends it (w+a → ưa), cancellation restores
                        // that source before emitting the escaped literal w.
                        cells[generatedIndex] =
                            vowelCell('u', 0, TONE_NONE);
                        for (size_t markIndex = 1;
                             markIndex < wModifiedMarks.size(); ++markIndex) {
                            const auto &[index, previousMark] =
                                wModifiedMarks[markIndex];
                            cells[index].mark = previousMark;
                        }
                    } else {
                        cells.erase(
                            cells.begin() +
                            static_cast<std::ptrdiff_t>(generatedIndex));
                    }
                } else {
                    for (const auto &[index, previousMark] : wModifiedMarks) {
                        cells[index].mark = previousMark;
                    }
                }
                bool applyNang = activeTone == TONE_NONE && shouldApplyNangOnWEscape(cells);
                cells.push_back(rawCell(
                    applyNang ? 'w' : escapedLiterals.back().value));
                if (applyNang) {
                    activeTone = TONE_NANG;
                }
                wModifierActive = false;
                wGeneratedVowel = false;
                wModifiedMarks.clear();
                wEscaped = true;
                continue;
            }
            // In gi, the raw i belongs to the onset. A following w starts the
            // actual vowel nucleus with ư instead of trying to modify that i.
            if (cells.size() == 2 &&
                cells[0].kind == Cell::Kind::Raw &&
                lowerAscii(cells[0].raw) == 'g' &&
                isVowelCell(cells[1]) && cells[1].base == 'i' &&
                cells[1].mark == 0) {
                wModifiedMarks = {{cells.size(), 0}};
                cells.push_back(vowelCell(
                    'u', 3, TONE_NONE,
                    std::isupper(static_cast<unsigned char>(ch))));
                wModifierActive = true;
                wGeneratedVowel = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }
            const size_t nucleusStartForW = firstNucleusCellIndex(cells);
            if (endsWithBases(cells, "uoi") && cells.size() - 3 >= nucleusStartForW) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 3, cells[cells.size() - 3].mark},
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                };
                cells[cells.size() - 3].mark = 3;
                cells[cells.size() - 2].mark = 3;
                wModifierActive = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }

            // Check if cells contains 'u' and 'o' sequence (with optional coda consonants/tones)
            ssize_t uCellIdx = -1;
            ssize_t oCellIdx = -1;
            const size_t nucStart = firstNucleusCellIndex(cells);
            for (ssize_t idx = static_cast<ssize_t>(cells.size()) - 1; idx >= static_cast<ssize_t>(nucStart); --idx) {
                if (isVowelCell(cells[idx])) {
                    if (cells[idx].base == 'o') {
                        oCellIdx = idx;
                    } else if (cells[idx].base == 'u' && oCellIdx != -1) {
                        bool onlyCodaBetween = true;
                        for (ssize_t k = idx + 1; k < oCellIdx; ++k) {
                            if (isVowelCell(cells[k])) {
                                onlyCodaBetween = false;
                                break;
                            }
                        }
                        if (onlyCodaBetween) {
                            uCellIdx = idx;
                            break;
                        } else {
                            oCellIdx = -1;
                        }
                    }
                }
            }

            if (uCellIdx != -1 && oCellIdx != -1) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {static_cast<size_t>(uCellIdx), cells[uCellIdx].mark},
                    {static_cast<size_t>(oCellIdx), cells[oCellIdx].mark},
                };
                cells[uCellIdx].mark = 3;
                cells[oCellIdx].mark = 3;
                wModifierActive = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }

            if (endsWithBases(cells, "ua") && cells.size() - 2 >= nucleusStartForW) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                    {cells.size() - 1, cells[cells.size() - 1].mark},
                };
                cells[cells.size() - 2].mark = 3;
                cells[cells.size() - 1].mark = 0;
                wModifierActive = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }
            if (endsWithBases(cells, "ua") && cells.size() - 2 >= nucleusStartForW) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                    {cells.size() - 1, cells[cells.size() - 1].mark},
                };
                cells[cells.size() - 2].mark = 3;
                cells[cells.size() - 1].mark = 0;
                wModifierActive = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }
            bool hookApplied = false;
            const size_t nucleusStart = firstNucleusCellIndex(cells);
            for (size_t index = cells.size(); index > nucleusStart; --index) {
                auto &cell = cells[index - 1];
                if (isVowelCell(cell) && cell.mark == 0) {
                    if (cell.base == 'a') {
                        wModifiedMarks = {{index - 1, cell.mark}};
                        cell.mark = 2;
                        hookApplied = true;
                        break;
                    }
                    if (cell.base == 'o' || cell.base == 'u') {
                        wModifiedMarks = {{index - 1, cell.mark}};
                        cell.mark = 3;
                        hookApplied = true;
                        break;
                    }
                }
            }
            if (hookApplied) {
                wGeneratedVowel = false;
                wModifierActive = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }
            if (!hasVowel(cells)) {
                wModifiedMarks = {{cells.size(), 0}};
                cells.push_back(vowelCell('u', 3, TONE_NONE, std::isupper(static_cast<unsigned char>(ch))));
                wModifierActive = true;
                wGeneratedVowel = true;
                wModifierKey = ch;
                wModifierRawIndex = i;
                continue;
            }
            if (i + 1 < letterRaw.size() &&
                std::tolower(static_cast<unsigned char>(letterRaw[i + 1])) == 'w') {
                cells.push_back(rawCell(ch));
                ++i;
                continue;
            }
        }

        if (isToneKey(lower)) {
            // Only an escaped tone literal can remain in letterRaw.
            cells.push_back(rawCell(ch));
            continue;
        }

        if (isRawVowel(lower)) {
            cells.push_back(vowelCell(lower, 0, TONE_NONE, std::isupper(static_cast<unsigned char>(ch))));
        } else {
            cells.push_back(rawCell(ch));
        }
    }

    if (activeTone != TONE_NONE) {
        const auto preToneParts = parseCellSyllableParts(cells);
        std::string preToneBases;
        for (const auto index : preToneParts.nucleusIndexes) {
            preToneBases.push_back(cells[index].base);
        }
        if (preToneBases == "uo" && !preToneParts.coda.empty()) {
            bool plainUO = true;
            for (const auto index : preToneParts.nucleusIndexes) {
                if (cells[index].mark != 0) {
                    plainUO = false;
                    break;
                }
            }
            if (plainUO) {
                for (const auto index : preToneParts.nucleusIndexes) {
                    cells[index].mark = 3;
                }
            }
        }
        if (lowerRaw(preToneParts.onset) == "qu" && preToneBases == "ay" &&
            preToneParts.nucleusIndexes.size() == 2) {
            cells[preToneParts.nucleusIndexes.front()].mark = 1;
        }
    }
    conversionDebugTrace.cellsBeforeTone = cellsText(cells);
    const auto parsedParts = parseCellSyllableParts(cells);
    conversionDebugTrace.parsedOnset = parsedParts.onset;
    conversionDebugTrace.parsedVowelNucleus = parsedParts.nucleus;
    conversionDebugTrace.parsedCoda = parsedParts.coda;
    if (activeTone != TONE_NONE) {
        const auto toneTarget = applyToneToCells(cells, activeTone);
        if (toneTarget) {
            conversionDebugTrace.toneTargetIndex = std::to_string(*toneTarget);
        }
    }
    shapeCandidate.validVietnamese = isValidVietnameseSyllableCells(cells);
    auto candidate = cellsText(cells);
    conversionDebugTrace.candidate = candidate;
    conversionDebugTrace.validVietnamese = shapeCandidate.validVietnamese;
    if (candidate != raw && shapeCandidate.validVietnamese) {
        for (const auto &action : shapeCandidate.shapeActions) {
            conversionDebugTrace.consumedModifierIndexes.push_back(
                action.triggerRawIndex);
        }
        for (auto &token : toneLex.tokens) {
            const bool shapeTrigger = std::any_of(
                shapeCandidate.shapeActions.begin(),
                shapeCandidate.shapeActions.end(),
                [&token](const ShapeAction &action) {
                    return action.triggerRawIndex == token.rawIndex;
                });
            const bool wTrigger = lowerAscii(token.value) == 'w' &&
                                  !wEscaped;
            if (shapeTrigger || wTrigger) {
                token.ownership = RawKeyOwnership::LetterTransformTrigger;
                conversionDebugTrace.consumedModifierIndexes.push_back(
                    token.rawIndex);
            }
        }
        std::sort(conversionDebugTrace.consumedModifierIndexes.begin(),
                  conversionDebugTrace.consumedModifierIndexes.end());
        conversionDebugTrace.consumedModifierIndexes.erase(
            std::unique(conversionDebugTrace.consumedModifierIndexes.begin(),
                        conversionDebugTrace.consumedModifierIndexes.end()),
            conversionDebugTrace.consumedModifierIndexes.end());
    }
    conversionDebugTrace.tokens = tokenOwnershipText(toneLex.tokens);
    bool rawIsDRun = !raw.empty() &&
                     std::all_of(raw.begin(), raw.end(), [](char value) {
                         return std::tolower(static_cast<unsigned char>(value)) == 'd';
                     });

    if (candidate != raw && !shapeCandidate.shapeActions.empty() &&
        !hasVietnameseMarkedCell(cells)) {
        // A planned shape action that did not survive parsing must fall back
        // with all of its raw keys.  This prevents mixed ownership paths such
        // as cheese -> chee where a later tone-like letter was swallowed.
        return evaluateRawEscaped(raw);
    }
    if (candidate != raw && activeTone != TONE_NONE && !hasVowel(cells)) {
        // A tone action cannot own a word whose shape escape left no vowel
        // cell. Restore the complete raw transaction (for example cheese),
        // rather than leaking a partially collapsed ee/oo sequence.
        return evaluateRawEscaped(raw);
    }
    if (!rawIsDRun && !wEscaped && !wGeneratedVowel && candidate != raw && hasVietnameseMarkedCell(cells) &&
        !shapeCandidate.validVietnamese &&
        activeTone == TONE_NONE && !isPotentialVietnamesePrefixCells(cells)) {
        if (!toneLex.escapedLiteralIndexes.empty()) {
            // Reconstruct from ownership: clear the applied tone, retain
            // literal/escaped cells, and never resurrect consumed modifiers
            // by returning the original raw buffer.
            applyToneToCells(cells, TONE_NONE);
            return cellsText(cells);
        }
        return evaluateRawEscaped(raw);
    }
    return candidate;
}

std::string UniikiEngine::evaluateTelexForTest(const std::string &raw) {
    return evaluateTelex(raw);
}

UniikiEngine::ConversionResult
UniikiEngine::conversionWithProvenance(const std::string &raw) {
    ConversionResult result;
    result.rendered = normalizeNFC(evaluateTelex(raw));
    if (raw.empty() || result.rendered.empty()) {
        return result;
    }

    struct IndexedRawKey {
        char value;
        size_t originalIndex;
    };
    std::vector<IndexedRawKey> remaining;
    remaining.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        remaining.push_back({raw[i], i});
    }

    auto rawText = [](const std::vector<IndexedRawKey> &keys,
                      const std::vector<bool> *deleted = nullptr) {
        std::string text;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!deleted || !(*deleted)[i]) {
                text.push_back(keys[i].value);
            }
        }
        return text;
    };

    auto removeForRendered = [&](const std::vector<IndexedRawKey> &keys,
                                 const std::string &target) {
        std::vector<bool> answer;
        if (target.empty()) {
            return std::vector<bool>(keys.size(), true);
        }
        const size_t maxOwnership = std::min<size_t>(6, keys.size());
        for (size_t removeCount = 1; removeCount <= maxOwnership; ++removeCount) {
            std::vector<bool> deleted(keys.size(), false);
            std::function<bool(size_t, size_t)> search =
                [&](size_t from, size_t left) -> bool {
                if (left == 0) {
                    if (normalizeNFC(evaluateTelex(rawText(keys, &deleted))) ==
                        target) {
                        answer = deleted;
                        return true;
                    }
                    return false;
                }
                if (keys.size() - from < left) {
                    return false;
                }
                for (size_t i = from; i + left <= keys.size(); ++i) {
                    deleted[i] = true;
                    if (search(i + 1, left - 1)) {
                        return true;
                    }
                    deleted[i] = false;
                }
                return false;
            };
            if (search(0, removeCount)) {
                return answer;
            }
        }

        // Converter rules have bounded ownership in Telex. This defensive
        // fallback preserves visual deletion semantics for an unexpected rule
        // by retaining the longest raw prefix that renders the target.
        for (size_t keep = keys.size(); keep > 0; --keep) {
            std::vector<IndexedRawKey> prefix(keys.begin(),
                                             keys.begin() + keep - 1);
            if (normalizeNFC(evaluateTelex(rawText(prefix))) == target) {
                std::vector<bool> deleted(keys.size(), false);
                std::fill(deleted.begin() + keep - 1, deleted.end(), true);
                return deleted;
            }
        }
        return std::vector<bool>(keys.size(), true);
    };

    auto renderedGraphemes = splitCodePoints(result.rendered);
    std::vector<RenderedGrapheme> reversed;
    while (!renderedGraphemes.empty() && !remaining.empty()) {
        const auto removedText = renderedGraphemes.back();
        renderedGraphemes.pop_back();
        std::string target;
        for (const auto &grapheme : renderedGraphemes) {
            target += grapheme;
        }
        const auto deleted = removeForRendered(remaining, target);
        RenderedGrapheme ownership;
        ownership.text = removedText;
        std::vector<IndexedRawKey> next;
        for (size_t i = 0; i < remaining.size(); ++i) {
            if (deleted[i]) {
                ownership.rawIndexes.push_back(remaining[i].originalIndex);
            } else {
                next.push_back(remaining[i]);
            }
        }
        reversed.push_back(std::move(ownership));
        remaining = std::move(next);
    }
    std::reverse(reversed.begin(), reversed.end());
    result.graphemes = std::move(reversed);
    return result;
}

std::string UniikiEngine::rawAfterVisualBackspace(const std::string &raw) {
    const auto conversion = conversionWithProvenance(raw);
    if (conversion.graphemes.empty()) {
        return raw;
    }
    const auto &indexes = conversion.graphemes.back().rawIndexes;
    std::unordered_set<size_t> removed(indexes.begin(), indexes.end());
    std::string result;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (!removed.count(i)) {
            result.push_back(raw[i]);
        }
    }
    return result;
}

UniikiEngine::ConversionResult
UniikiEngine::conversionWithProvenanceForTest(const std::string &raw) {
    return conversionWithProvenance(raw);
}

std::pair<std::string, std::string>
UniikiEngine::visualBackspaceForTest(const std::string &raw) {
    const auto rawAfter = rawAfterVisualBackspace(raw);
    return {rawAfter, rawAfter.empty() ? std::string() : evaluateTelex(rawAfter)};
}

std::string UniikiEngine::toneOwnershipForTest(const std::string &raw) {
    const auto toneLex = lexAndReduceToneActions(raw);
    return "tokens=" + tokenOwnershipText(toneLex.tokens) +
           " baseLetters=" + toneLex.baseLetters +
           " removedModifierIndexes=" +
           rawIndexesText(toneLex.history.consumedModifierIndexes) +
           " escapedLiteralIndexes=" +
           rawIndexesText(toneLex.escapedLiteralIndexes) +
           " activeTone=" + toneName(toneLex.history.activeTone) +
           " rendered=" + evaluateTelex(raw);
}

std::string UniikiEngine::converterSegmentationForTest(
    const std::string &raw) {
    for (size_t modifier = 1; modifier + 1 < raw.size(); ++modifier) {
        if (!isToneKey(lowerAscii(raw[modifier]))) {
            continue;
        }
        const char suffixVowel = lowerAscii(raw[modifier + 1]);
        if (suffixVowel != 'a' && suffixVowel != 'e' &&
            suffixVowel != 'o') {
            continue;
        }
        if (!std::all_of(
                raw.begin() + static_cast<std::ptrdiff_t>(modifier + 1),
                raw.end(), [suffixVowel](char ch) {
                    return lowerAscii(ch) == suffixVowel;
                })) {
            continue;
        }
        const auto prefixRaw = raw.substr(0, modifier + 1);
        const auto prefixRendered = evaluateTelex(prefixRaw);
        if (prefixRendered != prefixRaw && containsNonAscii(prefixRendered)) {
            const auto literalSuffix = raw.substr(modifier + 1);
            return "transformedRaw=" + prefixRaw +
                   " transformedRendered=" + prefixRendered +
                   " literalSuffix=" + literalSuffix +
                   " rendered=" + prefixRendered + literalSuffix;
        }
    }
    return "transformedRaw= transformedRendered= literalSuffix= rendered=" +
           evaluateTelex(raw);
}

std::string UniikiEngine::conversionTraceForTest(const std::string &raw) {
    const auto rendered = evaluateTelex(raw);
    conversionDebugTrace.rendered = rendered;
    if (conversionDebugTrace.candidate.empty() && rendered == raw) {
        conversionDebugTrace.fallbackReason = "pre-parse-raw-policy";
    } else if (!conversionDebugTrace.candidate.empty() &&
               conversionDebugTrace.candidate != rendered) {
        conversionDebugTrace.fallbackReason =
            conversionDebugTrace.validVietnamese
                ? "word-policy-restored-raw"
                : "invalid-vietnamese-syllable";
    } else {
        conversionDebugTrace.fallbackReason = "none";
    }
    return "rawBuffer=" + conversionDebugTrace.rawBuffer +
           " tokens=" + conversionDebugTrace.tokens +
           " consumedModifierIndexes=" +
           rawIndexesText(conversionDebugTrace.consumedModifierIndexes) +
           " parsedOnset=" + conversionDebugTrace.parsedOnset +
           " parsedVowelNucleus=" + conversionDebugTrace.parsedVowelNucleus +
           " parsedCoda=" + conversionDebugTrace.parsedCoda +
           " activeTone=" + toneName(conversionDebugTrace.activeTone) +
           " toneTargetIndex=" + conversionDebugTrace.toneTargetIndex +
           " cellsBeforeTone=" + conversionDebugTrace.cellsBeforeTone +
           " candidate=" + conversionDebugTrace.candidate +
           " rendered=" + rendered +
           " fallbackReason=" + conversionDebugTrace.fallbackReason;
}

bool UniikiEngine::rawOwnershipInvariantForTest(const std::string &raw) {
    const auto toneLex = lexAndReduceToneActions(raw);
    if (toneLex.tokens.size() != raw.size()) {
        return false;
    }
    std::vector<bool> owned(raw.size(), false);
    for (const auto &token : toneLex.tokens) {
        if (token.rawIndex >= raw.size() || owned[token.rawIndex] ||
            token.value != raw[token.rawIndex]) {
            return false;
        }
        owned[token.rawIndex] = true;
    }
    return std::all_of(owned.begin(), owned.end(), [](bool value) {
        return value;
    });
}

std::pair<unsigned int, std::string>
UniikiEngine::replacementDeltaForTest(const std::string &oldText,
                                      const std::string &newText) {
    return replacementDelta(oldText, newText);
}

std::string UniikiEngine::replacementTraceForTest(
    const std::string &visibleBefore, const std::string &oldRendered,
    const std::string &newRendered, uint64_t transactionId,
    uint64_t stateVersion) {
    const auto oldChars = splitCodePoints(oldRendered);
    const auto newChars = splitCodePoints(newRendered);
    size_t commonLength = 0;
    while (commonLength < oldChars.size() &&
           commonLength < newChars.size() &&
           oldChars[commonLength] == newChars[commonLength]) {
        ++commonLength;
    }
    std::string commonPrefix;
    std::string oldSuffix;
    std::string newSuffix;
    for (size_t i = 0; i < commonLength; ++i) {
        commonPrefix += oldChars[i];
    }
    for (size_t i = commonLength; i < oldChars.size(); ++i) {
        oldSuffix += oldChars[i];
    }
    for (size_t i = commonLength; i < newChars.size(); ++i) {
        newSuffix += newChars[i];
    }
    const auto [deleteUnits, commitText] =
        replacementDelta(oldRendered, newRendered);
    const bool suffixMatches = endsWith(visibleBefore, oldRendered);
    std::string visibleAfter = visibleBefore;
    if (suffixMatches) {
        auto visibleChars = splitCodePoints(visibleBefore);
        if (deleteUnits <= visibleChars.size()) {
            visibleAfter.clear();
            for (size_t i = 0; i < visibleChars.size() - deleteUnits; ++i) {
                visibleAfter += visibleChars[i];
            }
            visibleAfter += commitText;
        }
    }
    return std::string("converterOutput=") + newRendered +
           " oldRendered=" + oldRendered +
           " newRendered=" + newRendered +
           " commonPrefix=" + commonPrefix +
           " oldSuffix=" + oldSuffix +
           " newSuffix=" + newSuffix +
           " deleteUnits=" + std::to_string(deleteUnits) +
           " commitText=" + commitText +
           " visibleTextBefore=" + visibleBefore +
           " visibleTextAfter=" + visibleAfter +
           " transactionId=" + std::to_string(transactionId) +
           " stateVersion=" + std::to_string(stateVersion) +
           " status=" +
           (suffixMatches ? "COMMITTED" : "SURROUNDING_MISMATCH");
}

std::string UniikiEngine::twoPhaseReplacementTraceForTest(
    const std::string &visibleBefore, const std::string &oldRendered,
    const std::string &newRendered, const std::string &visibleAfterDelete,
    const std::string &visibleAfterCommit) {
    const auto oldChars = splitCodePoints(oldRendered);
    const auto newChars = splitCodePoints(newRendered);
    size_t commonLength = 0;
    while (commonLength < oldChars.size() &&
           commonLength < newChars.size() &&
           oldChars[commonLength] == newChars[commonLength]) {
        ++commonLength;
    }
    std::string oldSuffix;
    std::string commitText;
    for (size_t i = commonLength; i < oldChars.size(); ++i) {
        oldSuffix += oldChars[i];
    }
    for (size_t i = commonLength; i < newChars.size(); ++i) {
        commitText += newChars[i];
    }
    const auto deleteUnits =
        static_cast<unsigned int>(oldChars.size() - commonLength);
    const bool suffixMatches = endsWith(visibleBefore, oldRendered);
    std::string expectedAfterDelete = visibleBefore;
    if (suffixMatches) {
        expectedAfterDelete.resize(expectedAfterDelete.size() -
                                   oldSuffix.size());
    }
    const auto expectedAfterCommit = expectedAfterDelete + commitText;
    const bool deleteAcknowledged =
        suffixMatches && visibleAfterDelete == expectedAfterDelete;
    const bool commitSent = deleteAcknowledged;
    const bool commitAcknowledged =
        commitSent && visibleAfterCommit == expectedAfterCommit;
    const auto status =
        !suffixMatches
            ? "SURROUNDING_MISMATCH"
            : (!deleteAcknowledged
                   ? "DELETE_NOT_ACKNOWLEDGED"
                   : (commitAcknowledged ? "COMMITTED"
                                         : "COMMIT_NOT_ACKNOWLEDGED"));
    const auto visibleAfter = commitSent ? visibleAfterCommit
                                         : visibleAfterDelete;
    return std::string("oldRendered=") + oldRendered +
           " newRendered=" + newRendered +
           " suffixMatches=" +
           std::string(suffixMatches ? "true" : "false") +
           " deleteOffset=-" + std::to_string(deleteUnits) +
           " deleteSize=" + std::to_string(deleteUnits) +
           " deleteUnit=UCS4_CODEPOINT" +
           " visibleTextAfterDelete=" + visibleAfterDelete +
           " commitSent=" + std::string(commitSent ? "true" : "false") +
           " commitText=" + commitText +
           " visibleTextAfter=" + visibleAfter +
           " status=" + status;
}

std::string UniikiEngine::replacementSelectionPolicyForTest(
    unsigned int cursor, unsigned int anchor,
    const std::string &beforeCursor, const std::string &oldRendered) {
    const auto state = replacementSelectionState(
        cursor, anchor, beforeCursor, oldRendered);
    return std::string("cursor=") + std::to_string(cursor) +
           " anchor=" + std::to_string(anchor) +
           " selectionLength=" + std::to_string(state.length) +
           " autocompleteSelection=" + boolText(state.autocompleteSelection) +
           " selectionSafe=" + boolText(state.safe);
}

std::string UniikiEngine::simulateDirectForTest(const std::string &raw) {
    std::string visible;
    std::string rawBuffer;
    std::string lastRendered;
    std::string recoverRawBuffer;
    std::string recoverRendered;
    std::string recoverSuffix;

    for (char ch : raw) {
        if (ch == '\b') {
            if (!rawBuffer.empty()) {
                auto lastRenderedBefore = lastRendered;
                rawBuffer = rawAfterVisualBackspace(rawBuffer);
                auto candidate = normalizeNFC(rawBuffer.empty() ? std::string() : evaluateTelex(rawBuffer));
                auto [deleteCount, insertText] = replacementDelta(lastRenderedBefore, candidate);
                if (deleteCount > 0) {
                    auto visibleChars = splitCodePoints(visible);
                    visible.clear();
                    auto keepCount = visibleChars.size() - deleteCount;
                    for (size_t i = 0; i < keepCount; ++i) {
                        visible += visibleChars[i];
                    }
                }
                visible += insertText;
                lastRendered = candidate;
            } else if (recoverSuffix == " " && !recoverRawBuffer.empty()) {
                auto visibleChars = splitCodePoints(visible);
                if (!visibleChars.empty()) {
                    visible.clear();
                    for (size_t i = 0; i + 1 < visibleChars.size(); ++i) {
                        visible += visibleChars[i];
                    }
                }
                rawBuffer = recoverRawBuffer;
                lastRendered = recoverRendered;
                recoverRawBuffer.clear();
                recoverRendered.clear();
                recoverSuffix.clear();
            } else {
                auto visibleChars = splitCodePoints(visible);
                if (!visibleChars.empty()) {
                    visible.clear();
                    for (size_t i = 0; i + 1 < visibleChars.size(); ++i) {
                        visible += visibleChars[i];
                    }
                }
            }
            continue;
        }

        if (!recoverSuffix.empty()) {
            recoverRawBuffer.clear();
            recoverRendered.clear();
            recoverSuffix.clear();
        }

        if (!isAsciiWordChar(ch)) {
            visible += ch;
            recoverRawBuffer = ch == ' ' ? rawBuffer : std::string("");
            recoverRendered = ch == ' ' ? lastRendered : std::string("");
            recoverSuffix = ch == ' ' ? std::string(" ") : std::string("");
            rawBuffer.clear();
            lastRendered.clear();
            continue;
        }

        auto rawBefore = rawBuffer;
        auto lastRenderedBefore = lastRendered;
        rawBuffer.push_back(ch);
        auto candidate = normalizeNFC(evaluateTelex(rawBuffer));
        auto [deleteCount, insertText] = replacementDelta(lastRenderedBefore, candidate);
        if (deleteCount > 0) {
            auto visibleChars = splitCodePoints(visible);
            visible.clear();
            auto keepCount = visibleChars.size() - deleteCount;
            for (size_t i = 0; i < keepCount; ++i) {
                visible += visibleChars[i];
            }
        }
        visible += insertText;
        lastRendered = candidate;
    }

    return visible;
}

AddonInstance *UniikiFactory::create(AddonManager *manager) {
    return new UniikiEngine(manager->instance());
}

} // namespace fcitx
