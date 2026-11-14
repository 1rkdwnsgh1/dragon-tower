// 에셋 폴더가 제대로 읽히는지 확인한다. 경고가 하나라도 있으면 실패한다.
//
//   asset_check <assets 폴더>
//
// 게임을 켜지 않고도 dragon.txt / enemy.txt 의 오타와 범위 벗어난 값을 잡는다.
// 시트 그림은 Windows 쪽에서 읽으므로 여기서는 확인하지 않는다(데이터만 본다).
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/DragonData.h"
#include "core/Enemy.h"
#include "core/EnemyData.h"
#include "core/Species.h"

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
        std::printf("  - %s (%s) 스킬 %d개\n", s.key.c_str(), s.name.c_str(),
                    static_cast<int>(s.skills.size()));
        // v1.4: 해제 레벨이 "레벨마다 하나씩" 으로 제대로 배정됐는지 눈으로 확인할 수 있게 적는다.
        // 같은 레벨에 둘이 겹치면(level= 을 직접 적다가 실수한 경우) 경고한다.
        std::vector<int> used;
        for (const dt::SkillDef& sk : s.skills) {
            bool passive = dt::skillIsPassive(sk.kind);
            std::string motion;
            if (!sk.motion.empty()) {
                std::filesystem::path f = root / "dragons" / s.key / ("skill_" + sk.motion + ".png");
                std::error_code ec;
                motion = "  모션 skill_" + sk.motion + ".png";
                if (!std::filesystem::exists(f, ec)) motion += " (없음 -> attack 으로 대신 그려짐)";
            }
            std::printf("      Lv%-2d %-14s %-14s%s%s\n", sk.unlockLevel, sk.name.c_str(),
                        dt::skillKindName(sk.kind), passive ? "  [패시브]" : "", motion.c_str());
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
        // v1.6: 그림을 빌려 쓰는 변종은 어디서 빌리는지 함께 적는다. 그림이 없는 적을
        // 만들어 두고 왜 도형으로 나오는지 찾아다니지 않게 하려는 것이다.
        if (s.artFrom.empty())
            std::printf("  - %s (%s) %s\n", s.key.c_str(), s.name.c_str(),
                        s.role == dt::EnemyRole::Boss ? "[보스]" : "");
        else
            std::printf("  - %s (%s) %s 그림은 %s 재활용\n", s.key.c_str(), s.name.c_str(),
                        s.role == dt::EnemyRole::Boss ? "[보스]" : "", s.artFrom.c_str());
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
