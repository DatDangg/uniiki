#include "uniiki_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

std::string boolText(bool value) {
    return value ? "1" : "0";
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

bool isVietnameseInitialPrefix(const std::string &tail) {
    static const std::vector<std::string> clusters = {
        "ch", "gh", "gi", "kh", "ng", "ngh", "nh", "ph", "qu", "th", "tr",
    };
    return std::any_of(clusters.begin(), clusters.end(), [&tail](const std::string &cluster) {
        return cluster.rfind(tail, 0) == 0;
    });
}

bool isEnglishConsonantCluster(const std::string &tail) {
    static const std::vector<std::string> clusters = {
        "scr", "str", "spl", "spr", "sch", "thr", "phr", "chr",
    };
    return std::any_of(clusters.begin(), clusters.end(), [&tail](const std::string &cluster) {
        return tail.size() >= cluster.size() &&
               tail.compare(tail.size() - cluster.size(), cluster.size(), cluster) == 0;
    });
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

std::string normalizeNFC(const std::string &text) {
    // Uniiki's generated Vietnamese table is already precomposed NFC. Keep this
    // as the single normalization boundary until a Unicode normalizer is linked.
    return text;
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
    static const std::vector<std::string> protectedWords = {
        "python", "javascript", "typescript", "telex", "vni", "terminal",
        "code", "chrome", "firefox", "libreoffice", "telegram", "discord",
        "zalo", "web", "latinh", "password", "desktop", "pre", "windows",
        "google", "version", "raw",
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
        "u0a0", "u0o1", "u0o1i0", "u0i0", "u0u0", "u0y0", "u0y0e1",
        "u0y0e1u0", "u3a0", "u3i0", "u3o3", "u3o3i0", "u3u0",
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

void applyToneToCells(std::vector<Cell> &cells, int tone) {
    std::vector<size_t> vowels;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (isVowelCell(cells[i])) {
            vowels.push_back(i);
        }
    }
    if (vowels.empty()) {
        return;
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
                return;
            }
        }

        const auto &lastVowel = cells[vowels.back()];
        if (lastVowel.base == 'i' || lastVowel.base == 'y' || lastVowel.base == 'u') {
            target = vowels[vowels.size() - 2];
            cells[target].tone = tone;
            return;
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
                return;
            }
        }
        if (last + 1 < cells.size() && lastGroup.size() > 1) {
            target = lastGroup.back();
        } else {
            target = lastGroup.front();
        }
    }

    cells[target].tone = tone;
}

} // namespace

void UniikiState::reset() {
    rawBuffer.clear();
    displayText.clear();
    lastRenderedText.clear();
    recoverRawBuffer.clear();
    recoverRenderedText.clear();
    recoverSuffix.clear();
    directActive = false;
    isInternalCommit = false;
    replacementInProgress = false;
    pendingKeys.clear();
    replacement = ReplacementTransaction();
    revision = 0;
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
            if (!state->replacementInProgress || state->replacement.completed) {
                return;
            }

            const auto &surrounding = ic->surroundingText();
            auto beforeCursor =
                surrounding.isValid() ? textBeforeCursor(surrounding) : std::string();
            if (!surrounding.isValid() ||
                !endsWith(beforeCursor, state->replacement.newRendered)) {
                FCITX_INFO() << "UniikiTrace REPLACEMENT_ACK_STALE"
                             << " eventId=" << state->replacement.id
                             << " expected=" << state->replacement.newRendered
                             << " beforeCursor=" << beforeCursor
                             << " surroundingValid=" << surrounding.isValid();
                return;
            }

            FCITX_INFO() << "UniikiTrace REPLACEMENT_ACK"
                         << " eventId=" << state->replacement.id
                         << " candidate=" << state->replacement.newRendered
                         << " beforeCursor=" << beforeCursor;
            completeReplacement(ic, *state, "surrounding-update");
        });
}

void UniikiEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    auto sym = event.key().sym();
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    auto eventId = nextKeyEventId();
    auto physicalKey = event.key().toString();
    auto keyStateValue = event.key().states().toInteger();
    bool isRepeat = (keyStateValue & static_cast<uint32_t>(KeyState::Repeat)) != 0;
    bool hasNonRepeatState = (keyStateValue & ~static_cast<uint32_t>(KeyState::Repeat)) != 0;
    auto rawBufferBefore = state->rawBuffer;
    auto candidateBefore = state->displayText;
    auto renderedTextBefore = state->lastRenderedText;
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
        FCITX_INFO() << "UniikiTrace KEY_EVENT"
                     << " eventId=" << eventId
                     << " physicalKey=" << physicalKey
                     << " unicode=" << unicode
                     << " keyCode=" << static_cast<uint32_t>(sym)
                     << " keyState=" << keyStateValue
                     << " isPress=" << boolText(!event.isRelease())
                     << " isRelease=" << boolText(event.isRelease())
                     << " isRepeat=" << boolText(isRepeat)
                     << " rawBufferBefore=" << rawBufferBefore
                     << " rawBufferAfter=" << state->rawBuffer
                     << " candidateBefore=" << candidateBefore
                     << " candidateAfter=" << state->displayText
                     << " renderedTextBefore=" << renderedTextBefore
                     << " renderedTextAfter=" << state->lastRenderedText
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
        FCITX_INFO() << "UniikiTrace KEY_EVENT_RESULT"
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
        FCITX_INFO() << "UniikiTrace INPUT_CONTEXT_CAPABILITY"
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
            FCITX_INFO() << "UniikiTrace KEY_DROPPED"
                         << " eventId=" << eventId
                         << " physicalKey=" << physicalKey;
        }
        if (ownershipCount > 1) {
            FCITX_INFO() << "UniikiTrace KEY_DOUBLE_OWNERSHIP"
                         << " eventId=" << eventId
                         << " physicalKey=" << physicalKey;
        }
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
        event.filterAndAccept();
        state->pendingKeys.push_back({eventId, event.key(), event.rawKey(), event.isRelease(), event.time()});
        handledByEngine = false;
        forwardedToClient = false;
        consumed = true;
        queued = true;
        FCITX_INFO() << "UniikiTrace OVERLAPPING_REPLACEMENT"
                     << " eventId=" << eventId
                     << " physicalKey=" << event.key().toString()
                     << " queued=1"
                     << " queueLength=" << state->pendingKeys.size();
        FCITX_INFO() << "UniikiTrace ASSERT_REPLACEMENT_PENDING physicalKey="
                     << event.key().toString()
                     << " rawBuffer=" << state->rawBuffer
                     << " renderedText=" << state->lastRenderedText
                     << " revision=" << state->revision;
        logKeyTrace();
        // Reaching the next physical event means the client has had an event
        // loop turn to apply the previous delete/commit pair. Treat that event
        // as a bounded fallback acknowledgement so a client that never sends
        // SurroundingTextUpdated cannot leave the keyboard queue stuck.
        completeReplacement(ic, *state, "next-key");
        return;
    }
    bool directCommit = canUseDirectCommit(ic, *state);
    if (!directCommit && state->directActive) {
        auto capabilities = ic->capabilityFlags();
        const auto &surrounding = ic->surroundingText();
        FCITX_INFO() << "UniikiTrace DIRECT_REPLACE_UNSUPPORTED"
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
        state->rawBuffer.pop_back();
        state->displayText =
            state->rawBuffer.empty() ? std::string() : evaluateTelex(state->rawBuffer);
        if (directCommit) {
            deleteCalled = !lastRenderedBefore.empty();
            commitCalled = !state->displayText.empty();
            renderDirectCommit(ic, *state, "BackSpace", rawBefore, lastRenderedBefore, eventId);
            if (state->rawBuffer.empty()) {
                state->displayText.clear();
                state->lastRenderedText.clear();
                state->recoverRawBuffer.clear();
                state->recoverRenderedText.clear();
                state->recoverSuffix.clear();
                state->directActive = false;
            }
            resetCalled = true;
            resetReason = "BackSpace raw character";
        } else {
            state->reset();
            resetCalled = true;
            resetReason = "BackSpace";
            state->isInternalCommit = true;
            ic->forwardKey(event.rawKey(), false, event.time());
            state->isInternalCommit = false;
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
        FCITX_INFO() << "UniikiTrace RESTORE_AFTER_SPACE_BACKSPACE"
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
    if (!rawBefore.empty() && lastRenderedBefore != rawBefore &&
        result.text == state->rawBuffer &&
        !shouldProtectRawWord(state->rawBuffer, lowerRaw(state->rawBuffer))) {
        state->rawBuffer = std::string(1, ch);
        state->displayText = evaluateTelex(state->rawBuffer);
        lastRenderedBefore.clear();
    }
    if (directCommit) {
        deleteCalled = !lastRenderedBefore.empty();
        commitCalled = !state->displayText.empty();
        renderDirectCommit(ic, *state, std::string(1, ch), rawBefore, lastRenderedBefore, eventId);
        processPendingKeys(ic, *state);
    } else {
        deleteCalled = !lastRenderedBefore.empty();
        commitCalled = !state->displayText.empty();
        renderDirectCommit(ic, *state, std::string(1, ch), rawBefore, lastRenderedBefore, eventId);
        processPendingKeys(ic, *state);
    }
    logKeyTrace();
}

void UniikiEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->reset();
}

void UniikiEngine::processPendingKeys(InputContext *ic, UniikiState &state) const {
    while (!state.replacementInProgress && !state.pendingKeys.empty()) {
        auto pending = state.pendingKeys.front();
        state.pendingKeys.pop_front();
        processPendingKey(ic, state, pending);
    }
}

void UniikiEngine::completeReplacement(InputContext *ic, UniikiState &state,
                                       const char *source) const {
    if (!state.replacementInProgress) {
        return;
    }
    FCITX_INFO() << "UniikiTrace REPLACEMENT_COMPLETE"
                 << " eventId=" << state.replacement.id
                 << " source=" << source
                 << " queueLength=" << state.pendingKeys.size();
    state.isInternalCommit = false;
    state.replacementInProgress = false;
    state.replacement.completed = true;
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

    auto logPendingResult = [&]() {
        auto stateAfter = state.rawBuffer.empty() ? "IDLE" : "ACTIVE_COMPOSITION";
        FCITX_INFO() << "UniikiTrace KEY_EVENT_RESULT"
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
        }
        logPendingResult();
        return;
    }

    auto rawBeforeLocal = state.rawBuffer;
    auto renderedBeforeLocal = state.lastRenderedText;
    auto result = processChar(state, ch);
    state.displayText = result.text;
    if (!rawBeforeLocal.empty() && renderedBeforeLocal != rawBeforeLocal &&
        result.text == state.rawBuffer &&
        !shouldProtectRawWord(state.rawBuffer, lowerRaw(state.rawBuffer))) {
        state.rawBuffer = std::string(1, ch);
        state.displayText = evaluateTelex(state.rawBuffer);
        renderedBeforeLocal.clear();
    }

    consumedAsTelex = true;
    deleteCalled = !renderedBeforeLocal.empty();
    commitCalled = !state.displayText.empty();
    renderDirectCommit(ic, state, physicalKey, rawBeforeLocal, renderedBeforeLocal, pending.eventId);
    logPendingResult();
}

UniikiEngine::ProcessResult UniikiEngine::processChar(UniikiState &state, char ch) const {
    auto current = state.displayText;
    auto nextRaw = state.rawBuffer + ch;
    auto next = evaluateTelex(nextRaw);

    state.rawBuffer = nextRaw;

    if (next == current + ch) {
        return {true, false, next, true, true, true};
    }
    return {true, true, next, true, true, true};
}

bool UniikiEngine::canUseDirectCommit(InputContext *ic, const UniikiState &state) const {
    auto capabilities = ic->capabilityFlags();

    if (state.lastRenderedText.empty()) {
        return true;
    }

    return capabilities.test(CapabilityFlag::SurroundingText);
}

unsigned int UniikiEngine::surroundingDeleteUnits(const std::string &utf8Text) const {
    return static_cast<unsigned int>(utf8::length(normalizeNFC(utf8Text)));
}

void UniikiEngine::setOwnedLocalSurrounding(InputContext *ic, const std::string &text) const {
    auto cursor = surroundingDeleteUnits(text);
    ic->surroundingText().setText(text, cursor, cursor);
}

void UniikiEngine::renderDirectCommit(InputContext *ic, UniikiState &state,
                                      const std::string &physicalKey,
                                      const std::string &rawBefore,
                                      const std::string &lastRenderedBefore,
                                      uint64_t eventId) const {
    auto candidate = normalizeNFC(state.displayText);
    state.revision = eventId;
    auto [deleteCount, insertText] = replacementDelta(lastRenderedBefore, candidate);
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
    state.replacementInProgress = true;
    state.replacement = {eventId, lastRenderedBefore, candidate, deleteCount, false};
    state.isInternalCommit = true;
    FCITX_INFO() << "UniikiTrace DELETE_UNIT_CHECK"
                 << " eventId=" << eventId
                 << " text=" << lastRenderedBefore
                 << " UTF8_BYTES=" << lastRenderedBefore.size()
                 << " UNICODE_CODEPOINTS=" << utf8::length(normalizeNFC(lastRenderedBefore))
                 << " DELETE_UNITS=" << deleteCount;
    FCITX_INFO() << "UniikiTrace REPLACE_BEGIN eventId=" << eventId
                 << " key=" << physicalKey
                 << " oldRendered=" << lastRenderedBefore
                 << " newCandidate=" << candidate
                 << " surroundingTextBefore=" << surroundingTextBefore
                 << " beforeCursorBefore=" << beforeCursor
                 << " cursorBefore=" << cursorBefore
                 << " anchorBefore=" << anchorBefore
                 << " capabilities=" << capabilities.toInteger()
                 << " hasSurroundingTextCapability=" << hasSurroundingCapability
                 << " deleteOffset=" << -static_cast<int>(deleteCount)
                 << " deleteCount=" << deleteCount
                 << " insertText=" << insertText
                 << " cursorFromClient=" << cursorBefore
                 << " expectedCursor=unknown"
                 << " queueLength=0"
                 << " replacementInProgress=" << state.replacementInProgress;

    if (deleteCount > 0 &&
        (!surroundingValidBefore || !endsWith(beforeCursor, lastRenderedBefore))) {
        FCITX_INFO() << "UniikiTrace SURROUNDING_MISMATCH"
                     << " eventId=" << eventId
                     << " expectedSuffix=" << lastRenderedBefore
                     << " actualBeforeCursor=" << beforeCursor
                     << " surroundingValid=" << surroundingValidBefore
                     << " deleteSkipped=0"
                     << " replacementProceedingFromOwnedState=1"
                     << " physicalKey=" << physicalKey;
    }

    if (deleteCount > 0) {
        // Wayland converts character offsets to protocol byte offsets from this
        // cache. Reassert the engine-owned word so a delayed client snapshot
        // cannot make the frontend reject or mis-size the deletion.
        setOwnedLocalSurrounding(ic, lastRenderedBefore);
        ic->deleteSurroundingText(-static_cast<int>(deleteCount), deleteCount);
    }
    FCITX_INFO() << "UniikiTrace DELETE_SENT eventId=" << eventId
                 << " deleteOffset=" << -static_cast<int>(deleteCount)
                 << " deleteCount=" << deleteCount
                 << " method=surrounding";

    if (!insertText.empty()) {
        ic->commitString(insertText);
    }
    setOwnedLocalSurrounding(ic, candidate);
    FCITX_INFO() << "UniikiTrace COMMIT_SENT eventId=" << eventId
                 << " insertText=" << insertText;

    state.displayText = candidate;
    state.lastRenderedText = candidate;
    state.directActive = true;
    state.isInternalCommit = false;

    auto cursor = ic->surroundingText().isValid()
                      ? std::to_string(ic->surroundingText().cursor())
                      : std::string("invalid");
    auto surroundingAfter = ic->surroundingText().isValid() ? ic->surroundingText().text() : std::string("");
    auto beforeCursorAfter = ic->surroundingText().isValid() ? textBeforeCursor(ic->surroundingText()) : std::string("");
    FCITX_INFO() << "UniikiTrace REPLACE_END eventId=" << eventId
                 << " key=" << physicalKey
                 << " oldRendered=" << lastRenderedBefore
                 << " newCandidate=" << candidate
                 << " deleteOffset=" << -static_cast<int>(deleteCount)
                 << " deleteCount=" << deleteCount
                 << " insertText=" << insertText
                 << " cursorFromClient=" << cursor
                 << " surroundingTextAfter=" << surroundingAfter
                 << " expectedSuffix=" << candidate
                 << " replacementMatched=deferred"
                 << " expectedCursor=unknown"
                 << " queueLength=0"
                 << " replacementInProgress=" << state.replacementInProgress;

    FCITX_INFO() << "UniikiTrace eventId=" << eventId
                 << " physicalKey=" << physicalKey
                 << " eventSource=physical"
                 << " rawBufferBefore=" << rawBefore
                 << " rawBufferAfter=" << state.rawBuffer
                 << " renderedTextBefore=" << lastRenderedBefore
                 << " candidate=" << candidate
                 << " commonPrefixLength=0"
                 << " deleteCount=" << deleteCount
                 << " insertText=" << insertText
                 << " renderedTextAfter=" << state.lastRenderedText
                 << " isInternalCommit=" << state.isInternalCommit
                 << " cursorPosition=" << cursor
                 << " revision=" << state.revision
                 << " queueState=idle"
                 << " pendingCommit=";

    if (deleteCount == 0) {
        completeReplacement(ic, state, "append-dispatched");
    } else if (!hasSurroundingCapability) {
        completeReplacement(ic, state, "replace-no-surrounding");
    }
}

void UniikiEngine::fallbackCommitRaw(InputContext *ic, UniikiState &state, const std::string &text,
                                     const std::string &reason) const {
    FCITX_INFO() << "UniikiTrace RAW_FALLBACK"
                 << " reason=" << reason
                 << " text=" << text
                 << " rawBuffer=" << state.rawBuffer
                 << " renderedText=" << state.lastRenderedText
                 << " handledByEngine=1"
                 << " consumed=1"
                 << " forwardedToClient=0"
                 << " noPreeditApiCalled=1";
    state.isInternalCommit = true;
    ic->commitString(normalizeNFC(text));
    state.isInternalCommit = false;
    state.reset();
}

void UniikiEngine::finishDirectComposition(InputContext *ic, UniikiState &state,
                                           const std::string &boundary,
                                           bool commitBoundary) const {
    auto recoverRawBuffer = boundary == " " ? state.rawBuffer : std::string("");
    auto recoverRenderedText = boundary == " " ? state.lastRenderedText : std::string("");
    if (commitBoundary && !boundary.empty()) {
        state.isInternalCommit = true;
        ic->commitString(normalizeNFC(boundary));
        state.isInternalCommit = false;
    }
    ic->surroundingText().invalidate();
    state.reset();
    state.recoverRawBuffer = recoverRawBuffer;
    state.recoverRenderedText = recoverRenderedText;
    state.recoverSuffix = boundary;
}

std::string UniikiEngine::evaluateTelex(const std::string &raw) {
    auto rawLower = lowerRaw(raw);
    if (shouldProtectRawWord(raw, rawLower)) {
        return raw;
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

    std::vector<Cell> cells;
    int activeTone = TONE_NONE;
    std::unordered_map<char, int> toneCount;
    bool wModifierActive = false;
    bool wGeneratedVowel = false;
    bool wEscaped = false;
    char wModifierKey = 0;
    std::vector<std::pair<size_t, int>> wModifiedMarks;

    for (size_t i = 0; i < raw.size(); ++i) {
        char ch = raw[i];
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (wModifierActive && isRawVowel(lower)) {
            wModifierActive = false;
            wGeneratedVowel = false;
            wModifiedMarks.clear();
        }

        if (lower == 'd') {
            if (i + 2 < raw.size() &&
                std::tolower(static_cast<unsigned char>(raw[i + 1])) == 'd' &&
                std::tolower(static_cast<unsigned char>(raw[i + 2])) == 'd') {
                cells.push_back(rawCell(ch));
                cells.push_back(rawCell(raw[i + 2]));
                i += 2;
                continue;
            }
            if (i + 1 < raw.size() &&
                std::tolower(static_cast<unsigned char>(raw[i + 1])) == 'd') {
                cells.push_back(strokeDCell(std::isupper(static_cast<unsigned char>(ch))));
                ++i;
                continue;
            }
            if (i > 0 && !cells.empty() && rawLower[0] == 'd' && hasVowel(cells) &&
                cells.front().kind == Cell::Kind::Raw && std::tolower(cells.front().raw) == 'd') {
                cells.front() = strokeDCell(std::isupper(static_cast<unsigned char>(cells.front().raw)));
                continue;
            }
            cells.push_back(rawCell(ch));
            continue;
        }

        if ((lower == 'a' || lower == 'e' || lower == 'o') && i + 2 < raw.size() &&
            std::tolower(static_cast<unsigned char>(raw[i + 1])) == lower &&
            std::tolower(static_cast<unsigned char>(raw[i + 2])) == lower) {
            cells.push_back(rawCell(ch));
            cells.push_back(rawCell(raw[i + 2]));
            i += 2;
            continue;
        }

        if ((lower == 'a' || lower == 'e' || lower == 'o') && i + 1 < raw.size() &&
            std::tolower(static_cast<unsigned char>(raw[i + 1])) == lower) {
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
            bool skippedTrailingConsonant = false;
            bool seenVowel = false;
            for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
                if (!isVowelCell(*it)) {
                    if (!seenVowel && !skippedTrailingConsonant && isRawConsonant(*it)) {
                        skippedTrailingConsonant = true;
                        continue;
                    }
                    break;
                }
                seenVowel = true;
                if (it->base == lower && it->mark == 0) {
                    it->mark = 1;
                    appliedLateMark = true;
                    break;
                }
            }
            if (appliedLateMark) {
                continue;
            }
        }

        if (lower == 'w') {
            if (wModifierActive) {
                if (wGeneratedVowel) {
                    cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(wModifiedMarks.front().first));
                } else {
                    for (const auto &[index, previousMark] : wModifiedMarks) {
                        cells[index].mark = previousMark;
                    }
                }
                bool applyNang = activeTone == TONE_NONE && shouldApplyNangOnWEscape(cells);
                cells.push_back(rawCell(applyNang ? 'w' : wModifierKey));
                if (applyNang) {
                    activeTone = TONE_NANG;
                }
                wModifierActive = false;
                wGeneratedVowel = false;
                wModifiedMarks.clear();
                wEscaped = true;
                continue;
            }
            if (endsWithBases(cells, "uoi")) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 3, cells[cells.size() - 3].mark},
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                };
                cells[cells.size() - 3].mark = 3;
                cells[cells.size() - 2].mark = 3;
                wModifierActive = true;
                wModifierKey = ch;
                continue;
            }
            if (endsWithBases(cells, "uo")) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                    {cells.size() - 1, cells[cells.size() - 1].mark},
                };
                cells[cells.size() - 2].mark = 3;
                cells[cells.size() - 1].mark = 3;
                wModifierActive = true;
                wModifierKey = ch;
                continue;
            }
            if (endsWithBases(cells, "ua")) {
                wGeneratedVowel = false;
                wModifiedMarks = {
                    {cells.size() - 2, cells[cells.size() - 2].mark},
                    {cells.size() - 1, cells[cells.size() - 1].mark},
                };
                cells[cells.size() - 2].mark = 3;
                cells[cells.size() - 1].mark = 0;
                wModifierActive = true;
                wModifierKey = ch;
                continue;
            }
            bool hookApplied = false;
            for (size_t index = cells.size(); index > 0; --index) {
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
                continue;
            }
            if (!hasVowel(cells)) {
                wModifiedMarks = {{cells.size(), 0}};
                cells.push_back(vowelCell('u', 3, TONE_NONE, std::isupper(static_cast<unsigned char>(ch))));
                wModifierActive = true;
                wGeneratedVowel = true;
                wModifierKey = ch;
                continue;
            }
            if (i + 1 < raw.size() &&
                std::tolower(static_cast<unsigned char>(raw[i + 1])) == 'w') {
                cells.push_back(rawCell(ch));
                ++i;
                continue;
            }
        }

        if (isToneKey(lower)) {
            bool hasFutureVowel = false;
            for (size_t j = i + 1; j < raw.size(); ++j) {
                if (isRawVowel(raw[j])) {
                    hasFutureVowel = true;
                    break;
                }
            }
            bool hasVowelAlready = hasVowel(cells);
            if (hasVowelAlready || (!cells.empty() && hasFutureVowel)) {
                auto tail = lowerRaw(cellsText(cells)) + lower;
                if (!hasVowelAlready && isVietnameseInitialPrefix(tail)) {
                    cells.push_back(rawCell(ch));
                    continue;
                }
                if (hasVowelAlready && isEnglishConsonantCluster(tail)) {
                    cells.push_back(rawCell(ch));
                    continue;
                }
                toneCount[lower] += 1;
                if (toneCount[lower] > 1) {
                    activeTone = TONE_NONE;
                    cells.push_back(rawCell(ch));
                } else {
                    activeTone = toneFromKey(lower);
                }
                continue;
            }
        }

        if (isRawVowel(lower)) {
            cells.push_back(vowelCell(lower, 0, TONE_NONE, std::isupper(static_cast<unsigned char>(ch))));
        } else {
            cells.push_back(rawCell(ch));
        }
    }

    if (activeTone != TONE_NONE) {
        applyToneToCells(cells, activeTone);
    }
    auto candidate = cellsText(cells);
    if (!wEscaped && candidate != raw && hasVietnameseMarkedCell(cells) &&
        !isPotentialVietnamesePrefixCells(cells) && !isValidVietnameseSyllableCells(cells)) {
        return raw;
    }
    return candidate;
}

std::string UniikiEngine::evaluateTelexForTest(const std::string &raw) {
    return evaluateTelex(raw);
}

std::pair<unsigned int, std::string>
UniikiEngine::replacementDeltaForTest(const std::string &oldText,
                                      const std::string &newText) {
    return replacementDelta(oldText, newText);
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
                rawBuffer.pop_back();
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
        if (!rawBefore.empty() && lastRenderedBefore != rawBefore && candidate == rawBuffer &&
            !shouldProtectRawWord(rawBuffer, lowerRaw(rawBuffer))) {
            rawBuffer = std::string(1, ch);
            candidate = normalizeNFC(evaluateTelex(rawBuffer));
            lastRenderedBefore.clear();
        }

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
