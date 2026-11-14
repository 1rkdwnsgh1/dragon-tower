// 에셋 폴더가 제대로 읽히는지 확인한다. 경고가 하나라도 있으면 실패한다.
//
//   asset_check <assets 폴더>
//
// 게임을 켜지 않고도 dragon.txt / enemy.txt 의 오타와 범위 벗어난 값을 잡는다.
// 시트 그림은 Windows 쪽에서 읽으므로 여기서는 확인하지 않는다(데이터만 본다).
#include <cstdio>
#include <filesystem>
#include <string>

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
        std::printf("  - %s (%s)\n", s.key.c_str(), s.name.c_str());
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
