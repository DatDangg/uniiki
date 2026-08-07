#include "engine.h"
#include <algorithm>
#include <cctype>
#include <iostream>

static std::vector<std::string> splitUtf8(const std::string& str) {
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < str.length()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        chars.push_back(str.substr(i, len));
        i += len;
    }
    return chars;
}

static std::string joinUtf8(const std::vector<std::string>& chars) {
    std::string s;
    for (const auto& c : chars) s += c;
    return s;
}

struct VowelGroup {
    std::string base;
    int hat;
    std::vector<std::string> tones;
};

VietnameseEngine::VietnameseEngine(const std::string& mode, bool modern_tone)
    : mode_(mode), modern_tone_(modern_tone) {
    initMaps();
    resetBuffer();
}

void VietnameseEngine::resetBuffer() {
    raw_keys_.clear();
}

void VietnameseEngine::setMode(const std::string& mode) {
    mode_ = mode;
}

void VietnameseEngine::initMaps() {
    std::vector<VowelGroup> groups = {
        {"a", 0, {"a", "á", "à", "ả", "ã", "ạ"}},
        {"a", 1, {"â", "ấ", "ầ", "ẩ", "ẫ", "ậ"}},
        {"a", 2, {"ă", "ắ", "ằ", "ẳ", "ẵ", "ặ"}},
        {"e", 0, {"e", "é", "è", "ẻ", "ẽ", "ẹ"}},
        {"e", 1, {"ê", "ế", "ề", "ể", "ễ", "ệ"}},
        {"i", 0, {"i", "í", "ì", "ỉ", "ĩ", "ị"}},
        {"o", 0, {"o", "ó", "ò", "ỏ", "õ", "ọ"}},
        {"o", 1, {"ô", "ố", "ồ", "ổ", "ỗ", "ộ"}},
        {"o", 3, {"ơ", "ớ", "ờ", "ở", "ỡ", "ợ"}},
        {"u", 0, {"u", "ú", "ù", "ủ", "ũ", "ụ"}},
        {"u", 3, {"ư", "ứ", "ừ", "ử", "ữ", "ự"}},
        {"y", 0, {"y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"}}
    };

    for (const auto& g : groups) {
        for (size_t tone = 0; tone < g.tones.size(); ++tone) {
            char_decompose_[g.tones[tone]] = {g.base, g.hat, static_cast<int>(tone)};
        }
    }
    char_decompose_["đ"] = {"d", -1, 0};
    char_decompose_["Đ"] = {"D", -1, 0};

    add_hat_map_["a"] = {
        {"a", "â"}, {"ă", "â"}, {"á", "ấ"}, {"à", "ầ"}, {"ả", "ẩ"}, {"ã", "ẫ"}, {"ạ", "ậ"},
        {"A", "Â"}, {"Ă", "Â"}, {"Á", "Ấ"}, {"À", "Ầ"}, {"Ả", "Ẩ"}, {"Ã", "Ẫ"}, {"Ạ", "Ậ"},
        {"ắ", "ấ"}, {"ằ", "ầ"}, {"ẳ", "ẩ"}, {"ẵ", "ẫ"}, {"ặ", "ậ"},
        {"Ắ", "Ấ"}, {"Ằ", "Ầ"}, {"Ẳ", "Ẩ"}, {"Ẵ", "Ẫ"}, {"Ặ", "Ậ"}
    };
    add_hat_map_["e"] = {
        {"e", "ê"}, {"é", "ế"}, {"è", "ề"}, {"ẻ", "ể"}, {"ẽ", "ễ"}, {"ẹ", "ệ"},
        {"E", "Ê"}, {"É", "Ế"}, {"È", "Ề"}, {"Ẻ", "Ể"}, {"Ẽ", "Ễ"}, {"Ẹ", "Ệ"}
    };
    add_hat_map_["o"] = {
        {"o", "ô"}, {"ơ", "ô"}, {"ó", "ố"}, {"ò", "ồ"}, {"ỏ", "ổ"}, {"õ", "ỗ"}, {"ọ", "ộ"},
        {"O", "Ô"}, {"Ơ", "Ô"}, {"Ó", "Ố"}, {"Ò", "Ồ"}, {"Ỏ", "Ổ"}, {"Õ", "Ỗ"}, {"Ọ", "Ộ"},
        {"ớ", "ố"}, {"ờ", "ồ"}, {"ở", "ổ"}, {"ỡ", "ỗ"}, {"ợ", "ộ"},
        {"Ớ", "Ố"}, {"Ờ", "Ồ"}, {"Ở", "Ổ"}, {"Ỡ", "Ỗ"}, {"Ợ", "Ộ"}
    };

    remove_hat_map_["a"] = {
        {"â", "a"}, {"ấ", "á"}, {"ầ", "à"}, {"ẩ", "ả"}, {"ẫ", "ã"}, {"ậ", "ạ"},
        {"Â", "A"}, {"Ấ", "Á"}, {"Ầ", "À"}, {"Ẩ", "Ả"}, {"Ẫ", "Ã"}, {"Ậ", "Ạ"}
    };
    remove_hat_map_["e"] = {
        {"ê", "e"}, {"ế", "é"}, {"ề", "è"}, {"ể", "ẻ"}, {"ễ", "ẽ"}, {"ệ", "ẹ"},
        {"Ê", "E"}, {"Ế", "É"}, {"Ề", "È"}, {"Ể", "Ẻ"}, {"Ễ", "Ẽ"}, {"Ệ", "Ẹ"}
    };
    remove_hat_map_["o"] = {
        {"ô", "o"}, {"ố", "ó"}, {"ồ", "ò"}, {"ổ", "ỏ"}, {"ỗ", "õ"}, {"ộ", "ọ"},
        {"Ô", "O"}, {"Ố", "Ó"}, {"Ồ", "Ò"}, {"Ổ", "Ỏ"}, {"Ỗ", "Õ"}, {"Ộ", "Ọ"}
    };

    unhatted_map_["a"] = {"a","á","à","ả","ã","ạ","A","Á","À","Ả","Ã","Ạ","ă","ắ","ằ","ẳ","ẵ","ặ","Ă","Ắ","Ằ","Ẳ","Ẵ","Ặ"};
    unhatted_map_["e"] = {"e","é","è","ẻ","ẽ","ẹ","E","É","È","Ẻ","Ẽ","Ẹ"};
    unhatted_map_["o"] = {"o","ó","ò","ỏ","õ","ọ","O","Ó","Ò","Ỏ","Õ","Ọ","ơ","ớ","ờ","ở","ỡ","ợ","Ơ","Ớ","Ờ","Ở","Ỡ","Ợ"};

    hatted_map_["a"] = {"â","ấ","ầ","ẩ","ẫ","ậ","Â","Ấ","Ầ","Ẩ","Ẫ","Ậ"};
    hatted_map_["e"] = {"ê","ế","ề","ể","ễ","ệ","Ê","Ế","Ề","Ể","Ễ","Ệ"};
    hatted_map_["o"] = {"ô","ố","ồ","ổ","ỗ","ộ","Ô","Ố","Ồ","Ổ","Ỗ","Ộ"};
}

EngineResult VietnameseEngine::processKey(char key) {
    if (!std::isalnum(static_cast<unsigned char>(key)) && key != '_') {
        resetBuffer();
        std::string s(1, key);
        return {EngineAction::RESET, 0, s};
    }

    std::string current_word = getCurrentWord();
    std::vector<char> test_keys = raw_keys_;
    test_keys.push_back(key);
    std::string new_word = evaluateSequence(test_keys);

    std::string expected_append = current_word + key;
    if (new_word != expected_append) {
        auto cur_chars = splitUtf8(current_word);
        auto new_chars = splitUtf8(new_word);

        size_t prefix_len = 0;
        size_t min_len = std::min(cur_chars.size(), new_chars.size());
        while (prefix_len < min_len && cur_chars[prefix_len] == new_chars[prefix_len]) {
            prefix_len++;
        }

        size_t backspace_count = cur_chars.size() - prefix_len;
        std::string insert_str = "";
        for (size_t k = prefix_len; k < new_chars.size(); ++k) {
            insert_str += new_chars[k];
        }
        raw_keys_.push_back(key);
        return {EngineAction::MODIFY, backspace_count, insert_str};
    } else {
        raw_keys_.push_back(key);
        std::string s(1, key);
        return {EngineAction::APPEND, 0, s};
    }
}

std::string VietnameseEngine::getCurrentWord() const {
    return evaluateSequence(raw_keys_);
}

bool VietnameseEngine::isLateHatModifier(const std::vector<char>& segment_keys, char lower_char) const {
    std::string lower(1, lower_char);
    if (lower != "a" && lower != "e" && lower != "o") return false;
    for (char k : segment_keys) {
        std::string ks(1, k);
        std::string base = ks;
        if (char_decompose_.count(ks)) {
            base = char_decompose_.at(ks).base;
        }
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        if (base == lower) return true;
    }
    return false;
}

std::vector<std::pair<size_t, size_t>> VietnameseEngine::splitRawSegments(const std::vector<char>& keys) const {
    std::vector<std::pair<size_t, size_t>> segments;
    if (keys.empty()) return segments;

    std::set<std::string> telex_tone_keys = {"s", "f", "r", "x", "j"};
    std::vector<std::string> valid_codas = {"c", "ch", "m", "n", "ng", "nh", "p", "t"};

    auto is_coda_prefix = [&](const std::string& val) {
        for (const auto& candidate : valid_codas) {
            if (candidate.rfind(val, 0) == 0) return true;
        }
        return false;
    };

    size_t segment_start = 0;
    bool has_vowel = false;
    std::string coda = "";

    for (size_t index = 0; index < keys.size(); ++index) {
        char k = keys[index];
        char lower_char = std::tolower(static_cast<unsigned char>(k));
        std::string lower(1, lower_char);

        if (lower == "w") {
            if (!has_vowel) has_vowel = true;
            continue;
        }
        if (telex_tone_keys.count(lower) || lower == "z") {
            if (has_vowel) continue;
        }
        if (std::string("aeiouy").find(lower_char) != std::string::npos) {
            if (has_vowel && !coda.empty()) {
                if ((lower == "a" || lower == "e" || lower == "o") &&
                    isLateHatModifier(std::vector<char>(keys.begin() + segment_start, keys.begin() + index), lower_char)) {
                    coda = "";
                    continue;
                }
                segments.push_back({segment_start, index});
                segment_start = index;
            }
            has_vowel = true;
            coda = "";
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(lower_char))) {
            if (index > segment_start) {
                segments.push_back({segment_start, index});
            }
            segment_start = index + 1;
            has_vowel = false;
            coda = "";
            continue;
        }
        if (!has_vowel) continue;

        std::string next_coda = coda + lower;
        if (!is_coda_prefix(next_coda)) {
            segments.push_back({segment_start, index});
            segment_start = index;
            has_vowel = false;
            coda = "";
            continue;
        }
        coda = next_coda;
    }

    if (segment_start < keys.size()) {
        segments.push_back({segment_start, keys.size()});
    }
    return segments;
}

std::pair<int, std::string> VietnameseEngine::findHatTarget(const std::vector<std::string>& chars, const std::string& base_vowel) const {
    if (!unhatted_map_.count(base_vowel)) return {-1, ""};

    const auto& unhatted = unhatted_map_.at(base_vowel);
    const auto& hatted = hatted_map_.at(base_vowel);

    int skipped_consonants = 0;
    for (int idx = static_cast<int>(chars.size()) - 1; idx >= 0; --idx) {
        std::string ch = chars[idx];
        bool is_vowel = char_decompose_.count(ch) || (ch.length() == 1 && std::string("aeiouyAEIOUY").find(ch[0]) != std::string::npos);
        if (!is_vowel) {
            if (skipped_consonants < 2 && ch.length() == 1 && std::isalpha(static_cast<unsigned char>(ch[0]))) {
                skipped_consonants++;
                continue;
            }
            break;
        }
        if (unhatted.count(ch)) return {idx, "ADD_HAT"};
        if (hatted.count(ch)) return {idx, "REMOVE_HAT"};
    }
    return {-1, ""};
}

std::string VietnameseEngine::evaluateSequence(const std::vector<char>& keys) const {
    if (keys.empty()) return "";

    auto segments = splitRawSegments(keys);
    if (segments.size() <= 1) {
        return evaluateSegment(keys);
    }

    std::string rendered = "";
    for (const auto& seg : segments) {
        std::vector<char> seg_keys(keys.begin() + seg.first, keys.begin() + seg.second);
        rendered += evaluateSegment(seg_keys);
    }
    return rendered;
}

std::string VietnameseEngine::evaluateSegment(const std::vector<char>& keys) const {
    if (keys.empty()) return "";

    std::string raw_str(keys.begin(), keys.end());
    std::set<std::string> protected_words = {
        "python", "password", "desktop", "windows", "google", "version",
        "linux", "raw", "javascript", "typescript", "telex", "vni",
        "terminal", "code", "chrome", "firefox", "libreoffice",
        "telegram", "discord", "zalo", "web", "latinh", "pre", "test",
        "best", "data", "apple", "start", "friend", "work"
    };
    std::string raw_lower = raw_str;
    std::transform(raw_lower.begin(), raw_lower.end(), raw_lower.begin(), ::tolower);
    if (protected_words.count(raw_lower)) return raw_str;

    std::vector<std::string> res_chars;
    int active_tone = 0;
    size_t i = 0;
    size_t n = keys.size();

    while (i < n) {
        char k_char = keys[i];
        std::string k(1, k_char);
        char lk_char = std::tolower(static_cast<unsigned char>(k_char));
        std::string lk(1, lk_char);

        // 'dd' -> 'đ'
        if (lk == "d") {
            if (i == 0) {
                size_t run_end = i;
                while (run_end < n && std::tolower(static_cast<unsigned char>(keys[run_end])) == 'd') {
                    run_end++;
                }
                size_t run_length = run_end - i;
                if (run_length % 2 == 0) {
                    res_chars.push_back(std::isupper(static_cast<unsigned char>(keys[0])) ? "Đ" : "đ");
                } else {
                    res_chars.push_back(std::string(1, keys[0]));
                }
                for (size_t literal = 0; literal < (run_length - 1) / 2; ++literal) {
                    res_chars.push_back(std::string(1, keys[run_length - 1 - literal]));
                }
                i = run_end;
                continue;
            }
            res_chars.push_back(k);
            i++;
            continue;
        }

        // Hat modifiers: a, e, o
        if (lk == "a" || lk == "e" || lk == "o") {
            if (lk == "o" && i + 1 < n && std::tolower(static_cast<unsigned char>(keys[i + 1])) == 'o' &&
                !res_chars.empty() && (res_chars.back() == "ư" || res_chars.back() == "Ư")) {
                res_chars.push_back(std::isupper(static_cast<unsigned char>(k_char)) ? "Ơ" : "ơ");
                i += 2;
                continue;
            }

            auto target = findHatTarget(res_chars, lk);
            if (target.first != -1) {
                if (target.second == "ADD_HAT") {
                    res_chars[target.first] = add_hat_map_.at(lk).at(res_chars[target.first]);
                    i++;
                    continue;
                } else if (target.second == "REMOVE_HAT") {
                    res_chars[target.first] = remove_hat_map_.at(lk).at(res_chars[target.first]);
                    res_chars.push_back(k);
                    i++;
                    continue;
                }
            }
        }

        // Telex tones
        std::map<std::string, int> tone_keys = {
            {"s", 1}, {"f", 2}, {"r", 3}, {"x", 4}, {"j", 5}
        };
        if (tone_keys.count(lk) || lk == "z") {
            bool has_vowel_already = false;
            for (const auto& ch : res_chars) {
                if (char_decompose_.count(ch) || (ch.length() == 1 && std::string("aeiouyAEIOUY").find(ch[0]) != std::string::npos)) {
                    has_vowel_already = true;
                    break;
                }
            }
            if (has_vowel_already) {
                active_tone = (lk == "z") ? 0 : tone_keys[lk];
                i++;
                continue;
            }
        }

        res_chars.push_back(k);
        i++;
    }

    std::string word_str = joinUtf8(res_chars);
    if (active_tone != 0) {
        word_str = applyToneToWord(word_str, active_tone);
    }
    return word_str;
}

std::string VietnameseEngine::applyToneToWord(const std::string& word, int tone) const {
    auto chars = splitUtf8(word);
    int target_vowel_idx = -1;
    for (int idx = 0; idx < static_cast<int>(chars.size()); ++idx) {
        if (char_decompose_.count(chars[idx])) {
            target_vowel_idx = idx;
            break;
        }
    }
    if (target_vowel_idx == -1) return word;

    std::string ch = chars[target_vowel_idx];
    VowelDecompose decomp = char_decompose_.at(ch);

    std::vector<VowelGroup> groups = {
        {"a", 0, {"a", "á", "à", "ả", "ã", "ạ"}},
        {"a", 1, {"â", "ấ", "ầ", "ẩ", "ẫ", "ậ"}},
        {"a", 2, {"ă", "ắ", "ằ", "ẳ", "ẵ", "ặ"}},
        {"e", 0, {"e", "é", "è", "ẻ", "ẽ", "ẹ"}},
        {"e", 1, {"ê", "ế", "ề", "ể", "ễ", "ệ"}},
        {"i", 0, {"i", "í", "ì", "ỉ", "ĩ", "ị"}},
        {"o", 0, {"o", "ó", "ò", "ỏ", "õ", "ọ"}},
        {"o", 1, {"ô", "ố", "ồ", "ổ", "ỗ", "ộ"}},
        {"o", 3, {"ơ", "ớ", "ờ", "ở", "ỡ", "ợ"}},
        {"u", 0, {"u", "ú", "ù", "ủ", "ũ", "ụ"}},
        {"u", 3, {"ư", "ứ", "ừ", "ử", "ữ", "ự"}},
        {"y", 0, {"y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"}}
    };

    for (const auto& g : groups) {
        if (g.base == decomp.base && g.hat == decomp.hat) {
            chars[target_vowel_idx] = g.tones[tone];
            return joinUtf8(chars);
        }
    }
    return word;
}
