#include <cstdio>
#include "Sprites.h"
#include "HeatBladeAtlases.h"

#include "core/World.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace dt::win {

namespace {
// assets/README.md의 파일 목록과 같아야 한다(기존 정지 이미지).
const char* kSpriteKeys[] = {
    "dragon_fire", "dragon_earth", "dragon_wind", "dragon_water",
    "enemy_imp",   "boss_guardian", "button_settings",
};

std::unique_ptr<Gdiplus::Bitmap> loadPng(const std::filesystem::path& p, bool detach = false) {
    using namespace Gdiplus;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromFile(p.wstring().c_str(), FALSE));
    if (!bmp || bmp->GetLastStatus() != Ok || bmp->GetWidth() == 0 || bmp->GetHeight() == 0) return nullptr;
    if (!detach) return bmp;

    auto copy = std::make_unique<Bitmap>(bmp->GetWidth(), bmp->GetHeight(), PixelFormat32bppPARGB);
    Graphics graphics(copy.get());
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    const Rect rect(0, 0, static_cast<INT>(bmp->GetWidth()), static_cast<INT>(bmp->GetHeight()));
    if (graphics.DrawImage(bmp.get(), rect, 0, 0, rect.Width, rect.Height, UnitPixel) != Ok) return nullptr;
    return copy;
}

template <typename Opacity>
bool maskBreathAtlas(Gdiplus::Bitmap& atlas, Opacity opacity) {
    using namespace Gdiplus;
    constexpr int frameW = 1024;
    const Rect area(0, 0, static_cast<int>(atlas.GetWidth()), static_cast<int>(atlas.GetHeight()));
    BitmapData pixels{};
    if (atlas.LockBits(&area, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &pixels) != Ok)
        return false;
    for (int y = 0; y < area.Height; ++y) {
        auto* row = static_cast<BYTE*>(pixels.Scan0) + y * pixels.Stride;
        for (int x = 0; x < area.Width; ++x) {
            BYTE& alpha = row[x * 4 + 3];
            alpha = static_cast<BYTE>(alpha * opacity(x / frameW, x % frameW));
        }
    }
    return atlas.UnlockBits(&pixels) == Ok;
}

float smoothRamp(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

std::unique_ptr<Gdiplus::Bitmap> makeEndRibbons(Gdiplus::Bitmap& ribbons) {
    using namespace Gdiplus;
    auto end = std::unique_ptr<Bitmap>(ribbons.Clone(Rect(0, 0, 4096, 512), PixelFormat32bppARGB));
    if (!end || end->GetLastStatus() != Ok || !maskBreathAtlas(*end, [](int frame, int x) {
            constexpr int maskStart[] = {24, 165, 390, 650};
            return smoothRamp((x - maskStart[frame]) / 72.f);
        })) return nullptr;
    return end;
}

std::unique_ptr<Gdiplus::Bitmap> makeUmbralStart(Gdiplus::Bitmap& loop) {
    using namespace Gdiplus;
    constexpr int width = 1024, height = 512;
    auto start = std::make_unique<Bitmap>(4 * width, height, PixelFormat32bppARGB);
    {
        Graphics canvas(start.get());
        canvas.SetCompositingMode(CompositingModeSourceCopy);
        canvas.SetInterpolationMode(InterpolationModeNearestNeighbor);
        for (int i = 0; i < 4; ++i) {
            const int phase = (i + 5) % 8;
            if (canvas.DrawImage(&loop, Rect(i * width, 0, width, height),
                                 (phase % 4) * width, (phase / 4) * height, width, height, UnitPixel) != Ok)
                return nullptr;
        }
    }
    if (!maskBreathAtlas(*start, [](int frame, int x) {
            constexpr int edge[] = {105, 350, 690, 1000};
            return 1.f - smoothRamp((x - (edge[frame] - 42)) / 42.f);
        })) return nullptr;
    return start;
}
// 숨결 그림 key 하나의 층들을 원본 크기 그대로(1:1) 겹친다. 투명도는 예전 drawBreath 와 같다.
// 1:1 정수 좌표라 GDI+ 가 다시 샘플링하지 않는다 — 결과는 층을 각각 그려 겹친 것과 같다.
std::unique_ptr<Gdiplus::Bitmap> composeBreathFrame(const BreathLayers& pack, bool umbral, int key) {
    using namespace Gdiplus;
    constexpr int w = BreathSheets::kWidth, h = BreathSheets::kHeight;
    auto out = std::make_unique<Bitmap>(w, h, PixelFormat32bppPARGB);
    if (out->GetLastStatus() != Ok) return nullptr;
    Graphics g(out.get());
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    bool ok = true;
    const auto layer = [&](Bitmap* bitmap, int frames, int index, float alpha = 1.f) {
        if (!bitmap || alpha <= 0.f) return;
        const int columns = frames == 1 ? 1 : 4;
        const Rect dst(0, 0, w, h);
        const int sx = w * (index % columns), sy = h * (index / columns);
        Status st;
        if (alpha >= 1.f) {
            st = g.DrawImage(bitmap, dst, sx, sy, w, h, UnitPixel);
        } else {
            ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                               {0.f, 1.f, 0.f, 0.f, 0.f},
                               {0.f, 0.f, 1.f, 0.f, 0.f},
                               {0.f, 0.f, 0.f, alpha, 0.f},
                               {0.f, 0.f, 0.f, 0.f, 1.f}}};
            ImageAttributes ia;
            ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            st = g.DrawImage(bitmap, dst, sx, sy, w, h, UnitPixel, &ia);
        }
        ok = ok && st == Ok;
    };
    if (key < 4) {
        constexpr float bonestormAlpha[] = {0.f, 0.12f, 0.45f, 0.96f};
        constexpr float umbralAlpha[] = {0.f, 0.08f, 0.43f, 0.96f};
        layer(pack.start.get(), 4, key);
        if (umbral) {
            layer(pack.startRibbons.get(), 4, key, umbralAlpha[key]);
            layer(pack.startOuter.get(), 4, key, umbralAlpha[key]);
        } else {
            layer(pack.ribbons.get(), 8, (key + 5) % 8, bonestormAlpha[key]);
            layer(pack.outer.get(), 8, (key + 5) % 8, bonestormAlpha[key]);
        }
    } else if (key < 12) {
        const int phase = key - 4;
        layer(pack.core.get(), 8, phase);
        layer(pack.ribbons.get(), 8, phase);
        layer(pack.outer.get(), 8, phase);
    } else {
        constexpr float alpha[] = {0.85f, 0.50f, 0.20f, 0.025f};
        const int index = key - 12;
        layer(pack.end.get(), 4, index);
        if (umbral) layer(pack.endRibbonFrames[index].get(), 1, 0);
        else layer(pack.endRibbons.get(), 4, index, alpha[index]);
        layer(pack.endOuter[index].get(), 1, 0);
    }
    return ok ? std::move(out) : nullptr;
}

}  // namespace

void Sprites::loadPreviewDragon(const std::filesystem::path& dir, const SpeciesDef& def,
                                const EnemyDef* target) {
    images_.clear();
    sheets_.clear();
    breathSheets_.clear();
    ++generation_;
    warnings_.clear();
    detachFiles_ = true;
    if (auto bmp = loadPng(dir / "dragon.png", true)) images_["dragon_" + def.key] = std::move(bmp);
    loadSheetsFor(dir, def.anims, def.key, def.key);
    loadSheetsFor(dir / "awakened", def.anims, def.key + ":awakened", def.key + "/awakened");
    loadSkillSheets(dir, def, def.key);
    loadFxSheets(dir, def, def.key);
    loadBreathSheets(dir, def, def.key);
    if (target) {
        const auto targetDir = dir.parent_path().parent_path() / "enemies" / target->key;
        if (std::filesystem::exists(targetDir))
            loadSheetsFor(targetDir, target->anims, "enemy:" + target->key, target->key);
    }
    detachFiles_ = false;
}

void Sprites::loadPreviewEnemy(const std::filesystem::path& dir, const EnemyDef& def) {
    images_.clear();
    sheets_.clear();
    breathSheets_.clear();
    ++generation_;
    warnings_.clear();
    detachFiles_ = true;
    loadSheetsFor(dir, def.anims, "enemy:" + def.key, def.key);
    detachFiles_ = false;
}

void Sprites::load(const std::filesystem::path& assetDir) {
    assetDir_ = assetDir;
    images_.clear();
    sheets_.clear();
    breathSheets_.clear();
    ++generation_;
    warnings_.clear();

    for (const char* key : kSpriteKeys) {
        if (auto bmp = loadPng(assetDir / (std::string(key) + ".png"))) images_[key] = std::move(bmp);
    }
    // 스킬 아이콘은 종이 늘어날 수 있으므로 등록된 종을 돌며 읽는다.
    for (int i = 0; i < speciesCount(); ++i) {
        const SpeciesDef& def = speciesDef(static_cast<Species>(i));
        for (int n = 1; n <= 3; ++n) {
            std::string key = "skill_" + def.key + "_" + std::to_string(n);
            if (auto bmp = loadPng(assetDir / (key + ".png"))) images_[key] = std::move(bmp);
        }
        // 종 폴더: 정지 이미지(dragon.png)와 상태별 시트
        std::filesystem::path dir = assetDir / "dragons" / def.key;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        if (auto bmp = loadPng(dir / "dragon.png")) images_["dragon_" + def.key] = std::move(bmp);
        // v1.5 스킬 아이콘: 종 폴더의 icon_1.png ~ icon_N.png. 없으면 글자로 그린다.
        for (size_t n = 0; n < def.skills.size(); ++n) {
            std::string file = "icon_" + std::to_string(n + 1) + ".png";
            if (auto bmp = loadPng(dir / file))
                images_["skillicon_" + def.key + "_" + std::to_string(n + 1)] = std::move(bmp);
        }
        loadSheetsFor(dir, def.anims, def.key, "dragons/" + def.key);
        loadSheetsFor(dir / "awakened", def.anims, def.key + ":awakened", def.key + "/awakened");
        loadSkillSheets(dir, def, "dragons/" + def.key);  // v1.4 스킬 전용 모션
        loadFxSheets(dir, def, "dragons/" + def.key);     // v1.6 스킬 효과
        loadBreathSheets(dir, def, "dragons/" + def.key);
    }

    // UI 스킨 폴더 (v0.9)
    loadUiSkins(assetDir / "ui");

    // 적 폴더 (v0.4)
    for (int i = 0; i < enemyCount(); ++i) {
        const EnemyDef& def = enemyDef(static_cast<EnemyKind>(i));
        // v1.6: art_from= 을 적은 변종은 다른 적 폴더의 시트를 그대로 읽는다(그림 복사 없음).
        // 읽어 넣는 자리는 자기 키("enemy:<키>")이고, [anim.*] 설정은 자기 enemy.txt 의 것을 쓴다.
        const std::string artKey = def.artFrom.empty() ? def.key : def.artFrom;
        std::filesystem::path dir = assetDir / "enemies" / artKey;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        loadSheetsFor(dir, def.anims, "enemy:" + def.key, "enemies/" + artKey);
        // T8(2026-11-25): shot 공격의 투사체 그림(있을 때만). 오른쪽으로 날아가는 가로 한 줄, 정사각 칸.
        if (def.attack.kind == EnemyAttackKind::Shot) {
            const AnimDef square{};
            // 변종(art_from)이라도 자기 폴더에 projectile.png 가 있으면 그것을 쓴다(정예 리치).
            const std::filesystem::path own = assetDir / "enemies" / def.key / "projectile.png";
            const bool hasOwn = std::filesystem::exists(own, ec);
            loadSheetFile(hasOwn ? own : dir / "projectile.png", square, "enemy:" + def.key + "/fx_projectile",
                          "enemies/" + (hasOwn ? def.key : artKey) + "/projectile.png");
        }
        // 2026-11-25 분열 연출(있을 때만): 본체 동작 split.png, 효과 fx_split.png·fx_split_land.png
        if (!def.splitKey.empty()) {
            const AnimDef square{};
            const std::string k = "enemy:" + def.key;
            loadSheetFile(dir / "split.png", square, k + "/skill_split", "enemies/" + artKey + "/split.png");
            loadSheetFile(dir / "fx_split.png", square, k + "/fx_split", "enemies/" + artKey + "/fx_split.png");
            loadSheetFile(dir / "fx_split_land.png", square, k + "/fx_split_land", "enemies/" + artKey + "/fx_split_land.png");
        }
    }
}

void Sprites::loadBreathSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label) {
    for (const SkillDef& skill : def.skills) {
        if (skill.layeredBreath.empty()) continue;
        const std::string key = def.key + "/" + skill.layeredBreath;
        if (breathSheets_.count(key)) continue;
        const auto folder = dir / skill.layeredBreath;
        const bool umbral = skill.usesUmbralBreath();
        const auto read = [&](const char* name, unsigned columns, unsigned rows) -> std::unique_ptr<Gdiplus::Bitmap> {
            auto bmp = loadPng(folder / name, detachFiles_ || !umbral);
            if (bmp && bmp->GetWidth() == columns * 1024 && bmp->GetHeight() == rows * 512) {
                if (umbral || maskBreathAtlas(*bmp, [](int, int x) {
                        // Soften the far end, leaving only faint residual light beyond the core.
                        constexpr float oldTip = bonestorm::anchorX +
                            (bonestorm::coreTipX - bonestorm::anchorX) / bonestorm::reachScale;
                        return 1.f - 0.85f * smoothRamp((x - oldTip + 56.f) / 56.f);
                    })) return bmp;
            }
            warnings_.push_back(label + "/" + skill.layeredBreath + "/" + name +
                                ": missing or invalid breath atlas; using fallback");
            return nullptr;
        };
        BreathLayers bundle;
        bundle.start = read(umbral ? "umbral_vortex_L1_start.png" : "bonestorm_L1_start.png", 4, 1);
        bundle.core = read(umbral ? "umbral_vortex_L1_loop.png" : "bonestorm_L1_loop.png", 4, 2);
        bundle.end = read(umbral ? "umbral_vortex_L1_end.png" : "bonestorm_L1_end.png", 4, 1);
        bundle.ribbons = read(umbral ? "umbral_vortex_L2_helix_loop.png" : "bonestorm_L2_spiral_loop.png", 4, 2);
        bundle.outer = read(umbral ? "umbral_vortex_L3_shadow_currents_loop.png" : "bonestorm_L3_bones_loop.png", 4, 2);
        bool complete = bundle.start && bundle.core && bundle.end && bundle.ribbons && bundle.outer;
        for (size_t i = 0; i < bundle.endOuter.size(); ++i) {
            const std::string suffix = std::to_string(i + 1) + ".png";
            const std::string outer = (umbral ? "L3_shadow_currents_end_0" : "L3_end_tail_0") + suffix;
            bundle.endOuter[i] = read(outer.c_str(), 1, 1);
            complete = complete && bundle.endOuter[i] != nullptr;
            if (umbral) {
                bundle.endRibbonFrames[i] = read(("L2_end_tail_0" + suffix).c_str(), 1, 1);
                complete = complete && bundle.endRibbonFrames[i] != nullptr;
            }
        }
        if (!complete) continue;
        if (umbral) {
            bundle.startRibbons = makeUmbralStart(*bundle.ribbons);
            bundle.startOuter = makeUmbralStart(*bundle.outer);
            complete = bundle.startRibbons && bundle.startOuter;
        } else {
            bundle.endRibbons = makeEndRibbons(*bundle.ribbons);
            complete = bundle.endRibbons != nullptr;
        }
        if (!complete) {
            warnings_.push_back(label + "/" + skill.layeredBreath + ": could not prepare breath masks; using fallback");
            continue;
        }
        if (umbral) {
            const Sheet motion = skillSheet(def.key, skill.motion);
            if (!motion.bmp || motion.frames != 12 || motion.frameW != 256 || motion.frameH != 256) {
                warnings_.push_back(label + ": invalid Umbral breath body motion; using fallback");
                continue;
            }
        }
        // 층을 16장으로 합치고 원본 층은 여기서 버린다(그리는 쪽은 합친 것만 쓴다).
        BreathSheets sheets;
        for (int k = 0; k < BreathSheets::kFrames && complete; ++k) {
            sheets.frames[static_cast<size_t>(k)] = composeBreathFrame(bundle, umbral, k);
            complete = sheets.frames[static_cast<size_t>(k)] != nullptr;
        }
        if (!complete) {
            warnings_.push_back(label + "/" + skill.layeredBreath + ": could not compose breath frames; using fallback");
            continue;
        }
        breathSheets_.emplace(key, std::move(sheets));
    }
}

Sheet Sprites::breathBody(const SpeciesDef& species, const SkillDef& skill) const {
    if (!breathSheets(species.key, skill.layeredBreath)) return {};
    if (skill.usesUmbralBreath()) return skillSheet(species.key, skill.motion);
    const Sheet attack = sheet(species.key, AnimState::Attack);
    if (attack.frames < 4) return {};
    return attack;
}

const BreathSheets* Sprites::breathSheets(const std::string& speciesKey, const std::string& package) const {
    const auto it = breathSheets_.find(speciesKey + "/" + package);
    return it == breathSheets_.end() ? nullptr : &it->second;
}

bool Sprites::loadSheetFile(const std::filesystem::path& file, const AnimDef& anim, const std::string& key,
                            const std::string& label) {
    auto bmp = loadPng(file, detachFiles_);
    if (!bmp) {
        std::error_code ec;
        if (detachFiles_ && std::filesystem::exists(file, ec))
            warnings_.push_back(label + ": PNG를 읽을 수 없습니다");
        return false;
    }

    int w = static_cast<int>(bmp->GetWidth());
    int h = static_cast<int>(bmp->GetHeight());
    int frameW = anim.frameW;
    if (frameW <= 0) frameW = h;  // frame_w가 없으면 정사각형 칸으로 본다
    if (frameW > w) {
        if (detachFiles_) warnings_.push_back(label + ": 설정한 칸 너비보다 PNG가 좁아 한 칸으로 읽습니다");
        frameW = w;
    }
    if (frameW <= 0 || w % frameW != 0) {
        warnings_.push_back(label + ": 너비 " + std::to_string(w) + "가 칸 너비 " + std::to_string(frameW) +
                            "로 나누어떨어지지 않습니다");
        return false;
    }
    SheetData s;
    s.frames = w / frameW;
    s.frameW = frameW;
    s.frameH = h;
    s.bmp = std::move(bmp);
    sheets_[key] = std::move(s);
    return true;
}

void Sprites::loadSheetsFor(const std::filesystem::path& dir, const AnimDef* anims, const std::string& prefix,
                            const std::string& label) {
    for (int i = 0; i < kAnimStateCount; ++i) {
        AnimState st = static_cast<AnimState>(i);
        std::string name = animStateName(st);
        const std::string key = prefix + "/" + name;
        if (!loadSheetFile(dir / (name + ".png"), anims[i], key, label + "/" + name + ".png") ||
            prefix.compare(0, 6, "enemy:") != 0) continue;
        auto& sheet = sheets_.at(key);
        const int width = sheet.frames * sheet.frameW;
        Gdiplus::Rect rect(0, 0, width, sheet.frameH);
        Gdiplus::BitmapData pixels{};
        std::vector<float> heights(sheet.frames, 1.f);
        if (sheet.bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &pixels) != Gdiplus::Ok)
            continue;
        for (int frame = 0; frame < sheet.frames; ++frame) {
            const auto hasInk = [&](int y) {
                const auto* row = static_cast<const BYTE*>(pixels.Scan0) + y * pixels.Stride;
                for (int x = frame * sheet.frameW; x < (frame + 1) * sheet.frameW; ++x)
                    if (row[4 * x + 3] >= 16) return true;
                return false;
            };
            int top = 0, bottom = sheet.frameH - 1;
            while (top <= bottom && !hasInk(top)) ++top;
            while (bottom > top && !hasInk(bottom)) --bottom;
            if (top <= bottom) heights[frame] = static_cast<float>(bottom - top + 1) / sheet.frameH;
        }
        sheet.bmp->UnlockBits(&pixels);
        sheet.visibleHeight = std::move(heights);
    }
}

// v1.4: 스킬마다 motion= 으로 적어 둔 이름의 시트를 읽는다. 같은 이름을 여러 스킬이
// 함께 써도 되고(한 번만 읽는다), 파일이 없으면 조용히 넘어가 공용 attack 으로 그려진다.
void Sprites::loadSkillSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label) {
    for (const SkillDef& sk : def.skills) {
        if (sk.motion.empty()) continue;
        std::string key = def.key + "/skill_" + sk.motion;
        if (sheets_.count(key) != 0) continue;  // 다른 스킬이 이미 읽었다
        std::string file = "skill_" + sk.motion + ".png";
        loadSheetFile(dir / file, sk.motionAnim, key, label + "/" + file);
    }
}

// v1.6: 스킬마다 fx= 로 적어 둔 이름의 효과 시트를 읽는다. 규칙은 skill_*.png 와 같다 —
// 같은 이름을 여러 스킬이 함께 써도 한 번만 읽고, 파일이 없으면 조용히 넘어간다.
// 그러면 그리는 쪽이 예전처럼 도형으로 그려서 깨진 그림이 나오지 않는다.
const std::string& soulfireLayerKey(int variant, SoulfirePass pass) {
    static const auto keys = [] {
        std::array<std::array<std::string, 3>, kSoulfireFlameVariants> result;
        constexpr const char* layers[] = {"_ground", "_rear", "_front"};
        for (int i = 0; i < kSoulfireFlameVariants; ++i)
            for (int layer = 0; layer < 3; ++layer)
                result[i][layer] = std::string("soulfire_") + "abcdef"[i] + layers[layer];
        return result;
    }();
    return keys[variant][static_cast<int>(pass)];
}

void Sprites::loadFxSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label) {
    if (def.key == "steel_dragon") {
        const auto load = [&](const char* layer, int cell, float fps = 12.f) {
            const std::string name = std::string("fx_") + layer;
            loadSheetFile(dir / (name + ".png"), AnimDef{fps, false, cell, 1.f},
                          def.key + "/" + name, label + "/" + name + ".png");
        };
        load("fly_thrust", 384);
        load("fly_vent", 384);
        load("idle_vent_body", 256);
        load("idle_vent", 288);
        load("walk_vent", 256, 16.f);
        load("walk_ember", 256, 16.f);
        load("basic_heat", 256, kSteelBasicFps);
        load("basic_projectile", 512, kSteelBasicFps);
        load("basic_hit", 256, kSteelBasicFps);
        for (const auto& atlas : heatBlade::atlases) {
            const auto file = dir / "heat_blade" / atlas.path;
            auto image = loadPng(file, detachFiles_);
            if (!image || image->GetWidth() % atlas.width != 0 ||
                image->GetHeight() % atlas.height != 0 ||
                (image->GetWidth() / atlas.width) * (image->GetHeight() / atlas.height) <
                    static_cast<unsigned>(atlas.frames)) {
                warnings_.push_back(label + "/heat_blade/" + atlas.path + ": 승인 아틀라스 규격 불일치");
                continue;
            }
            SheetData data;
            data.frames = atlas.frames;
            data.frameW = atlas.width;
            data.frameH = atlas.height;
            data.columns = static_cast<int>(image->GetWidth()) / atlas.width;
            data.bmp = std::move(image);
            sheets_[def.key + "/fx_" + atlas.path] = std::move(data);
        }
        for (const char* mask : {"overheat_idle", "overheat_idle_vent", "overheat_move", "overheat_fly", "overheat_attack"})
            load(mask, 256);
    }
    for (const SkillDef& sk : def.skills) {
        if (sk.kind == SkillKind::Procession && def.key == "revenant") {
            const AnimDef sheet{8.f, false, 256, 1.f};
            for (const char* layer : kProcessionLayers) {
                const std::string name = std::string("fx_") + layer;
                loadSheetFile(dir / (name + ".png"), sheet, def.key + "/" + name, label + "/" + name + ".png");
            }
        }
        if (sk.kind == SkillKind::Zone && sk.soulfire) {
            const auto load = [&](const std::string& key, int cell) {
                const std::string name = "fx_" + key;
                loadSheetFile(dir / (name + ".png"), AnimDef{20.f, false, cell, 1.f},
                              def.key + "/" + name, label + "/" + name + ".png");
            };
            load("soulfire_beam", 256);
            load("soulfire_impact", 256);
            load("soulfire_wing", 128);
            load("soulfire_wing_front", 128);
            load("soulfire_orbit", 128);
            load("soulfire_core", 128);
            load("soulfire_fragments", 128);
            load("soulfire_mote", 128);
            load("soulfire_crown", 128);
            load("soulfire_hit_rear", 128);
            load("soulfire_hit_front", 128);
            for (int variant = 0; variant < kSoulfireFlameVariants; ++variant)
                for (SoulfirePass pass : {SoulfirePass::Ground, SoulfirePass::Rear, SoulfirePass::Front})
                    load(soulfireLayerKey(variant, pass), 128);
        }
        if (sk.spectralCleave) {
            for (const char* layer : {"cast_rear", "cast_front", "hit_rear", "hit_front",
                                      "swing", "membrane", "edge", "fragments"}) {
                const std::string name = "fx_" + sk.fx + "_" + layer;
                loadSheetFile(dir / (name + ".png"), sk.fxAnim, def.key + "/" + name,
                              label + "/" + name + ".png");
            }
            continue;
        }
        for (const std::string* effect : {&sk.fx, &sk.fxImpact}) {
            if (effect->empty()) continue;
            std::string key = def.key + "/fx_" + *effect;
            if (sheets_.count(key) != 0) continue;
            std::string file = "fx_" + *effect + ".png";
            AnimDef anim = sk.fxAnim;
            if (sk.furnaceBurst && effect == &sk.fxImpact) anim.frameW = 0;
            loadSheetFile(dir / file, anim, key, label + "/" + file);
        }
    }
}

Sheet Sprites::fxSheet(const std::string& speciesKey, const std::string& fxKey) const {
    Sheet out;
    if (fxKey.empty()) return out;
    auto it = sheets_.find(speciesKey + "/fx_" + fxKey);
    if (it == sheets_.end()) return out;
    out.bmp = it->second.bmp.get();
    out.frames = it->second.frames;
    out.frameW = it->second.frameW;
    out.frameH = it->second.frameH;
    out.columns = it->second.columns;
    return out;
}

Sheet Sprites::skillSheet(const std::string& speciesKey, const std::string& motion) const {
    Sheet out;
    if (motion.empty()) return out;
    auto it = sheets_.find(speciesKey + "/skill_" + motion);
    if (it == sheets_.end()) return out;
    out.bmp = it->second.bmp.get();
    out.frames = it->second.frames;
    out.frameW = it->second.frameW;
    out.frameH = it->second.frameH;
    return out;
}

// ui.txt 의 9조각 여백을 읽는다. 형식은 dragon.txt 와 같은 `[구역]` + `키=값` 이다.
// 파일이 없거나 줄이 이상해도 그냥 넘어간다. 여백이 없으면 늘리지 않고 그대로 그린다.
void Sprites::loadUiSkins(const std::filesystem::path& uiDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(uiDir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(uiDir, ec)) {
        if (!entry.is_directory()) continue;
        std::string skin = entry.path().filename().string();

        // 먼저 여백 표를 읽는다(조각 이름 -> 네 변)
        std::map<std::string, std::array<int, 4>> margins;
        std::map<std::string, UiFit> fits;
        std::ifstream f(entry.path() / "ui.txt");
        std::string line, section;
        while (std::getline(f, line)) {
            // 주석과 공백 제거
            size_t c = line.find_first_of(";#");
            if (c != std::string::npos) line.erase(c);  // substr 로 임시 문자열을 만들지 않는다
            size_t b = line.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) continue;
            size_t e = line.find_last_not_of(" \t\r\n");
            line = line.substr(b, e - b + 1);
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                margins[section];  // 구역만 있어도 자리를 만든다
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos || section.empty()) continue;
            std::string key = line.substr(0, eq);
            if (key == "fit") {
                std::string v = line.substr(eq + 1);
                if (v == "height") fits[section] = UiFit::Height;
                else if (v == "contain") fits[section] = UiFit::Contain;
                else if (v == "nine") fits[section] = UiFit::Nine;
                else warnings_.push_back("ui/" + skin + "/" + section + ": fit 값이 이상합니다 (" + v + ")");
                continue;
            }
            int val = 0;
            try {
                val = std::stoi(line.substr(eq + 1));
            } catch (...) {
                continue;
            }
            if (val < 0) val = 0;
            auto& m = margins[section];
            if (key == "left") m[0] = val;
            else if (key == "top") m[1] = val;
            else if (key == "right") m[2] = val;
            else if (key == "bottom") m[3] = val;
        }

        // PNG를 읽는다. ui.txt 에 없는 조각도 그대로 쓴다(늘리지 않음).
        for (const auto& png : std::filesystem::directory_iterator(entry.path(), ec)) {
            if (png.path().extension() != ".png") continue;
            std::string name = png.path().stem().string();
            auto bmp = loadPng(png.path());
            if (!bmp) {
                warnings_.push_back("ui/" + skin + "/" + name + ".png: 그림을 읽지 못했습니다");
                continue;
            }
            UiData u;
            auto it = margins.find(name);
            if (it != margins.end()) {
                int w = static_cast<int>(bmp->GetWidth());
                int h = static_cast<int>(bmp->GetHeight());
                // 여백이 그림보다 크면 9조각 계산이 뒤집힌다. 줄여서 쓰고 알린다.
                if (it->second[0] + it->second[2] >= w || it->second[1] + it->second[3] >= h) {
                    warnings_.push_back("ui/" + skin + "/" + name + ": 여백이 그림보다 큽니다. 줄여서 씁니다");
                    it->second[0] = std::min(it->second[0], (w - 1) / 2);
                    it->second[2] = std::min(it->second[2], (w - 1) / 2);
                    it->second[1] = std::min(it->second[1], (h - 1) / 2);
                    it->second[3] = std::min(it->second[3], (h - 1) / 2);
                }
                u.left = it->second[0];
                u.top = it->second[1];
                u.right = it->second[2];
                u.bottom = it->second[3];
            }
            auto fi = fits.find(name);
            if (fi != fits.end()) {
                u.fit = fi->second;
                u.hasCfg = true;
            }
            if (it != margins.end()) u.hasCfg = true;
            u.bmp = std::move(bmp);
            ui_[skin + "/" + name] = std::move(u);
        }
    }

    // 스킨에 ui.txt 가 없거나 그 조각이 안 적혀 있으면 default 의 설정을 물려받는다.
    // 그림만 바꾼 스킨(색만 다른 것)이 여백·맞춤 방식을 잃고 통째로 늘어나는 것을 막는다.
    for (auto& [key, data] : ui_) {
        if (data.hasCfg) continue;
        size_t slash = key.find('/');
        if (slash == std::string::npos) continue;
        if (key.compare(0, slash, "default") == 0) continue;
        auto base = ui_.find("default/" + key.substr(slash + 1));
        if (base == ui_.end() || !base->second.hasCfg) continue;
        data.left = base->second.left;
        data.top = base->second.top;
        data.right = base->second.right;
        data.bottom = base->second.bottom;
        data.fit = base->second.fit;
    }
}

UiPiece Sprites::ui(const std::string& skinKey, const std::string& name) const {
    // 고른 스킨에 없으면 default 에서 찾는다. 둘 다 없으면 빈 조각(도형으로 그린다).
    for (const std::string& skin : {skinKey, std::string("default")}) {
        auto it = ui_.find(skin + "/" + name);
        if (it == ui_.end()) continue;
        UiPiece p;
        p.bmp = it->second.bmp.get();
        p.left = it->second.left;
        p.top = it->second.top;
        p.right = it->second.right;
        p.bottom = it->second.bottom;
        p.fit = it->second.fit;
        return p;
    }
    return UiPiece{};
}

std::unique_ptr<Gdiplus::Bitmap> Sprites::loadTowerBackground(int zone) const {
    if (assetDir_.empty() || zone < 1) return nullptr;
    char name[32];
    std::snprintf(name, sizeof(name), "zone_%02d.png", zone);
    return loadPng(assetDir_ / "tower" / "bg" / name);
}

Gdiplus::Bitmap* Sprites::get(const std::string& key) const {
    auto it = images_.find(key);
    return it == images_.end() ? nullptr : it->second.get();
}

Sheet Sprites::sheet(const std::string& speciesKey, AnimState state, AnimState* resolved) const {
    // 대체 규칙(docs/character-pipeline.md 4장): 없는 상태는 idle로 그린다.
    // fly 시트가 없으면 move를 먼저 써 본다(걷는 그림이 가만히 있는 것보다 낫다).
    AnimState order[3] = {state, state == AnimState::Fly ? AnimState::Move : AnimState::Idle, AnimState::Idle};
    for (AnimState s : order) {
        auto it = sheets_.find(speciesKey + "/" + animStateName(s));
        if (it == sheets_.end()) continue;
        if (resolved) *resolved = s;
        Sheet out;
        out.bmp = it->second.bmp.get();
        out.frames = it->second.frames;
        out.frameW = it->second.frameW;
        out.frameH = it->second.frameH;
        if (!it->second.visibleHeight.empty()) out.visibleHeight = it->second.visibleHeight.data();
        return out;
    }
    return Sheet{};
}

bool Sprites::hasAnySheet(const std::string& speciesKey) const {
    return sheets_.find(speciesKey + "/idle") != sheets_.end();
}

}  // namespace dt::win
