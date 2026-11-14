// 에셋 폴더가 제대로 읽히는지 확인한다. 경고가 하나라도 있으면 실패한다.
//
//   asset_check <assets 폴더>
//
// 게임을 켜지 않고도 dragon.txt / enemy.txt 의 오타와 범위 벗어난 값을 잡는다.
// 시트 그림을 읽어 보는 것은 Windows 쪽 일이지만(여기서는 PNG 를 해독하지 않는다),
// **파일이 있는지 없는지**는 Linux 에서도 알 수 있다. v1.6(큐 7번)부터 스킬마다
// 아이콘·본체 모션·효과 시트의 유무를 한 표로 적고, 없으면 무엇으로 대체되는지 적는다.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "core/DragonData.h"
#include "core/Enemy.h"
#include "core/EnemyData.h"
#include "core/Species.h"

namespace {

// 한글은 UTF-8 에서 한 글자가 3바이트라 printf 의 "%-14s" 가 바이트로 세어 표가 어긋난다.
// 표로 보여야 하는 항목이므로 화면에서 차지하는 칸 수로 직접 채운다.
// (CJK 글자는 두 칸으로 센다 — 터미널이 보통 그렇게 그린다.)
int displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        unsigned int cp = c;
        if (c >= 0xF0) {
            len = 4;
            cp = c & 0x07u;
        } else if (c >= 0xE0) {
            len = 3;
            cp = c & 0x0Fu;
        } else if (c >= 0xC0) {
            len = 2;
            cp = c & 0x1Fu;
        }
        for (size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        bool wide = (cp >= 0x1100u && cp <= 0x115Fu) || (cp >= 0x2E80u && cp <= 0xA4CFu) ||
                    (cp >= 0xAC00u && cp <= 0xD7A3u) || (cp >= 0xF900u && cp <= 0xFAFFu) ||
                    (cp >= 0xFF00u && cp <= 0xFF60u) || (cp >= 0xFFE0u && cp <= 0xFFE6u);
        w += wide ? 2 : 1;
        i += len;
    }
    return w;
}

std::string pad(const std::string& s, int cols) {
    std::string out = s;
    for (int i = displayWidth(s); i < cols; ++i) out += ' ';
    return out;
}

bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("사용법: asset_check <assets 폴더>\n");
        return 2;
    }
    std::filesystem::path root(argv[1]);
    int warned = 0;

    dt::DragonScanResult d = dt::loadDragonFolders(root / "dragons");
    std::printf("드래곤 폴더 %d개, 새로 추가된 종 %d개 (등록된 종 %d개)\n", d.folderCount, d.newSpecies,
                dt::speciesCount());
    for (int i = 0; i < dt::speciesCount(); ++i) {
        const dt::SpeciesDef& s = dt::speciesDef(static_cast<dt::Species>(i));
        std::filesystem::path dir = root / "dragons" / s.key;
        std::printf("  - %s (%s) 스킬 %d개\n", s.key.c_str(), s.name.c_str(),
                    static_cast<int>(s.skills.size()));
        std::printf("      %s%s%s%s%s%s\n", pad("레벨", 6).c_str(), pad("스킬", 16).c_str(),
                    pad("종류", 14).c_str(), pad("아이콘", 30).c_str(), pad("본체 모션", 36).c_str(),
                    "효과 시트");

        // 이 종이 실제로 가리키는 파일. 폴더에 있는데 아무 스킬도 가리키지 않는 파일을 뒤에서 가린다.
        std::set<std::string> referenced;
        std::vector<int> used;
        for (size_t n = 0; n < s.skills.size(); ++n) {
            const dt::SkillDef& sk = s.skills[n];
            bool passive = dt::skillIsPassive(sk.kind);

            // 아이콘: 스킬 순서대로 icon_1.png ~ icon_N.png (src/win/Sprites.cpp 와 같은 규칙).
            std::string iconFile = "icon_" + std::to_string(n + 1) + ".png";
            referenced.insert(iconFile);
            std::string icon = iconFile;
            if (!fileExists(dir / iconFile)) icon = "없음 -> 글자로 그림";

            // 본체 모션: motion= 을 적었을 때만 skill_<이름>.png 를 쓴다.
            std::string motion;
            if (sk.motion.empty()) {
                motion = "(motion= 없음 -> attack 시트)";
            } else {
                std::string f = "skill_" + sk.motion + ".png";
                referenced.insert(f);
                motion = f;
                if (!fileExists(dir / f)) motion += " 없음 -> attack 시트";
            }

            // 효과 시트: dragon.txt 의 fx= 가 가리키는 fx_<키>.png (큐 8번이 그린다).
            // main 의 SkillDef 에는 아직 fx 칸이 없다 — PR #25(큐 2번)가 병합되면 여기서 함께 센다.
            std::string fx = "(fx= 미지원)";

            std::printf("      %s%s%s%s%s%s%s\n", pad("Lv" + std::to_string(sk.unlockLevel), 6).c_str(),
                        pad(sk.name, 16).c_str(), pad(dt::skillKindName(sk.kind), 14).c_str(),
                        pad(icon, 30).c_str(), pad(motion, 36).c_str(), fx.c_str(),
                        passive ? "  [패시브]" : "");
            // v1.4: 해제 레벨이 "레벨마다 하나씩" 으로 제대로 배정됐는지 본다.
            // 같은 레벨에 둘이 겹치면(level= 을 직접 적다가 실수한 경우) 경고한다.
            for (int u : used) {
                if (u == sk.unlockLevel) {
                    std::printf("  [경고] %s: Lv%d 에 스킬이 둘 이상 해제됩니다 (%s)\n", s.key.c_str(),
                                sk.unlockLevel, sk.name.c_str());
                    ++warned;
                    break;
                }
            }
            used.push_back(sk.unlockLevel);
        }

        // 폴더에 있는데 아무 스킬도 가리키지 않는 파일. 지우라는 뜻이 아니라 "지금은 화면에
        // 나오지 않는다"는 뜻이다 — 아이콘을 미리 그려 두었거나 스킬이 지워진 자리다.
        // 경고로 세지 않는다(게임이 깨지지 않는다). 사람이 보고 판단할 줄이다.
        std::vector<std::string> idle;
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) {
            std::vector<std::string> names;
            for (const auto& ent : std::filesystem::directory_iterator(dir, ec))
                names.push_back(ent.path().filename().string());
            std::sort(names.begin(), names.end());
            for (const std::string& f : names) {
                bool candidate = f.rfind("icon_", 0) == 0 || f.rfind("skill_", 0) == 0 ||
                                 f.rfind("fx_", 0) == 0;
                if (!candidate) continue;
                if (f.size() < 4 || f.substr(f.size() - 4) != ".png") continue;
                if (referenced.count(f) == 0) idle.push_back(f);
            }
        }
        for (const std::string& f : idle) {
            const char* why = f.rfind("fx_", 0) == 0 ? "fx= 가 아직 없어 쓰이지 않음"
                                                     : "가리키는 스킬이 없음";
            std::printf("      [알림] %s — %s\n", f.c_str(), why);
        }
    }
    for (const std::string& w : d.warnings) {
        std::printf("  [경고] %s\n", w.c_str());
        ++warned;
    }

    dt::EnemyScanResult e = dt::loadEnemyFolders(root / "enemies");
    std::printf("적 폴더 %d개, 새로 추가된 적 %d개 (등록된 적 %d개)\n", e.folderCount, e.newEnemies,
                dt::enemyCount());
    for (int i = 0; i < dt::enemyCount(); ++i) {
        const dt::EnemyDef& s = dt::enemyDef(static_cast<dt::EnemyKind>(i));
        std::printf("  - %s (%s)\n", s.key.c_str(), s.name.c_str());
    }
    for (const std::string& w : e.warnings) {
        std::printf("  [경고] %s\n", w.c_str());
        ++warned;
    }

    if (warned > 0) {
        std::printf("\n경고 %d건. 고치고 다시 확인하세요.\n", warned);
        return 1;
    }
    std::printf("\n경고 없음.\n");
    return 0;
}
