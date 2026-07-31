#include "engine.h"
#include <algorithm>
#include <cctype>
#include <iostream>

// Vowel transformation lookup tables
static const std::vector<std::string> VOWEL_A = {"a", "á", "à", "ả", "ã", "ạ"};
static const std::vector<std::string> VOWEL_AH = {"â", "ấ", "ầ", "ẩ", "ẫ", "ậ"};
static const std::vector<std::string> VOWEL_AW = {"ă", "ắ", "ằ", "ẳ", "ẵ", "ặ"};

static const std::vector<std::string> VOWEL_E = {"e", "é", "è", 'ẻ', "ẽ", "ẹ"};
static const std::vector<std::string> VOWEL_EH = {"ê", "ế", "ề", "ể", "ễ", "ệ"};

static const std::vector<std::string> VOWEL_I = {"i", "í", "ì", "ỉ", "ĩ", "ị"};

static const std::vector<std::string> VOWEL_O = {"o", "ó", "ò", "ỏ", "õ", "ọ"};
static const std::vector<std::string> VOWEL_OH = {"ô", "ố", "ồ", "ổ", "ỗ", "ộ"};
static const std::vector<std::string> VOWEL_OW = {"ơ", "ớ", "ờ", "ở", "ỡ", "ợ"};

static const std::vector<std::string> VOWEL_U = {"u", "ú", "ù", "ủ", "ũ", "ụ"};
static const std::vector<std::string> VOWEL_UW = {"ư", "ứ", "ừ", "ử", "ữ", "ự"};

static const std::vector<std::string> VOWEL_Y = {"y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"};

VietnameseEngine::VietnameseEngine(const std::string& mode, bool modern_tone)
    : mode_(mode), modern_tone_(modern_tone) {
    resetBuffer();
}

void VietnameseEngine::resetBuffer() {
    raw_keys_.clear();
}

void VietnameseEngine::setMode(const std::string& mode) {
    mode_ = mode;
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
        size_t prefix_len = 0;
        size_t min_len = std::min(current_word.length(), new_word.length());
        while (prefix_len < min_len && current_word[prefix_len] == new_word[prefix_len]) {
            prefix_len++;
        }
        size_t backspace_count = current_word.length() - prefix_len;
        std::string insert_str = new_word.substr(prefix_len);
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

std::string VietnameseEngine::evaluateSequence(const std::vector<char>& keys) const {
    if (keys.empty()) return "";

    std::string raw_str(keys.begin(), keys.end());

    // Simple Telex transformation logic in C++
    std::string word = "";
    int tone = 0; // 0: none, 1: sac, 2: huyen, 3: hoi, 4: nga, 5: nang

    for (size_t i = 0; i < keys.size(); ++i) {
        char c = keys[i];
        char lc = std::tolower(static_cast<unsigned char>(c));

        if (lc == 'd' && !word.empty() && std::tolower(static_cast<unsigned char>(word.back())) == 'd') {
            word.pop_back();
            word += "đ";
            continue;
        }

        if (lc == 'a' && !word.empty() && word.back() == 'a') {
            word.pop_back();
            word += "â";
            continue;
        }

        if (lc == 'e' && !word.empty() && word.back() == 'e') {
            word.pop_back();
            word += "ê";
            continue;
        }

        if (lc == 'o' && !word.empty() && word.back() == 'o') {
            word.pop_back();
            word += "ô";
            continue;
        }

        if (lc == 'w' && !word.empty()) {
            if (word.back() == 'a') { word.pop_back(); word += "ă"; continue; }
            if (word.back() == 'o') { word.pop_back(); word += "ơ"; continue; }
            if (word.back() == 'u') { word.pop_back(); word += "ư"; continue; }
        }

        // Telex tones
        if (lc == 's') { tone = 1; continue; }
        if (lc == 'f') { tone = 2; continue; }
        if (lc == 'r') { tone = 3; continue; }
        if (lc == 'x') { tone = 4; continue; }
        if (lc == 'j') { tone = 5; continue; }
        if (lc == 'z') { tone = 0; continue; }

        word += c;
    }

    if (tone != 0) {
        return applyToneToWord(word, tone);
    }

    return word;
}

std::string VietnameseEngine::applyToneToWord(const std::string& word, int tone) const {
    std::string res = word;
    // Replace vowels with accented versions based on tone
    if (res.find("a") != std::string::npos) {
        size_t pos = res.find("a");
        res.replace(pos, 1, VOWEL_A[tone]);
    } else if (res.find("â") != std::string::npos) {
        size_t pos = res.find("â");
        res.replace(pos, 2, VOWEL_AH[tone]);
    } else if (res.find("ă") != std::string::npos) {
        size_t pos = res.find("ă");
        res.replace(pos, 2, VOWEL_AW[tone]);
    } else if (res.find("e") != std::string::npos) {
        size_t pos = res.find("e");
        res.replace(pos, 1, VOWEL_E[tone]);
    } else if (res.find("ê") != std::string::npos) {
        size_t pos = res.find("ê");
        res.replace(pos, 2, VOWEL_EH[tone]);
    } else if (res.find("o") != std::string::npos) {
        size_t pos = res.find("o");
        res.replace(pos, 1, VOWEL_O[tone]);
    } else if (res.find("ô") != std::string::npos) {
        size_t pos = res.find("ô");
        res.replace(pos, 2, VOWEL_OH[tone]);
    } else if (res.find("ơ") != std::string::npos) {
        size_t pos = res.find("ơ");
        res.replace(pos, 2, VOWEL_OW[tone]);
    } else if (res.find("u") != std::string::npos) {
        size_t pos = res.find("u");
        res.replace(pos, 1, VOWEL_U[tone]);
    } else if (res.find("ư") != std::string::npos) {
        size_t pos = res.find("ư");
        res.replace(pos, 2, VOWEL_UW[tone]);
    } else if (res.find("i") != std::string::npos) {
        size_t pos = res.find("i");
        res.replace(pos, 1, VOWEL_I[tone]);
    } else if (res.find("y") != std::string::npos) {
        size_t pos = res.find("y");
        res.replace(pos, 1, VOWEL_Y[tone]);
    }

    return res;
}
