#ifndef UNIIKI_ENGINE_H
#define UNIIKI_ENGINE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>

enum class EngineAction {
    APPEND,
    MODIFY,
    RESET
};

struct EngineResult {
    EngineAction action;
    size_t backspace_count;
    std::string text;
};

struct VowelDecompose {
    std::string base;
    int hat;
    int tone;
};

class VietnameseEngine {
public:
    VietnameseEngine(const std::string& mode = "telex", bool modern_tone = true);
    void resetBuffer();
    void setMode(const std::string& mode);
    EngineResult processKey(char key);
    std::string getCurrentWord() const;

private:
    std::string mode_;
    bool modern_tone_;
    std::vector<char> raw_keys_;

    std::map<std::string, VowelDecompose> char_decompose_;
    std::map<std::string, std::map<std::string, std::string>> add_hat_map_;
    std::map<std::string, std::map<std::string, std::string>> remove_hat_map_;
    std::map<std::string, std::set<std::string>> unhatted_map_;
    std::map<std::string, std::set<std::string>> hatted_map_;

    void initMaps();
    bool isLateHatModifier(const std::vector<char>& segment_keys, char lower_char) const;
    std::vector<std::pair<size_t, size_t>> splitRawSegments(const std::vector<char>& keys) const;
    std::pair<int, std::string> findHatTarget(const std::vector<std::string>& chars, const std::string& base_vowel) const;
    std::string evaluateSequence(const std::vector<char>& keys) const;
    std::string evaluateSegment(const std::vector<char>& keys) const;
    std::string applyToneToWord(const std::string& word, int tone) const;
};

#endif // UNIIKI_ENGINE_H
