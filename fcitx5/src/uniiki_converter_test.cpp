#include "uniiki_engine.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expectConvert(const std::string &raw, const std::string &expected) {
    const auto actual = fcitx::UniikiEngine::evaluateTelexForTest(raw);
    if (actual == expected) {
        return true;
    }
    std::cerr << "converter raw=" << raw << " actual=" << actual
              << " expected=" << expected << '\n';
    return false;
}

bool expectTrace(const std::string &raw, const std::string &onset,
                 const std::string &nucleus, const std::string &coda,
                 const std::string &tone, const std::string &target,
                 const std::string &consumed) {
    const auto trace = fcitx::UniikiEngine::conversionTraceForTest(raw);
    const std::vector<std::string> requiredFields = {
        "rawBuffer=" + raw,
        " tokens=", " consumedModifierIndexes=" + consumed,
        " parsedOnset=" + onset + " parsedVowelNucleus=" + nucleus,
        " parsedCoda=" + coda + " activeTone=" + tone,
        " toneTargetIndex=" + target,
        " rendered=", " fallbackReason=none",
    };
    const bool passed = std::all_of(
        requiredFields.begin(), requiredFields.end(),
        [&trace](const std::string &field) {
            return trace.find(field) != std::string::npos;
        });
    std::cout << "TRACE " << trace << '\n';
    if (!passed) {
        std::cerr << "trace assertion failed raw=" << raw << '\n';
    }
    return passed;
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::string>> required = {
        {"hoom", "hôm"}, {"khuyru", "khuỷu"},
        {"khueesch", "khuếch"}, {"quawnf", "quằn"},
        {"quaji", "quại"}, {"ngueeuf", "nguều"},
        {"ngoaof", "ngoào"},
        {"cana", "cân"},   {"canaf", "cần"}, {"canas", "cấn"},
        {"canar", "cẩn"}, {"canax", "cẫn"}, {"canaj", "cận"},
        {"bene", "bên"},   {"benes", "bến"}, {"benef", "bền"},
        {"bener", "bển"}, {"benex", "bễn"}, {"benej", "bện"},
        {"cono", "côn"},   {"conos", "cốn"},
        {"asaaaa", "áaaaa"}, {"afaaaa", "àaaaa"},
        {"araaaa", "ảaaaa"}, {"axaaaa", "ãaaaa"},
        {"ajaaaa", "ạaaaa"}, {"oroooo", "ỏoooo"},
        {"osoooo", "óoooo"}, {"ofoooo", "òoooo"},
        {"oxoooo", "õoooo"}, {"ojoooo", "ọoooo"},
        {"dd", "đ"},
    };
    bool requiredPassed = true;
    for (const auto &[raw, expected] : required) {
        if (!expectConvert(raw, expected)) {
            requiredPassed = false;
        }
    }
    if (!requiredPassed) {
        for (const auto &raw : {std::string("khuyru"), std::string("quawnf"),
                                std::string("quaji")}) {
            std::cerr << fcitx::UniikiEngine::conversionTraceForTest(raw) << '\n';
        }
        return 1;
    }

    const std::vector<std::tuple<std::string, std::string, std::string,
                                 std::string, std::string, std::string,
                                 std::string>>
        requiredTraces = {
            {"hoom", "h", "ô", "m", "none", "none", "[2]"},
            {"khuyru", "kh", "uyu", "", "hoi", "3", "[4]"},
            {"khueesch", "kh", "uê", "ch", "sac", "3", "[4,5]"},
            {"quawnf", "qu", "ă", "n", "huyen", "2", "[3,5]"},
            {"quaji", "qu", "ai", "", "nang", "2", "[3]"},
            {"ngueeuf", "ng", "uêu", "", "huyen", "3", "[4,6]"},
            {"ngoaof", "ng", "oao", "", "huyen", "3", "[5]"},
        };
    for (const auto &[raw, onset, nucleus, coda, tone, target, consumed] : requiredTraces) {
        if (!expectTrace(raw, onset, nucleus, coda, tone, target, consumed)) {
            return 1;
        }
    }

    const std::vector<std::string> atomicShapeFallback = {
        "delete", "deletee", "tele", "telephone", "element",
        "generate", "screen", "green", "meeting", "cheese",
        "feedback", "google", "book", "food", "good", "room",
        "school", "tool", "zoom", "facebook", "database", "banana",
        "camera", "bazaar", "aardvark", "Aachen", "moderator",
        "coconut",
    };
    for (const auto &raw : atomicShapeFallback) {
        if (!expectConvert(raw, raw)) {
            std::cerr << "atomic shape fallback failed\n";
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> shapeRegression = {
        {"teen", "tên"}, {"teens", "tến"},
        {"bene", "bên"}, {"benef", "bền"},
        {"cono", "côn"}, {"conos", "cốn"}, {"cana", "cân"},
        {"canaf", "cần"}, {"tieengs", "tiếng"},
        {"vieetj", "việt"}, {"Vieetj", "Việt"},
        {"coong", "công"},
        {"quas", "quá"}, {"quans", "quán"},
        {"quawnf", "quằn"}, {"quaij", "quại"},
        {"quayx", "quẫy"},
        {"giwax", "giữa"}, {"giafnh", "giành"},
        {"giucj", "giục"}, {"ngheej", "nghệ"},
        {"nghieeng", "nghiêng"}, {"khuyru", "khuỷu"},
        {"khueesch", "khuếch"}, {"ngoaof", "ngoào"},
    };
    for (const auto &[raw, expected] : shapeRegression) {
        if (!expectConvert(raw, expected)) {
            return 1;
        }
    }

    const std::vector<std::tuple<std::string, std::string, std::string,
                                 std::string, std::string, std::string,
                                 std::string>>
        regressionTraces = {
            {"quas", "qu", "a", "", "sac", "2", "[3]"},
            {"quans", "qu", "a", "n", "sac", "2", "[4]"},
            {"quawnf", "qu", "ă", "n", "huyen", "2", "[3,5]"},
            {"quaij", "qu", "ai", "", "nang", "2", "[4]"},
            {"quayx", "qu", "ây", "", "nga", "2", "[4]"},
            {"giwax", "gi", "ưa", "", "nga", "2", "[2,4]"},
            {"giafnh", "gi", "a", "nh", "huyen", "2", "[3]"},
            {"giucj", "gi", "u", "c", "nang", "2", "[4]"},
            {"ngheej", "ngh", "ê", "", "nang", "3", "[4,5]"},
            {"nghieeng", "ngh", "iê", "ng", "none", "none", "[5]"},
            {"khuyru", "kh", "uyu", "", "hoi", "3", "[4]"},
            {"khueesch", "kh", "uê", "ch", "sac", "3", "[4,5]"},
            {"ngoaof", "ng", "oao", "", "huyen", "3", "[5]"},
        };
    for (const auto &[raw, onset, nucleus, coda, tone, target, consumed] : regressionTraces) {
        if (!expectTrace(raw, onset, nucleus, coda, tone, target, consumed)) {
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> tonePlacementCases = {
        {"hoaf", "hòa"},       // oa, open: first vowel
        {"hoasnh", "hoánh"},  // oa + coda: second vowel
        {"hoer", "hỏe"},      // oe, open: first vowel
        {"hoets", "hoét"},    // oe + coda: second vowel
        {"tuys", "túy"},      // uy
        {"tuees", "tuế"},    // uê
        {"huowngs", "hướng"}, // ươ
        {"khuyaf", "khuỳa"}, // uya
        {"khuyru", "khuỷu"}, // uyu
        {"ngoais", "ngoái"}, // oai
        {"tuais", "tuái"},   // uai
        {"ngoaof", "ngoào"}, // oao
    };
    for (const auto &[raw, expected] : tonePlacementCases) {
        if (!expectConvert(raw, expected)) {
            std::cerr << "tone placement trace: "
                      << fcitx::UniikiEngine::conversionTraceForTest(raw) << '\n';
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> canaPrefixes = {
        {"c", "c"}, {"ca", "ca"}, {"can", "can"},
        {"cana", "cân"}, {"canaf", "cần"},
    };
    const std::vector<std::pair<std::string, std::string>> benePrefixes = {
        {"b", "b"}, {"be", "be"}, {"ben", "ben"}, {"bene", "bên"},
    };
    for (const auto &[raw, expected] : canaPrefixes) {
        if (!expectConvert(raw, expected)) {
            return 1;
        }
        std::cout << "PREFIX canaf raw=" << raw << " rendered=" << expected << '\n';
    }
    for (const auto &[raw, expected] : benePrefixes) {
        if (!expectConvert(raw, expected)) {
            return 1;
        }
        std::cout << "PREFIX bene raw=" << raw << " rendered=" << expected << '\n';
    }

    const std::vector<char> modifiers = {'a', 'e', 'o'};
    const std::vector<char> tones = {'s', 'f', 'r', 'x', 'j'};
    const std::vector<std::string> codas = {
        "c", "ch", "m", "n", "ng", "nh", "p", "t",
    };
    size_t matrixCases = 0;
    for (char modifier : modifiers) {
        for (char tone : tones) {
            for (const auto &coda : codas) {
                const std::string canonical =
                    std::string(2, modifier) + coda + tone;
                const auto expected =
                    fcitx::UniikiEngine::evaluateTelexForTest(canonical);
                const std::vector<std::string> placements = {
                    std::string(1, modifier) + coda + modifier + tone,
                    std::string(1, modifier) + coda + tone + modifier,
                };
                for (const auto &raw : placements) {
                    if (!expectConvert(raw, expected)) {
                        std::cerr << "matrix canonical=" << canonical << '\n';
                        return 1;
                    }
                    ++matrixCases;
                }
            }
        }
    }

    std::cout << "SEGMENT "
              << fcitx::UniikiEngine::converterSegmentationForTest("oroooo")
              << '\n';
    std::cout << "converter-cases=" << required.size()
              << " atomic-shape-fallback=" << atomicShapeFallback.size()
              << " shape-regression=" << shapeRegression.size()
              << " late-shape-matrix=" << matrixCases << " status=pass\n";
    return 0;
}
