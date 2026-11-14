// assets 폴더의 PNG를 읽는다. 파일이 없으면 nullptr을 돌려주고 렌더러가 임시 도형을 그린다.
// v0.2: assets/dragons/<키>/<상태>.png 스프라이트 시트를 함께 읽는다.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "WinCommon.h"
#include "core/Enemy.h"
#include "core/Species.h"

namespace dt::win {

// UI 스킨 조각. margin은 9조각으로 늘릴 때 "늘리지 않을 테두리 두께"다(0이면 늘리지 않는다).
struct UiPiece {
    Gdiplus::Bitmap* bmp = nullptr;
    int left = 0, top = 0, right = 0, bottom = 0;
    bool valid() const { return bmp != nullptr; }
    bool stretch() const { return left || top || right || bottom; }
};

// 가로 한 줄 스프라이트 시트. frames는 칸 수, frameW는 칸 너비(픽셀).
struct Sheet {
    Gdiplus::Bitmap* bmp = nullptr;
    int frames = 1;
    int frameW = 0;
    int frameH = 0;
};

class Sprites {
public:
    // assetDir: 실행 파일 옆 assets 폴더. 기존 정지 이미지와 dragons/ 시트를 모두 읽는다.
    void load(const std::filesystem::path& assetDir);

    Gdiplus::Bitmap* get(const std::string& key) const;

    // 종의 상태 시트를 찾는다. 없으면 대체 규칙(move/attack → idle)을 따른다.
    // 어떤 시트도 없으면 bmp == nullptr.
    Sheet sheet(const std::string& speciesKey, AnimState state) const;
    bool hasAnySheet(const std::string& speciesKey) const;

    // UI 스킨 조각을 찾는다. 스킨에 없으면 default 에서 찾고, 그래도 없으면 bmp == nullptr.
    // 그림이 없으면 렌더러가 예전처럼 도형으로 그린다.
    UiPiece ui(const std::string& skinKey, const std::string& name) const;

    int loadedCount() const { return static_cast<int>(images_.size()); }
    int sheetCount() const { return static_cast<int>(sheets_.size()); }
    const std::vector<std::string>& warnings() const { return warnings_; }

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

    struct UiData {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        int left = 0, top = 0, right = 0, bottom = 0;
    };
    void loadUiSkins(const std::filesystem::path& uiDir);

    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> images_;
    std::map<std::string, UiData> ui_;  // "<스킨>/<조각>"
    std::map<std::string, SheetData> sheets_;  // "<키>/<상태>"
    std::vector<std::string> warnings_;
};

}  // namespace dt::win
