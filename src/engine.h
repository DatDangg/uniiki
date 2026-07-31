#ifndef UNIIKI_ENGINE_H
#define UNIIKI_ENGINE_H

#include <string>
#include <vector>
#include <map>
#include <set>

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

    std::string evaluateSequence(const std::vector<char>& keys) const;
    std::string applyToneToWord(const std::string& word, int tone) const;
};

#endif // UNIIKI_ENGINE_H
