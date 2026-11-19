// assets 폴더의 PNG를 읽는다. 파일이 없으면 nullptr을 돌려주고 렌더러가 임시 도형을 그린다.
// v0.2: assets/dragons/<키>/<상태>.png 스프라이트 시트를 함께 읽는다.
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "WinCommon.h"
#include "core/Enemy.h"
#include "core/Species.h"

namespace dt::win {

// 조각을 목적지에 맞추는 방법. 그림 종류마다 다르다.
//  Nine   — 9조각. 네 모서리를 지키고 가장자리·가운데만 늘린다. 큰 창틀용.
//  Height — 높이를 목적지에 맞춰 통째로 줄이고 가로만 늘린다. 세로로 눌리지 않는다.
//           버튼·막대처럼 "낮고 가로로 긴" 것에 쓴다.
//  Contain— 비율을 지킨 채 목적지 안에 넣는다. 토글·체크처럼 모양이 정해진 것에 쓴다.
enum class UiFit { Nine, Height, Contain };

// UI 스킨 조각. margin은 "늘리지 않을 테두리 두께"(원본 px)다.
struct UiPiece {
    Gdiplus::Bitmap* bmp = nullptr;
    int left = 0, top = 0, right = 0, bottom = 0;
    UiFit fit = UiFit::Nine;
    bool valid() const { return bmp != nullptr; }
    bool stretch() const { return left || top || right || bottom; }
};

// Grid atlas, read left to right and then top to bottom.
struct Sheet {
    Gdiplus::Bitmap* bmp = nullptr;
    int frames = 1;
    int frameW = 0;
    int frameH = 0;
    int columns = 0;  // Zero means a single row.
};

namespace bonestorm {
inline constexpr float anchorX = 64.f;
inline constexpr float coreTipX = 965.f;
inline constexpr float reachScale = 1.15f;
}

// 레이어형 숨결의 원본 층. 읽을 때만 쓰고, 아래 16장으로 합친 뒤 버린다.
struct BreathLayers {
    std::unique_ptr<Gdiplus::Bitmap> start, core, end, ribbons, outer, endRibbons;
    std::unique_ptr<Gdiplus::Bitmap> startRibbons, startOuter;
    std::array<std::unique_ptr<Gdiplus::Bitmap>, 4> endRibbonFrames, endOuter;
};

// 숨결 그림 key(0~3 시작, 4~11 반복, 12~15 끝)마다 층을 원본 크기(1024x512)로 미리 합친 것.
// 그리는 쪽은 한 장만 확대·회전하면 된다(예전에는 매 프레임 세 장).
struct BreathSheets {
    static constexpr int kFrames = 16;
    static constexpr int kWidth = 1024, kHeight = 512;
    std::array<std::unique_ptr<Gdiplus::Bitmap>, kFrames> frames;
};

class Sprites {
public:
    // assetDir: 실행 파일 옆 assets 폴더. 기존 정지 이미지와 dragons/ 시트를 모두 읽는다.
    void load(const std::filesystem::path& assetDir);

    // 제작 도구에서는 PNG를 열어 둔 채로 덮어쓸 수 있게 메모리로 분리한다.
    void loadPreviewDragon(const std::filesystem::path& dir, const SpeciesDef& def);

    void loadPreviewEnemy(const std::filesystem::path& dir, const EnemyDef& def);

    Gdiplus::Bitmap* get(const std::string& key) const;

    // 종의 상태 시트를 찾는다. 없으면 대체 규칙(move/attack → idle)을 따른다.
    // 어떤 시트도 없으면 bmp == nullptr.
    Sheet sheet(const std::string& speciesKey, AnimState state, AnimState* resolved = nullptr) const;
    bool hasAnySheet(const std::string& speciesKey) const;

    // v1.4 스킬 전용 본체 모션 시트(assets/dragons/<키>/skill_<motion>.png).
    // 없으면 bmp == nullptr 이고, 그리는 쪽이 공용 attack 시트로 대신한다.
    Sheet skillSheet(const std::string& speciesKey, const std::string& motion) const;

    // v1.6 스킬 효과 시트(assets/dragons/<키>/fx_<fx>.png).
    // 없으면 bmp == nullptr 이고, 그리는 쪽이 예전처럼 도형(FxType)으로 대신 그린다.
    Sheet fxSheet(const std::string& speciesKey, const std::string& fxKey) const;
    const BreathSheets* breathSheets(const std::string& speciesKey, const std::string& package) const;
    Sheet breathBody(const SpeciesDef& species, const SkillDef& skill) const;

    // UI 스킨 조각을 찾는다. 스킨에 없으면 default 에서 찾고, 그래도 없으면 bmp == nullptr.
    // 그림이 없으면 렌더러가 예전처럼 도형으로 그린다.
    UiPiece ui(const std::string& skinKey, const std::string& name) const;

    int loadedCount() const { return static_cast<int>(images_.size()); }
    int sheetCount() const { return static_cast<int>(sheets_.size()); }
    const std::vector<std::string>& warnings() const { return warnings_; }
    // 그림을 다시 읽을 때마다 1씩 는다. 그린 결과를 캐시하는 쪽이 낡았는지 확인하는 데 쓴다.
    unsigned generation() const { return generation_; }

private:
    struct SheetData {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        int frames = 1;
        int frameW = 0;
        int frameH = 0;
    };
    // prefix는 시트 이름 앞에 붙는 구분자다(적은 "enemy:<키>").
    void loadSheetsFor(const std::filesystem::path& dir, const AnimDef* anims, const std::string& prefix,
                       const std::string& label);
    // 한 장을 읽어 칸으로 나눈다. 읽었으면 true. 시트 이름은 prefix + "/" + name 이다.
    bool loadSheetFile(const std::filesystem::path& file, const AnimDef& anim, const std::string& key,
                       const std::string& label);
    // v1.4 그 종의 스킬이 쓰는 skill_*.png 를 모두 읽는다.
    void loadSkillSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label);
    // v1.6 그 종의 스킬이 fx= 로 적어 둔 fx_*.png 를 모두 읽는다.
    void loadFxSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label);
    void loadBreathSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label);

    struct UiData {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        int left = 0, top = 0, right = 0, bottom = 0;
        UiFit fit = UiFit::Nine;
        bool hasCfg = false;  // 이 스킨의 ui.txt 에 설정이 적혀 있었는가
    };
    void loadUiSkins(const std::filesystem::path& uiDir);

    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> images_;
    std::map<std::string, UiData> ui_;  // "<스킨>/<조각>"
    std::map<std::string, SheetData> sheets_;  // "<키>/<상태>"
    std::map<std::string, BreathSheets> breathSheets_;
    std::vector<std::string> warnings_;
    bool detachFiles_ = false;
    unsigned generation_ = 0;
};

}  // namespace dt::win
