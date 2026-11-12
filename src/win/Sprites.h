// assets 폴더의 PNG 이미지를 읽는다. 파일이 없으면 nullptr을 돌려주고 렌더러가 임시 도형을 그린다.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "WinCommon.h"

namespace dt::win {

class Sprites {
public:
    void load(const std::filesystem::path& assetDir);
    Gdiplus::Bitmap* get(const std::string& key) const;
    int loadedCount() const { return static_cast<int>(images_.size()); }

private:
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> images_;
};

}  // namespace dt::win
