#include "uniiki_engine.h"

#include <iostream>
#include <string>
#include <tuple>
#include <utility>
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
        {"dd", "đ"},
        {"ddd", "dd"},
        {"ddu", "đu"},
        {"dduo", "đuo"},
        {"dduow", "đươ"},
        {"dduowngf", "đường"},
        {"dduowjc", "được"},
        {"dduowcj", "được"},
        {"w", "ư"},
        {"ww", "w"},
        {"uw", "ư"},
        {"uww", "uw"},
        {"ow", "ơ"},
        {"oww", "ow"},
        {"aw", "ă"},
        {"aww", "aw"},
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
        {"truowngf", "trường"},
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
    };

    for (const auto &[raw, expected] : cases) {
        auto actual = fcitx::UniikiEngine::evaluateTelexForTest(raw);
        if (actual != expected) {
            std::cerr << raw << " -> " << actual << ", expected " << expected << "\n";
            return 1;
        }
    }

    const std::vector<std::pair<std::string, std::string>> directCases = {
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
        {"ddaay laf huwowngs ddi chuaanr chuyeen nghieepj cuar casc heej thoongs booj gox linux",
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
        {"test", "tét"},
        {"tesst", "test"},
        {"tesst tesst tesst", "test test test"},
        {"w ww wW Ww W WW", "ư w w W Ư W"},
        {"w ww uw uww ow oww aw aww chuaw chuaww chuw muaw thuaw duaw",
         "ư w ư uw ơ ow ă aw chưa chuaw chư mưa thưa dưa"},
        {"chuaw\b", "chua"},
        {"chuaww", "chuaw"},
        {"tuowngf nguowif dduowjc nuowcs truowngf thuowngf",
         "tường người được nước trường thường"},
        {"dduocw dduocW dduocww dduocwW dduocwjw dduocwjW dduowcjw dduowcjW",
         "đuơc đuơc đuọcw đuọcw đuọcw đuọcw đuọcw đuọcw"},
        {"thaays vaanx dduowcj", "thấy vẫn được"},
        {"tesst kh thaays chuwx, lucs thif thaays, lucs thif khoong thaays; vaanx phair guwri dduowcj\n",
         "test kh thấy chữ, lúc thì thấy, lúc thì không thấy; vẫn phải gửi được\n"},
        {"dduowcj dduowngf chuyeenj khoong trooi banf phims vaf tuwowng thichs",
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

    return 0;
}
