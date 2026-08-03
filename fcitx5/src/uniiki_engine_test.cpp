#include "uniiki_engine.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_set>
#include <vector>

int main() {
    const std::vector<std::tuple<std::string, std::string, unsigned int, std::string>>
        replacementCases = {
            {"", "t", 0, "t"},
            {"t", "te", 0, "e"},
            {"te", "té", 1, "é"},
            {"té", "tes", 1, "es"},
            {"tes", "test", 0, "t"},
            {"same", "same", 0, ""},
            {"thi", "thì", 1, "ì"},
            {"viêt", "việt", 2, "ệt"},
            {"đươc", "được", 2, "ợc"},
            {"dod", "dodd", 0, "d"},
            {"phá", "phát", 0, "t"},
            {"lịch", "lích", 3, "ích"},
            {"lịc", "lịch", 0, "h"},
            {"go\xCC\x83", "g", 1, ""},
        };
    for (const auto &[oldText, newText, expectedDelete, expectedInsert] : replacementCases) {
        auto [actualDelete, actualInsert] =
            fcitx::UniikiEngine::replacementDeltaForTest(oldText, newText);
        if (actualDelete != expectedDelete || actualInsert != expectedInsert) {
            std::cerr << "replace " << oldText << " -> " << newText
                      << ": delete=" << actualDelete << ", insert=" << actualInsert
                      << "; expected delete=" << expectedDelete
                      << ", insert=" << expectedInsert << "\n";
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> cases = {
        {"heo", "heo"},
        {"d", "d"},
        {"dd", "đ"},
        {"ddd", "dd"},
        {"dddd", "ddd"},
        {"datdang", "đatang"},
        {"ddatdang", "datdang"},
        {"datddang", "datdang"},
        {"datdddang", "datddang"},
        {"datd", "đat"},
        {"datdd", "datd"},
        {"datdda", "datda"},
        {"datddan", "datdan"},
        {"dadang", "đâng"},
        {"dodd", "dod"},
        {"doddd", "dodd"},
        {"drop", "drop"},
        {"case", "case"},
        {"base", "base"},
        {"test", "test"},
        {"best", "best"},
        {"asset", "aset"},
        {"class", "class"},
        {"pass", "pass"},
        {"mass", "mas"},
        {"style", "style"},
        {"system", "system"},
        {"screen", "screen"},
        {"frame", "frame"},
        {"from", "from"},
        {"order", "order"},
        {"browser", "browser"},
        {"proxy", "proxy"},
        {"axis", "axis"},
        {"ajax", "ajax"},
        {"json", "json"},
        {"project", "project"},
        {"ycx", "ycx"},
        {"ycxl", "ycxl"},
        {"ycxlx", "ycxlx"},
        {"bcx", "bcx"},
        {"clx", "clx"},
        {"tsx", "tsx"},
        {"jsx", "jsx"},
        {"book", "book"},
        {"food", "food"},
        {"room", "room"},
        {"tool", "tool"},
        {"email", "email"},
        {"feedback", "feedback"},
        {"database", "database"},
        {"address", "address"},
        {"middleware", "middleware"},
        {"add", "ad"},
        {"addd", "add"},
        {"ddu", "đu"},
        {"dduo", "đuo"},
        {"dduow", "đươ"},
        {"dduowngf", "đường"},
        {"dduowjc", "được"},
        {"dduwojc", "được"},
        {"ddwojc", "được"},
        {"dduwongf", "đường"},
        {"ddwongf", "đường"},
        {"dduowcj", "được"},
        {"w", "ư"},
        {"ww", "w"},
        {"uw", "ư"},
        {"uww", "uw"},
        {"ow", "ơ"},
        {"oww", "ow"},
        {"aw", "ă"},
        {"aww", "aw"},
        {"aa", "â"},
        {"aaa", "aa"},
        {"ee", "ê"},
        {"eee", "ee"},
        {"oo", "ô"},
        {"ooo", "oo"},
        {"as", "á"},
        {"ass", "as"},
        {"af", "à"},
        {"aff", "af"},
        {"ar", "ả"},
        {"arr", "ar"},
        {"ax", "ã"},
        {"axx", "ax"},
        {"aj", "ạ"},
        {"ajj", "aj"},
        {"tirr", "tir"},
        {"casse", "case"},
        {"phast", "phát"},
        {"phats", "phát"},
        {"asp", "áp"},
        {"aps", "áp"},
        {"basn", "bán"},
        {"bans", "bán"},
        {"lafm", "làm"},
        {"lamf", "làm"},
        {"najng", "nạng"},
        {"nangj", "nạng"},
        {"nawjng", "nặng"},
        {"nawngj", "nặng"},
        {"lijch", "lịch"},
        {"lichj", "lịch"},
        {"lichjs", "lích"},
        {"lichjsj", "lịch"},
        {"lichjsjs", "lích"},
        {"lichs", "lích"},
        {"lichsj", "lịch"},
        {"lijchsjs", "lích"},
        {"licjh", "lịch"},
        {"lischj", "lịch"},
        {"lichjj", "lichj"},
        {"lijchjj", "lichjj"},
        {"lisch", "lích"},
        {"lischss", "lichss"},
        {"lifch", "lìch"},
        {"lifchff", "lichff"},
        {"lirch", "lỉch"},
        {"lirchrr", "lichrr"},
        {"lixch", "lĩch"},
        {"lixchxx", "lichxx"},
        {"lichjjj", "lichjj"},
        {"lichsss", "lichss"},
        {"licshss", "lichss"},
        {"licfhff", "lichff"},
        {"licrhrr", "lichrr"},
        {"licxhxx", "lichxx"},
        {"licjhjj", "lichjj"},
        {"wW", "w"},
        {"Ww", "W"},
        {"W", "Ư"},
        {"WW", "W"},
        {"Ws", "Ứ"},
        {"c", "c"},
        {"ch", "ch"},
        {"chu", "chu"},
        {"chua", "chua"},
        {"chuaw", "chưa"},
        {"chuaww", "chuaw"},
        {"chuw", "chư"},
        {"muaw", "mưa"},
        {"thuaw", "thưa"},
        {"duaw", "dưa"},
        {"tuowngf", "tường"},
        {"nguowif", "người"},
        {"duowjc", "dược"},
        {"nuowcs", "nước"},
        {"nuwocs", "nước"},
        {"nwocs", "nước"},
        {"truowngf", "trường"},
        {"truwongf", "trường"},
        {"trwongf", "trường"},
        {"truowcs", "trước"},
        {"truwocs", "trước"},
        {"trwocs", "trước"},
        {"thuowng", "thương"},
        {"thuwong", "thương"},
        {"thwong", "thương"},
        {"chuowng", "chương"},
        {"chuwong", "chương"},
        {"chwong", "chương"},
        {"phuowng", "phương"},
        {"phuwong", "phương"},
        {"phwong", "phương"},
        {"huowngs", "hướng"},
        {"huwongs", "hướng"},
        {"hwongs", "hướng"},
        {"tuowngr", "tưởng"},
        {"tuwongr", "tưởng"},
        {"twongr", "tưởng"},
        {"vuownf", "vườn"},
        {"vuwonf", "vườn"},
        {"vwonf", "vườn"},
        {"thuowngf", "thường"},
        {"dduocw", "đuơc"},
        {"dduocW", "đuơc"},
        {"dduocww", "đuọcw"},
        {"dduocwW", "đuọcw"},
        {"dduocwjw", "đuọcw"},
        {"dduocwjW", "đuọcw"},
        {"dduowcjw", "đuọcw"},
        {"dduowcjW", "đuọcw"},
        {"pre", "pre"},
        {"google", "google"},
        {"gooogle", "google"},
        {"khoe", "khoe"},
        {"khoer", "khỏe"},
        {"hoawcj", "hoặc"},
        {"ngoafi", "ngoài"},
        {"nhieeuf", "nhiều"},
        {"kieeur", "kiểu"},
        {"chuyeenr", "chuyển"},
        {"traan", "trân"},
        {"trana", "trân"},
        {"troon", "trôn"},
        {"trono", "trôn"},
        {"tieengs", "tiếng"},
        {"vieetj", "việt"},
        {"Typescript", "Typescript"},
        {"typescript", "typescript"},
        {"Telex", "Telex"},
        {"Tele", "Tele"},
        {"Telee", "Telee"},
        {"telegram", "telegram"},
        {"telee", "telee"},
        {"Chrome", "Chrome"},
        {"Telegram", "Telegram"},
        {"Telergam", "Telergam"},
        {"telegam", "telegam"},
        {"Discord", "Discord"},
        {"LibreOffice", "LibreOffice"},
        {"Web", "Web"},
        {"Windows", "Windows"},
        {"Google", "Google"},
        {"version", "version"},
        {"raw", "raw"},
        {"giuwx", "giữ"},
        {"duaw", "dưa"},
        {"dduaw", "đưa"},
        {"dduaww", "đuaw"},
        {"dduwaw", "đuaw"},
        {"ddwaw", "đuaw"},
        {"uwaw", "uaw"},
        {"duaww", "duaw"},
        {"thuaww", "thuaw"},
        {"muaww", "muaw"},
        {"duow", "dươ"},
        {"dduow", "đươ"},
        {"thuowng", "thương"},
        {"chuowng", "chương"},
        {"duoww", "duow"},
        {"dduoww", "đuow"},
        {"thuowwng", "thuowng"},
        {"giw", "giư"},
        {"giwa", "giưa"},
        {"giwax", "giữa"},
        {"qua", "qua"},
        {"quas", "quá"},
        {"quan", "quan"},
        {"quans", "quán"},
        {"window", "window"},
        {"workflow", "workflow"},
        {"show", "show"},
        {"power", "power"},
        {"tower", "tower"},
        {"flower", "flower"},
        {"work", "work"},
        {"word", "word"},
        {"world", "world"},
        {"woke", "woke"},
        {"sword", "sword"},
        {"network", "network"},
        {"framework", "framework"},
    };

    for (const auto &[raw, expected] : cases) {
        auto actual = fcitx::UniikiEngine::evaluateTelexForTest(raw);
        if (actual != expected) {
            std::cerr << raw << " -> " << actual << ", expected " << expected << "\n";
            return 1;
        }
    }

    const auto ownershipTrace =
        fcitx::UniikiEngine::toneOwnershipForTest("lijchjj");
    const std::string expectedOwnershipTrace =
        "tokens=[Literal(l@0),Literal(i@1),ToneModifier(j@2),"
        "Literal(c@3),Literal(h@4),EscapedLiteral(j@5),"
        "EscapedLiteral(j@6)] baseLetters=lichjj "
        "removedModifierIndexes=[2] escapedLiteralIndexes=[5,6] "
        "activeTone=none rendered=lichjj";
    if (ownershipTrace != expectedOwnershipTrace) {
        std::cerr << "ownership trace: " << ownershipTrace
                  << "\nexpected: " << expectedOwnershipTrace << "\n";
        return 1;
    }

    const std::vector<std::vector<std::pair<std::string, std::string>>> prefixCases = {
        {
            {"d", "d"},
            {"da", "da"},
            {"dat", "dat"},
            {"datd", "đat"},
            {"datda", "đât"},
            {"datdan", "đatan"},
            {"datdang", "đatang"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"dda", "đa"},
            {"ddat", "đat"},
            {"ddatd", "datd"},
            {"ddatda", "datda"},
            {"ddatdan", "datdan"},
            {"ddatdang", "datdang"},
        },
        {
            {"d", "d"},
            {"do", "do"},
            {"dod", "đo"},
            {"dodd", "dod"},
        },
        {
            {"d", "d"},
            {"do", "do"},
            {"dod", "đo"},
            {"dodd", "dod"},
            {"doddd", "dodd"},
        },
        {
            {"d", "d"},
            {"dr", "dr"},
            {"dro", "dro"},
            {"drop", "drop"},
        },
        {
            {"c", "c"},
            {"ca", "ca"},
            {"cas", "cá"},
            {"case", "case"},
        },
        {
            {"c", "c"},
            {"ca", "ca"},
            {"cas", "cá"},
            {"cass", "cas"},
            {"casse", "case"},
        },
        {
            {"t", "t"},
            {"ti", "ti"},
            {"tir", "tỉ"},
            {"tirr", "tir"},
        },
        {
            {"y", "y"},
            {"yc", "yc"},
            {"ycx", "ycx"},
            {"ycxl", "ycxl"},
            {"ycxlx", "ycxlx"},
        },
        {
            {"d", "d"},
            {"da", "da"},
            {"dat", "dat"},
            {"datd", "đat"},
            {"datdd", "datd"},
            {"datdda", "datda"},
            {"datddan", "datdan"},
            {"datddang", "datdang"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"ddu", "đu"},
            {"ddua", "đua"},
            {"dduaw", "đưa"},
            {"dduaww", "đuaw"},
        },
        {
            {"g", "g"},
            {"gi", "gi"},
            {"giw", "giư"},
            {"giwa", "giưa"},
            {"giwax", "giữa"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"ddu", "đu"},
            {"dduw", "đư"},
            {"dduwa", "đưa"},
            {"dduwaw", "đuaw"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"ddw", "đư"},
            {"ddwa", "đưa"},
            {"ddwaw", "đuaw"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"ddw", "đư"},
            {"ddwo", "đươ"},
            {"ddwoj", "đượ"},
            {"ddwojc", "được"},
        },
        {
            {"d", "d"},
            {"dd", "đ"},
            {"ddu", "đu"},
            {"dduw", "đư"},
            {"dduwo", "đươ"},
            {"dduwoj", "đượ"},
            {"dduwojc", "được"},
        },
        {
            {"a", "a"},
            {"as", "á"},
            {"asp", "áp"},
        },
        {
            {"p", "p"},
            {"ph", "ph"},
            {"pha", "pha"},
            {"phas", "phá"},
            {"phast", "phát"},
        },
        {
            {"p", "p"},
            {"ph", "ph"},
            {"pha", "pha"},
            {"phat", "phat"},
            {"phats", "phát"},
        },
        {
            {"l", "l"},
            {"li", "li"},
            {"lij", "lị"},
            {"lijc", "lịc"},
            {"lijch", "lịch"},
            {"lijchs", "lích"},
            {"lijchsj", "lịch"},
            {"lijchsjs", "lích"},
        },
        {
            {"l", "l"},
            {"li", "li"},
            {"lic", "lic"},
            {"lich", "lich"},
            {"lichj", "lịch"},
            {"lichjs", "lích"},
            {"lichjsj", "lịch"},
            {"lichjsjs", "lích"},
        },
        {
            {"l", "l"},
            {"li", "li"},
            {"lic", "lic"},
            {"lich", "lich"},
            {"lichj", "lịch"},
            {"lichjj", "lichj"},
        },
        {
            {"l", "l"},
            {"li", "li"},
            {"lij", "lị"},
            {"lijc", "lịc"},
            {"lijch", "lịch"},
            {"lijchj", "lichj"},
            {"lijchjj", "lichjj"},
        },
    };
    for (const auto &sequence : prefixCases) {
        for (const auto &[prefix, expected] : sequence) {
            const auto converted =
                fcitx::UniikiEngine::evaluateTelexForTest(prefix);
            if (converted != expected) {
                std::cerr << "prefix converter " << prefix << " -> "
                          << converted << ", expected " << expected << "\n";
                return 1;
            }
            const auto visible =
                fcitx::UniikiEngine::simulateDirectForTest(prefix);
            if (visible != expected) {
                std::cerr << "prefix visible " << prefix << " -> " << visible
                          << ", expected " << expected << "\n";
                return 1;
            }
        }
    }

    const std::vector<char> matrixVowels = {'a', 'e', 'i', 'o', 'u', 'y'};
    const std::vector<char> matrixTones = {'s', 'f', 'r', 'x', 'j'};
    const std::vector<std::string> matrixCodas = {
        "c", "m", "n", "p", "t", "ch", "ng", "nh",
    };
    size_t validMatrixCombinations = 0;
    for (char vowel : matrixVowels) {
        for (char tone : matrixTones) {
            for (const auto &coda : matrixCodas) {
                const std::string canonical =
                    std::string(1, vowel) + coda + tone;
                const auto expected =
                    fcitx::UniikiEngine::evaluateTelexForTest(canonical);
                const bool transformed =
                    std::any_of(expected.begin(), expected.end(), [](char ch) {
                        return static_cast<unsigned char>(ch) >= 128;
                    });
                if (!transformed) {
                    continue;
                }
                ++validMatrixCombinations;
                std::vector<std::string> placements = {
                    std::string(1, vowel) + tone + coda,
                    canonical,
                };
                if (coda.size() == 2) {
                    placements.push_back(
                        std::string(1, vowel) + coda.substr(0, 1) +
                        tone + coda.substr(1));
                }
                for (const auto &raw : placements) {
                    const auto converted =
                        fcitx::UniikiEngine::evaluateTelexForTest(raw);
                    if (converted != expected) {
                        std::cerr << "tone matrix converter " << raw << " -> "
                                  << converted << ", expected " << expected
                                  << " from " << canonical << "\n";
                        return 1;
                    }
                    const auto visible =
                        fcitx::UniikiEngine::simulateDirectForTest(raw);
                    if (visible != expected) {
                        std::cerr << "tone matrix visible " << raw << " -> "
                                  << visible << ", expected " << expected
                                  << " from " << canonical << "\n";
                        return 1;
                    }
                }
            }
        }
    }
    if (validMatrixCombinations < 200) {
        std::cerr << "tone matrix covered only " << validMatrixCombinations
                  << " valid combinations, expected at least 200\n";
        return 1;
    }

    for (char firstTone : matrixTones) {
        for (char secondTone : matrixTones) {
            const std::string raw =
                std::string("a") + firstTone + secondTone;
            const std::string expected =
                firstTone == secondTone
                    ? std::string("a") + firstTone
                    : fcitx::UniikiEngine::evaluateTelexForTest(
                          std::string("a") + secondTone);
            const auto converted =
                fcitx::UniikiEngine::evaluateTelexForTest(raw);
            const auto visible =
                fcitx::UniikiEngine::simulateDirectForTest(raw);
            if (converted != expected || visible != expected) {
                std::cerr << "tone pair " << raw << ": converter="
                          << converted << ", visible=" << visible
                          << ", expected=" << expected << "\n";
                return 1;
            }
        }
    }

    const std::vector<std::pair<std::string, std::string>> burstCases = {
        {"test", "test"}, {"tieesng", "tiếng"},
        {"vieetj", "việt"}, {"ddaay", "đây"},
        {"laf", "là"}, {"vawn", "văn"}, {"banr", "bản"},
        {"ddaay laf vawn banr test gox tieesng Vieetj",
         "đây là văn bản test gõ tiếng Việt"},
    };
    for (const auto &[raw, expected] : burstCases) {
        const auto actual = fcitx::UniikiEngine::simulateDirectForTest(raw);
        if (actual != expected) {
            std::cerr << "burst baseline " << raw << " -> " << actual
                      << ", expected " << expected << "\n";
            return 1;
        }
    }

    struct StressEvent {
        uint64_t eventId;
        char key;
        unsigned int delayMs;
    };
    std::mt19937 stressRandom(0x554E494B);
    std::uniform_int_distribution<unsigned int> delayDistribution(0, 100);
    const auto &stressInput = burstCases.back().first;
    const auto &stressExpected = burstCases.back().second;
    for (size_t iteration = 0; iteration < 1000; ++iteration) {
        std::deque<StressEvent> queue;
        uint64_t nextEventId = 1;
        for (char key : stressInput) {
            StressEvent event{nextEventId++, key,
                              delayDistribution(stressRandom)};
            queue.push_back(event);
            if (iteration % 17 == 0 && event.eventId % 11 == 0) {
                queue.push_back(event);
            }
        }
        std::unordered_set<uint64_t> appliedEvents;
        std::string appliedRaw;
        while (!queue.empty()) {
            const auto event = queue.front();
            queue.pop_front();
            if (event.delayMs > 100) {
                std::cerr << "stress delay outside 0..100 ms\n";
                return 1;
            }
            if (!appliedEvents.insert(event.eventId).second) {
                continue;
            }
            appliedRaw.push_back(event.key);
        }
        if (appliedRaw != stressInput ||
            appliedEvents.size() != stressInput.size()) {
            std::cerr << "stress ownership failed at iteration "
                      << iteration << "\n";
            return 1;
        }
        const auto rendered =
            fcitx::UniikiEngine::simulateDirectForTest(appliedRaw);
        if (rendered != stressExpected) {
            std::cerr << "stress render failed at iteration " << iteration
                      << ": " << rendered << "\n";
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> directCases = {
        {"dduaw", "đưa"},
        {"dduaww", "đuaw"},
        {"dduwaw", "đuaw"},
        {"ddwaw", "đuaw"},
        {"duoww", "duow"},
        {"dduoww", "đuow"},
        {"thuowwng", "thuowng"},
        {"giwax", "giữa"},
        {"quas", "quá"},
        {"quans", "quán"},
        {"window", "window"},
        {"workflow", "workflow"},
        {"show", "show"},
        {"power", "power"},
        {"tower", "tower"},
        {"flower", "flower"},
        {"dduwojc", "được"},
        {"ddwojc", "được"},
        {"dduwongf", "đường"},
        {"ddwongf", "đường"},
        {"nuwocs", "nước"},
        {"nwocs", "nước"},
        {"truwocs", "trước"},
        {"trwocs", "trước"},
        {"truwongf", "trường"},
        {"trwongf", "trường"},
        {"thuwong", "thương"},
        {"thwong", "thương"},
        {"chuwong", "chương"},
        {"chwong", "chương"},
        {"phuwong", "phương"},
        {"phwong", "phương"},
        {"huwongs", "hướng"},
        {"hwongs", "hướng"},
        {"tuwongr", "tưởng"},
        {"twongr", "tưởng"},
        {"vuwonf", "vườn"},
        {"vwonf", "vườn"},
        {"work word world woke sword network framework",
         "work word world woke sword network framework"},
        {"phast phats basn bans lafm lamf najng nangj nawjng nawngj lijch lichj",
         "phát phát bán bán làm làm nạng nạng nặng nặng lịch lịch"},
        {"asp aps licjh lischj", "áp áp lịch lịch"},
        {"lijchjj lischss lifchff lirchrr lixchxx",
         "lichjj lichss lichff lichrr lichxx"},
        {"lichjjj lichsss licshss licfhff licrhrr licxhxx licjhjj",
         "lichjj lichss lichss lichff lichrr lichxx lichjj"},
        {"lichj lichjs lichjsj lichjsjs lijchsjs lichjj",
         "lịch lích lịch lích lích lichj"},
        {"banj", "bạn"},
        {"bajn", "bạn"},
        {"leenhj", "lệnh"},
        {"lejenh", "lệnh"},
        {"moiws", "mới"},
        {"ddooir", "đổi"},
        {"ddeer", "để"},
        {"suwra", "sửa"},
        {"nos", "nó"},
        {"chuaanr", "chuẩn"},
        {"heej", "hệ"},
        {"ddi", "đi"},
        {"laapj", "lập"},
        {"ddaauf", "đầu"},
        {"ddauaf", "đầu"},
        {"ddaafu", "đầu"},
        {"ddaau", "đâu"},
        {"vieetj", "việt"},
        {"vieetjnam", "việtnam"},
        {"duowcj", "dược"},
        {"duowcjabc", "dượcabc"},
        {"dd", "đ"},
        {"dduowjc", "được"},
        {"dduowcj", "được"},
        {"dduowcjabc", "đượcabc"},
        {"vowis", "với"},
        {"nguoonf", "nguồn"},
        {"buowcs", "bước"},
        {"ddaaufvieets", "đầuviết"},
        {"moiwsddooir", "mớiđổi"},
        {"bajn hayx chayj leenhj caif ddawtj thuw vieenj owr buowcs 1 truowcs sau ddos baos minhf ddeer chungs ta bawts ddaauf vieets max nguoonf",
         "bạn hãy chạy lệnh cài đặt thư viện ở bước 1 trước sau đó báo mình để chúng ta bắt đầu viết mã nguồn"},
        {"ddaay laf huwongs ddi chuaanr chuyeen nghieepj cuar casc heej thoongs booj gox linux",
         "đây là hướng đi chuẩn chuyên nghiệp của các hệ thống bộ gõ linux"},
        {"chinhs thuwcs thanhf laapj chuyeenj cuar chungs ta",
         "chính thức thành lập chuyện của chúng ta"},
        {"ddaauf ddaauf ddaauf ddaauf ddaauf", "đầu đầu đầu đầu đầu"},
        {"bajn,", "bạn,"},
        {"bajn.", "bạn."},
        {"bajn:", "bạn:"},
        {"bajn;", "bạn;"},
        {"bajn?", "bạn?"},
        {"bajn!", "bạn!"},
        {"thi \bf", "thì"},
        {"thif\n", "thì\n"},
        {"nhaajn", "nhận"},
        {"mootj soos", "một số"},
        {"neeus", "nếu"},
        {"treen", "trên"},
        {"cofn", "còn"},
        {"minhf", "mình"},
        {"dduowcj", "được"},
        {"vieetj", "việt"},
        {"vaayj", "vậy"},
        {"roofi", "rồi"},
        {"looxi", "lỗi"},
        {"thoarng", "thoảng"},
        {"mooxi", "mỗi"},
        {"vowis", "với"},
        {"car", "cả"},
        {"phair", "phải"},
        {"laanf", "lần"},
        {"mowis", "mới"},
        {"guwri", "gửi"},
        {"carm", "cảm"},
        {"vaanx", "vẫn"},
        {"phaafn", "phần"},
        {"maast", "mất"},
        {"tieengs", "tiếng"},
        {"neeus tooi gox treen ChatGPT thif phair nhaajn tieengs Vieetj ddaafy ddur",
         "nếu tôi gõ trên ChatGPT thì phải nhận tiếng Việt đầy đủ"},
        {"minhf ddang kieemr tra booj gox tieengs Vieetj",
         "mình đang kiểm tra bộ gõ tiếng Việt"},
        {"tes", "té"},
        {"tess", "tes"},
        {"test", "test"},
        {"tesst", "test"},
        {"tesst tesst tesst", "test test test"},
        {"w ww wW Ww W WW", "ư w w W Ư W"},
        {"w ww uw uww ow oww aw aww chuaw chuaww chuw muaw thuaw duaw",
         "ư w ư uw ơ ow ă aw chưa chuaw chư mưa thưa dưa"},
        {"d dd ddd dddd datdang ddatdang dodd doddd",
         "d đ dd ddd đatang datdang dod dodd"},
        {"chuaw\b", "chư"},
        {"chuaww", "chuaw"},
        {"tuowngf nguowif dduowjc nuowcs truowngf thuowngf",
         "tường người được nước trường thường"},
        {"dduocw dduocW dduocww dduocwW dduocwjw dduocwjW dduowcjw dduowcjW",
         "đuơc đuơc đuọcw đuọcw đuọcw đuọcw đuọcw đuọcw"},
        {"thaays vaanx dduowcj", "thấy vẫn được"},
        {"tesst kh thaays chuwx, lucs thif thaays, lucs thif khoong thaays; vaanx phair guwri dduowcj\n",
         "test kh thấy chữ, lúc thì thấy, lúc thì không thấy; vẫn phải gửi được\n"},
        {"dduowcj dduowngf chuyeenj khoong trooi banf phims vaf tuwong thichs",
         "được đường chuyện không trôi bàn phím và tương thích"},
        {"hoatj ddoongj truwcj tieeps", "hoạt động trực tiếp"},
        {"tieeps cos looxi", "tiếp có lỗi"},
        {"xin chaof tooi ddang kieemr tra booj gox tieengs vieetj treen ubuntu",
         "xin chào tôi đang kiểm tra bộ gõ tiếng việt trên ubuntu"},
    };

    for (const auto &[raw, expected] : directCases) {
        auto actual = fcitx::UniikiEngine::simulateDirectForTest(raw);
        if (actual != expected) {
            std::cerr << "direct " << raw << " -> " << actual << ", expected " << expected << "\n";
            return 1;
        }
        const std::vector<std::string> forbidden = {
            "babạbạn", "lêlệnh", "mơimới", "đổđổi", "đđeđêđể",
            "đđađâđâuđầu", "đượđược", "đuođượcđược",
        };
        for (const auto &bad : forbidden) {
            if (actual.find(bad) != std::string::npos) {
                std::cerr << "direct " << raw << " produced duplicate fragment " << bad << "\n";
                return 1;
            }
        }
    }

    std::cout << "tone-matrix-valid=" << validMatrixCombinations
              << " tone-pairs=" << matrixTones.size() * matrixTones.size()
              << " stress-iterations=1000 duplicate-applies=0"
              << " status=pass\n";
    return 0;
}
