#include "core/DragonData.h"

#include "core/Config.h"
#include "core/EnemyData.h"
#include "core/TowerData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dt {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return o;
}

// 줄 끝 주석을 지운다: 줄 시작의 ';'/'#', 또는 공백 뒤에 오는 ';'
std::string stripComment(const std::string& line) {
    if (!line.empty() && (line[0] == ';' || line[0] == '#')) return "";
    for (size_t i = 1; i < line.size(); ++i) {
        if (line[i] == ';' && std::isspace(static_cast<unsigned char>(line[i - 1]))) return line.substr(0, i);
    }
    return line;
}

bool parseFloat(const std::string& v, float& out) {
    try {
        size_t used = 0;
        float x = std::stof(v, &used);
        if (used == 0) return false;
        out = x;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& v, int& out) {
    try {
        size_t used = 0;
        int x = std::stoi(v, &used);
        if (used == 0) return false;
        out = x;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool(const std::string& v, bool& out) {
    std::string n = lower(v);
    if (n == "yes" || n == "true" || n == "1" || n == "on") { out = true; return true; }
    if (n == "no" || n == "false" || n == "0" || n == "off") { out = false; return true; }
    return false;
}

// RRGGBB 또는 AARRGGBB 16진수
bool parseColor(const std::string& v, uint32_t& out) {
    std::string h = v;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6 && h.size() != 8) return false;
    for (char c : h)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    uint32_t x = static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16));
    out = h.size() == 6 ? (0xFF000000u | x) : x;
    return true;
}

bool validKey(const std::string& k) {
    if (k.empty() || k.size() > 32) return false;
    for (char c : k) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

SpeciesDef newSpeciesTemplate(const std::string& key) {
    SpeciesDef d;  // SpeciesDef의 기본값이 곧 템플릿 수치다
    d.key = key;
    d.name = key;
    d.role = "미정";
    d.desc = "설명이 아직 없습니다.";
    d.skills = {
        {"스킬 1", "가장 가까운 적을 공격한다.", 1, 4.f, SkillKind::Projectile, 1.9f, 0.f, 370.f, 0.f, 0.f},
        {"스킬 2", "주변에 피해를 준다.", 3, 8.f, SkillKind::Burst, 1.6f, 110.f, 0.f, 0.f, 0.f},
        {"스킬 3", "일직선으로 피해를 준다.", 6, 10.f, SkillKind::Line, 2.6f, 36.f, 320.f, 0.f, 0.f},
    };
    d.anims[static_cast<int>(AnimState::Idle)] = {8.f, true, 0};
    d.anims[static_cast<int>(AnimState::Move)] = {10.f, true, 0};
    d.anims[static_cast<int>(AnimState::Attack)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Hit)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Death)] = {10.f, false, 0};
    d.anims[static_cast<int>(AnimState::Fly)] = {10.f, true, 0};
    d.anims[static_cast<int>(AnimState::Rest)] = {6.f, true, 0};
    return d;
}

bool animStateFromName(const std::string& n, AnimState& out) {
    for (int i = 0; i < kAnimStateCount; ++i) {
        if (n == animStateName(static_cast<AnimState>(i))) {
            out = static_cast<AnimState>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

DragonParseResult parseDragonText(const std::string& text, const std::string& folderKey, const SpeciesDef* base) {
    DragonParseResult r;
    r.def = base ? *base : newSpeciesTemplate(folderKey);
    r.def.key = folderKey;  // 키는 항상 폴더 이름이다

    std::string section = "dragon";
    AnimState animSec = AnimState::Idle;
    int skillSec = -1;  // 0..2
    bool sectionValid = true;

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    auto warn = [&](const std::string& msg) {
        if (r.warnings.size() < 20) r.warnings.push_back(folderKey + "/dragon.txt " + std::to_string(lineNo) + "행: " + msg);
    };

    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string line = trim(stripComment(raw));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') { warn("구역 이름이 ']'로 끝나지 않습니다"); sectionValid = false; continue; }
            std::string name = lower(trim(line.substr(1, line.size() - 2)));
            sectionValid = true;
            if (name == "dragon") {
                section = "dragon";
            } else if (name.rfind("anim.", 0) == 0) {
                AnimState st;
                if (animStateFromName(name.substr(5), st)) { section = "anim"; animSec = st; }
                else { warn("모르는 애니메이션 상태 [" + name + "] (idle/move/attack/hit/death/fly)"); sectionValid = false; }
            } else if (name.rfind("skill.", 0) == 0) {
                // v1.4: 개수 제한을 없앴다. [skill.1] 부터 차례로 적으면 그만큼 늘어난다.
                // 번호를 건너뛰면(1, 3) 사이가 기본값 스킬로 채워지므로 경고한다.
                int n;
                if (parseInt(name.substr(6), n) && n >= 1 && n <= cfg::kMaxSkillsPerDragon) {
                    section = "skill";
                    skillSec = n - 1;
                    if (skillSec >= static_cast<int>(r.def.skills.size())) {
                        if (skillSec > static_cast<int>(r.def.skills.size()))
                            warn("스킬 번호가 건너뛰었습니다: [" + name + "]. 1부터 차례로 적어 주세요");
                        r.def.skills.resize(static_cast<size_t>(skillSec) + 1);
                        // 새로 생긴 칸은 해제 레벨 '자동'(0)으로 둔다. level= 을 적으면 그 값을 쓴다.
                    }
                } else {
                    warn("스킬 번호는 1~" + std::to_string(cfg::kMaxSkillsPerDragon) + "입니다: [" + name + "]");
                    sectionValid = false;
                }
            } else {
                warn("모르는 구역 [" + name + "]");
                sectionValid = false;
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) { warn("키=값 형식이 아닙니다: " + line); continue; }
        if (!sectionValid) continue;  // 모르는 구역의 내용은 이미 경고했으므로 조용히 넘어간다
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) { warn(key + " 값이 비어 있습니다"); continue; }

        bool ok = true;
        if (section == "dragon") {
            SpeciesDef& d = r.def;
            if (key == "key") {
                if (val != folderKey) warn("key=" + val + "는 폴더 이름(" + folderKey + ")과 달라 폴더 이름을 씁니다");
            }
            else if (key == "name") d.name = val;
            else if (key == "ui_skin") d.uiSkin = val;
            else if (key == "role") d.role = val;
            else if (key == "desc") d.desc = val;
            else if (key == "color") ok = parseColor(val, d.color);
            else if (key == "hp") ok = parseFloat(val, d.baseHp);
            else if (key == "atk") ok = parseFloat(val, d.baseAtk);
            else if (key == "def") ok = parseFloat(val, d.baseDef);
            else if (key == "speed") ok = parseFloat(val, d.baseSpeed);
            else if (key == "dodge") ok = parseFloat(val, d.baseDodge);
            else if (key == "regen") ok = parseFloat(val, d.baseRegen);
            else if (key == "hp_growth") ok = parseFloat(val, d.hpGrowth);
            else if (key == "atk_growth") ok = parseFloat(val, d.atkGrowth);
            else if (key == "def_growth") ok = parseFloat(val, d.defGrowth);
            else if (key == "dodge_growth") ok = parseFloat(val, d.dodgeGrowth);
            else if (key == "regen_growth") ok = parseFloat(val, d.regenGrowth);
            else if (key == "flip") ok = parseBool(val, d.flipAllowed);
            else warn("모르는 키 " + key + " ([dragon])");
        } else if (section == "anim") {
            AnimDef& a = r.def.anims[static_cast<int>(animSec)];
            if (key == "fps") ok = parseFloat(val, a.fps);
            else if (key == "loop") ok = parseBool(val, a.loop);
            else if (key == "frame_w") ok = parseInt(val, a.frameW);
            else if (key == "render_scale") ok = parseFloat(val, a.renderScale);
            else if (key == "file") {
                std::string expect = std::string(animStateName(animSec)) + ".png";
                if (val != expect) warn("file=" + val + "는 쓰이지 않습니다. 파일 이름은 " + expect + "로 고정입니다");
            }
            else warn("모르는 키 " + key + " ([anim])");
        } else if (section == "skill" && skillSec >= 0) {
            SkillDef& s = r.def.skills[static_cast<size_t>(skillSec)];
            if (key == "name") s.name = val;
            else if (key == "desc") s.desc = val;
            else if (key == "kind") {
                SkillKind k;
                if (skillKindFromName(val, k)) s.kind = k;
                else warn("모르는 kind=" + val +
                          " (Projectile/Burst/Line/Dash/Heal/Shield/Haste/Zone/PassiveStat/PassiveOnKill)");
            }
            else if (key == "stat") {
                StatKind sk;
                if (statKindFromName(val, sk)) s.stat = sk;
                else warn("모르는 stat=" + val + " (hp/atk/def/speed/dodge/regen)");
            }
            // v1.4 스킬 전용 본체 모션. 파일은 skill_<motion>.png 이다.
            else if (key == "motion") s.motion = val;
            else if (key == "motion_fps") ok = parseFloat(val, s.motionAnim.fps);
            else if (key == "motion_frame_w") ok = parseInt(val, s.motionAnim.frameW);
            else if (key == "motion_render_scale") ok = parseFloat(val, s.motionAnim.renderScale);
            // v1.6 스킬 효과(FX) 스프라이트. 파일은 fx_<fx>.png 이다. 적지 않으면 전처럼 도형만 그린다.
            else if (key == "fx_alpha") ok = parseFloat(val, s.fxAlpha);
            else if (key == "detonate_curse") ok = parseBool(val, s.detonatesCurse);
            else if (key == "apply_curse") ok = parseBool(val, s.appliesCurse);
            else if (key == "procession_shots") {
                int value = 0;
                ok = parseInt(val, value) && value >= 1 && value <= 7;
                if (ok) s.processionShots = value;
            }
            else if (key == "procession_interval" || key == "procession_speed" || key == "procession_lifetime") {
                float value = 0.f;
                const float lo = key == "procession_speed" ? 1.f : 0.05f;
                const float hi = key == "procession_speed" ? 3000.f : 10.f;
                ok = parseFloat(val, value) && value >= lo && value <= hi;
                if (ok && key == "procession_interval") s.processionInterval = value;
                if (ok && key == "procession_speed") s.processionSpeed = value;
                if (ok && key == "procession_lifetime") s.processionLifetime = value;
            }
            else if (key == "grant_soul") ok = parseBool(val, s.grantsSoul);
            else if (key == "ultimate") ok = parseBool(val, s.ultimate);
            else if (key == "ultimate_resource") {
                ok = val == "souls" || val == "none";
                if (ok) s.ultimateUsesSouls = val == "souls";
            }
            else if (key == "scale_with_size") ok = parseBool(val, s.scaleWithSize);
            else if (key == "fx") s.fx = val;
            else if (key == "fx_impact") s.fxImpact = val;
            else if (key == "furnace_burst") ok = parseBool(val, s.furnaceBurst);
            else if (key == "core_overheat") ok = parseBool(val, s.coreOverheat);
            else if (key == "fx_impact_fps") {
                float fps = 0.f;
                ok = parseFloat(val, fps) && (fps == 0.f || (fps >= 1.f && fps <= 60.f));
                if (ok) s.fxImpactFps = fps;
            }
            else if (key == "layered_breath") {
                ok = !val.empty() && val.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
                if (ok) s.layeredBreath = val;
            }
            else if (key == "breath_input_lock") ok = parseFloat(val, s.breathInputLock);
            else if (key == "breath_sustained") ok = parseBool(val, s.sustainedBreath);
            else if (key == "breath_defer_hit") ok = parseBool(val, s.breathDeferHit);
            // 2026-11-18: 타격 프레임. 좌표는 "x,y" (칸 안 px)
            else if (key == "bone_shot") ok = parseBool(val, s.boneShot);
            else if (key == "spectral_cleave") ok = parseBool(val, s.spectralCleave);
            else if (key == "hit_frame") ok = parseInt(val, s.hitFrame) && s.hitFrame >= 0;
            else if (key == "fx_frame") ok = parseInt(val, s.fxFrame) && s.fxFrame >= 0;
            else if (key == "motion_frames") ok = parseInt(val, s.motionFrames) && s.motionFrames >= 0;
            else if (key == "fx_anchor" || key == "fx_origin") {
                size_t comma = val.find(',');
                float x = 0.f, y = 0.f;
                ok = comma != std::string::npos && parseFloat(trim(val.substr(0, comma)), x) &&
                     parseFloat(trim(val.substr(comma + 1)), y);
                if (ok && key == "fx_anchor") { s.hasFxAnchor = true; s.fxAnchorX = x; s.fxAnchorY = y; }
                if (ok && key == "fx_origin") { s.fxOriginX = x; s.fxOriginY = y; }
            }
            else if (key == "fx_front") ok = parseBool(val, s.fxInFront);
            else if (key == "soulfire") ok = parseBool(val, s.soulfire);
            else if (key == "soulfire_takeoff") ok = parseFloat(val, s.soulfireTakeoff) &&
                s.soulfireTakeoff >= 0.2f && s.soulfireTakeoff <= 2.f;
            else if (key == "soulfire_landing") ok = parseFloat(val, s.soulfireLanding) &&
                s.soulfireLanding >= 0.2f && s.soulfireLanding <= 2.f;
            else if (key == "soulfire_angle") ok = parseFloat(val, s.soulfireAngle) &&
                s.soulfireAngle >= 20.f && s.soulfireAngle <= 80.f;
            else if (key == "soulfire_width") ok = parseFloat(val, s.soulfireWidth) &&
                s.soulfireWidth >= 0.1f && s.soulfireWidth <= 3.f;
            else if (key == "soulfire_mouth_x") ok = parseFloat(val, s.soulfireMouthX) &&
                s.soulfireMouthX >= 0.f && s.soulfireMouthX <= 1.f;
            else if (key == "soulfire_mouth_y") ok = parseFloat(val, s.soulfireMouthY) &&
                s.soulfireMouthY >= 0.f && s.soulfireMouthY <= 0.8f;
            else if (key == "soulfire_impact_scale") ok = parseFloat(val, s.soulfireImpactScale) &&
                s.soulfireImpactScale > 0.f && s.soulfireImpactScale <= 3.f;
            else if (key == "soulfire_flame_scale") ok = parseFloat(val, s.soulfireFlameScale) &&
                s.soulfireFlameScale > 0.f && s.soulfireFlameScale <= 0.5f;
            else if (key == "fx_zone_sprite_only") ok = parseBool(val, s.fxZoneSpriteOnly);
            else if (key == "fx_scale") ok = parseFloat(val, s.fxScale);
            else if (key == "fx_offset_x") ok = parseFloat(val, s.fxOffsetX);
            else if (key == "fx_offset_y") ok = parseFloat(val, s.fxOffsetY);
            else if (key == "fx_windup") ok = parseFloat(val, s.fxWindup);
            else if (key == "fx_hold") ok = parseFloat(val, s.fxHold);
            else if (key == "fx_tail") ok = parseFloat(val, s.fxTail);
            else if (key == "fx_delay") {
                float delay = 0.f;
                ok = parseFloat(val, delay) && delay >= 0.f && delay <= 5.f;
                if (ok) s.fxDelay = delay;
            }
            else if (key == "fx_fps") ok = parseFloat(val, s.fxAnim.fps);
            else if (key == "fx_frame_w") ok = parseInt(val, s.fxAnim.frameW);
            else if (key == "fx_render_scale") ok = parseFloat(val, s.fxAnim.renderScale);
            // level= 을 적지 않으면 0(자동)으로 남아 "한 레벨에 하나씩" 규칙이 채운다.
            else if (key == "level") ok = parseInt(val, s.unlockLevel);
            else if (key == "cooldown") ok = parseFloat(val, s.cooldown);
            else if (key == "power") ok = parseFloat(val, s.power);
            else if (key == "radius") ok = parseFloat(val, s.radius);
            else if (key == "range") ok = parseFloat(val, s.range);
            else if (key == "duration") ok = parseFloat(val, s.duration);
            else if (key == "extra") ok = parseFloat(val, s.extra);
            else warn("모르는 키 " + key + " ([skill])");
        }
        if (!ok) warn(key + "=" + val + " 값을 읽을 수 없습니다");
    }

    // 값 범위를 안전하게 자른다
    SpeciesDef& d = r.def;
    auto clampWarn = [&](float& v, float lo, float hi, const char* what) {
        if (v < lo || v > hi) {
            r.warnings.push_back(folderKey + ": " + what + " 값이 범위를 벗어나 " + std::to_string(v < lo ? lo : hi) +
                                 "으로 맞췄습니다");
            v = v < lo ? lo : hi;
        }
    };
    clampWarn(d.baseHp, 1.f, 100000.f, "hp");
    clampWarn(d.baseAtk, 0.f, 10000.f, "atk");
    clampWarn(d.baseDef, 0.f, 10000.f, "def");
    clampWarn(d.baseSpeed, 10.f, 2000.f, "speed");
    clampWarn(d.baseDodge, 0.f, 0.75f, "dodge");
    for (SkillDef& s : d.skills) {
        // 0은 "자동"이라는 뜻이므로 그대로 둔다. 음수만 자동으로 되돌린다.
        if (s.unlockLevel < 0) s.unlockLevel = 0;
        if (s.unlockLevel > cfg::kMaxLevel) {
            r.warnings.push_back(folderKey + ": " + s.name + "의 level=" + std::to_string(s.unlockLevel) +
                                 "은 최고 레벨(" + std::to_string(cfg::kMaxLevel) + ")보다 높아 영영 해제되지 않습니다");
        }
        if (s.cooldown < 0.5f) s.cooldown = 0.5f;
        if (!(s.motionAnim.fps >= 1.f && s.motionAnim.fps <= 60.f)) s.motionAnim.fps = 12.f;
        if (s.motionAnim.frameW < 0) s.motionAnim.frameW = 0;
        if (!(s.motionAnim.renderScale > 0.f)) s.motionAnim.renderScale = 1.f;
        s.motionAnim.renderScale = std::clamp(s.motionAnim.renderScale, 0.25f, 4.f);
        s.motionAnim.loop = false;  // 스킬 모션은 항상 1회 재생이다
        // v1.6 효과(FX). 모션과 같은 울타리를 쓴다.
        if (!(s.fxAnim.fps >= 1.f && s.fxAnim.fps <= 60.f)) s.fxAnim.fps = 12.f;
        if (s.fxAnim.frameW < 0) s.fxAnim.frameW = 0;
        if (!(s.fxAnim.renderScale > 0.f)) s.fxAnim.renderScale = 1.f;
        s.fxAnim.renderScale = std::clamp(s.fxAnim.renderScale, 0.25f, 4.f);
        s.fxAnim.loop = false;  // 효과도 1회 재생이다(지속 단계에서 멈춰 있는다)
        if (!(s.fxScale > 0.f)) s.fxScale = 1.f;
        s.fxScale = std::clamp(s.fxScale, 0.1f, 8.f);
        s.fxOffsetX = std::clamp(s.fxOffsetX, -600.f, 600.f);
        s.fxOffsetY = std::clamp(s.fxOffsetY, -600.f, 600.f);
        // 단계는 0(기본값 쓰기)을 허용한다. 음수와 지나치게 긴 값만 막는다.
        s.fxWindup = std::clamp(s.fxWindup, 0.f, 5.f);
        s.fxHold = std::clamp(s.fxHold, 0.f, 10.f);
        s.fxTail = std::clamp(s.fxTail, 0.f, 5.f);
        s.breathInputLock = std::isfinite(s.breathInputLock) ? std::clamp(s.breathInputLock, 0.f, 1.92f) : 0.f;
        if (s.kind != SkillKind::Line && !s.layeredBreath.empty()) {
            r.warnings.push_back(folderKey + ": layered_breath requires a Line skill");
            s.layeredBreath.clear();
        }
    }
    if (d.skills.size() > static_cast<size_t>(cfg::kMaxSkillsPerDragon))
        d.skills.resize(static_cast<size_t>(cfg::kMaxSkillsPerDragon));
    for (AnimDef& a : d.anims) {
        if (a.fps < 1.f || a.fps > 60.f) a.fps = 8.f;
        if (a.frameW < 0) a.frameW = 0;
        if (!(a.renderScale > 0.f)) a.renderScale = 1.f;
        if (a.renderScale < 0.25f) a.renderScale = 0.25f;
        if (a.renderScale > 4.f) a.renderScale = 4.f;
    }
    return r;
}

DragonScanResult loadDragonFolders(const std::filesystem::path& dragonsDir) {
    DragonScanResult out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dragonsDir, ec)) return out;

    std::vector<std::filesystem::path> dirs;
    for (const auto& e : std::filesystem::directory_iterator(dragonsDir, ec)) {
        if (e.is_directory()) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());  // 등록 순서를 폴더 이름 순으로 고정한다

    for (const auto& dir : dirs) {
        std::string key = dir.filename().string();
        if (!key.empty() && (key[0] == '_' || key[0] == '.')) continue;
        if (!validKey(key)) {
            out.warnings.push_back("dragons/" + key + ": 폴더 이름은 영문 소문자·숫자·'-'·'_'만 쓸 수 있어 건너뜁니다");
            continue;
        }
        Species existing;
        bool known = speciesFromKey(key, existing);
        std::filesystem::path txt = dir / "dragon.txt";
        if (!std::filesystem::exists(txt, ec)) {
            if (!known) {
                out.warnings.push_back("dragons/" + key + ": dragon.txt가 없어 새 종으로 등록하지 못했습니다 "
                                       "(이미지만 쓰려면 내장 종 키(fire/earth/wind/water)와 같은 이름의 폴더여야 합니다)");
            } else {
                ++out.folderCount;  // 이미지 전용 폴더
            }
            continue;
        }
        std::ifstream f(txt, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        DragonParseResult r = parseDragonText(ss.str(), key, known ? &speciesDef(existing) : nullptr);
        for (auto& w : r.warnings) out.warnings.push_back(w);
        registerOrUpdateSpecies(r.def);
        ++out.folderCount;
        if (!known) ++out.newSpecies;
    }
    return out;
}


// ---------------------------------------------------------------------------
// 적 (assets/enemies/<키>/enemy.txt). 위 드래곤 파서와 같은 형식·같은 보조 함수를 쓴다.
// ---------------------------------------------------------------------------

EnemyParseResult parseEnemyText(const std::string& text, const std::string& folderKey, const EnemyDef* base) {
    EnemyParseResult r;
    if (base) r.def = *base;
    r.def.key = folderKey;
    if (r.def.name.empty()) r.def.name = folderKey;

    std::string section = "enemy";
    AnimState animSec = AnimState::Idle;
    bool sectionValid = true;

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    auto warn = [&](const std::string& msg) {
        if (r.warnings.size() < 20) r.warnings.push_back(folderKey + "/enemy.txt " + std::to_string(lineNo) + "행: " + msg);
    };

    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string line = trim(stripComment(raw));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') { warn("구역 이름이 ']'로 끝나지 않습니다"); sectionValid = false; continue; }
            std::string name = lower(trim(line.substr(1, line.size() - 2)));
            sectionValid = true;
            if (name == "enemy") {
                section = "enemy";
            } else if (name.rfind("anim.", 0) == 0) {
                AnimState st;
                if (animStateFromName(name.substr(5), st)) { section = "anim"; animSec = st; }
                else { warn("모르는 애니메이션 상태 [" + name + "] (idle/move/attack/hit/death/fly)"); sectionValid = false; }
            } else {
                warn("모르는 구역 [" + name + "]");
                sectionValid = false;
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) { warn("키=값 형식이 아닙니다: " + line); continue; }
        if (!sectionValid) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) { warn(key + " 값이 비어 있습니다"); continue; }

        bool ok = true;
        if (section == "enemy") {
            EnemyDef& d = r.def;
            if (key == "key") {
                if (val != folderKey) warn("key=" + val + "는 폴더 이름(" + folderKey + ")과 달라 폴더 이름을 씁니다");
            }
            else if (key == "name") d.name = val;
            else if (key == "desc") d.desc = val;
            // v1.6: 그림을 다른 적 폴더에서 가져온다. 시트를 복사하지 않고 변종을 만들기 위한 것이다.
            // 가리킨 적이 실제로 있는지는 폴더를 다 읽은 뒤에 본다(loadEnemyFolders) — 여기서는
            // 이름 모양만 본다. 한 다리만 건넌다: 가리킨 쪽이 또 art_from 을 적어도 따라가지 않는다.
            else if (key == "art_from") {
                std::string v = lower(val);
                if (v == folderKey) warn("art_from=" + val + " 은 자기 자신이라 무시합니다");
                else if (!validKey(v)) warn("art_from=" + val + " 은 폴더 이름 규칙(영문 소문자·숫자·'-'·'_')에 맞지 않아 무시합니다");
                else d.artFrom = v;
            }
            // v1.5 에 넣기로 하고 파서에 빠져 있었다(changelog·codemap 에는 적혀 있다).
            else if (key == "art_facing") {
                std::string v = lower(val);
                if (v == "right") d.artFacesRight = true;
                else if (v == "left") d.artFacesRight = false;
                else ok = false;
            }
            else if (key == "role") {
                std::string v = lower(val);
                if (v == "melee") d.role = EnemyRole::Melee;
                else if (v == "boss") d.role = EnemyRole::Boss;
                else { ok = false; }
            }
            else if (key == "color") ok = parseColor(val, d.color);
            else if (key == "hp") ok = parseFloat(val, d.baseHp);
            else if (key == "atk") ok = parseFloat(val, d.baseAtk);
            else if (key == "def") ok = parseFloat(val, d.baseDef);
            else if (key == "speed") ok = parseFloat(val, d.baseSpeed);
            else if (key == "hp_growth") ok = parseFloat(val, d.hpGrowth);
            else if (key == "atk_growth") ok = parseFloat(val, d.atkGrowth);
            else if (key == "def_growth") ok = parseFloat(val, d.defGrowth);
            else if (key == "speed_growth") ok = parseFloat(val, d.speedGrowth);
            else if (key == "radius") ok = parseFloat(val, d.radius);
            else if (key == "exp") ok = parseInt(val, d.baseExp);
            else if (key == "exp_growth") ok = parseInt(val, d.expGrowth);
            else if (key == "attack_range") ok = parseFloat(val, d.attackRange);
            else if (key == "keep_ratio") ok = parseFloat(val, d.keepRatio);
            else if (key == "windup") ok = parseFloat(val, d.windup);
            else if (key == "attack_cooldown") ok = parseFloat(val, d.attackCooldown);
            else if (key == "phase2") ok = parseBool(val, d.phase2);
            else if (key == "summon") d.summonKey = val;
            else if (key == "summon_count") ok = parseInt(val, d.summonCount);
            else if (key == "summon_level") ok = parseInt(val, d.summonLevel);
            else if (key == "split") d.splitKey = val;
            else if (key == "split_count") ok = parseInt(val, d.splitCount);
            else if (key == "split_every") ok = parseFloat(val, d.splitEvery);
            else if (key == "split_level_drop") ok = parseInt(val, d.splitLevelDrop);
            else if (key == "split_guard") ok = parseFloat(val, d.splitGuard);
            else warn("모르는 키 " + key + " ([enemy])");
        } else if (section == "anim") {
            AnimDef& a = r.def.anims[static_cast<int>(animSec)];
            if (key == "fps") ok = parseFloat(val, a.fps);
            else if (key == "loop") ok = parseBool(val, a.loop);
            else if (key == "frame_w") ok = parseInt(val, a.frameW);
            else if (key == "render_scale") ok = parseFloat(val, a.renderScale);
            else warn("모르는 키 " + key + " ([anim." + animStateName(animSec) + "])");
        }
        if (!ok) warn(key + " 값을 읽을 수 없습니다: " + val);
    }

    // 허용 범위로 맞춘다. 잘못된 값으로 게임이 멈추지 않게 하되, 조용히 넘기지 않고 알린다.
    EnemyDef& d = r.def;
    lineNo = 0;
    auto clampF = [&](const char* nm, float& v, float lo, float hi) {
        float c = std::clamp(v, lo, hi);
        if (c != v) {
            r.warnings.push_back(folderKey + "/enemy.txt: " + nm + "=" + std::to_string(v) +
                                 " 는 허용 범위를 벗어나 " + std::to_string(c) + " 로 맞췄습니다");
            v = c;
        }
    };
    auto clampI = [&](const char* nm, int& v, int lo, int hi) {
        int c = std::clamp(v, lo, hi);
        if (c != v) {
            r.warnings.push_back(folderKey + "/enemy.txt: " + nm + "=" + std::to_string(v) +
                                 " 는 허용 범위를 벗어나 " + std::to_string(c) + " 로 맞췄습니다");
            v = c;
        }
    };
    clampF("radius", d.radius, 4.f, 400.f);
    clampF("attack_range", d.attackRange, 8.f, 4000.f);
    clampF("keep_ratio", d.keepRatio, 0.05f, 1.f);
    clampF("windup", d.windup, 0.f, 5.f);
    clampF("attack_cooldown", d.attackCooldown, 0.1f, 30.f);
    clampI("summon_count", d.summonCount, 0, 8);
    clampI("summon_level", d.summonLevel, 1, 99);
    clampI("split_count", d.splitCount, 0, 6);
    clampF("split_every", d.splitEvery, 0.f, 1.f);
    clampI("split_level_drop", d.splitLevelDrop, 0, 500);
    clampF("split_guard", d.splitGuard, 0.f, 0.95f);
    clampI("exp", d.baseExp, 0, 100000);
    clampI("exp_growth", d.expGrowth, 0, 100000);
    for (int i = 0; i < kAnimStateCount; ++i) {
        AnimDef& a = d.anims[i];
        if (!(a.fps > 0.f) || a.fps > 60.f) a.fps = 8.f;
        if (a.frameW < 0) a.frameW = 0;
        if (!(a.renderScale > 0.f)) a.renderScale = 1.f;
        a.renderScale = std::clamp(a.renderScale, 0.25f, 4.f);
    }
    return r;
}

EnemyScanResult loadEnemyFolders(const std::filesystem::path& enemiesDir) {
    EnemyScanResult out;
    std::error_code ec;
    if (!std::filesystem::is_directory(enemiesDir, ec)) return out;

    std::vector<std::filesystem::path> dirs;
    for (const auto& e : std::filesystem::directory_iterator(enemiesDir, ec)) {
        if (e.is_directory()) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());

    std::vector<std::string> seen;  // 이번에 읽은 폴더 이름
    for (const auto& dir : dirs) {
        std::string key = dir.filename().string();
        if (!key.empty() && (key[0] == '_' || key[0] == '.')) continue;
        if (!validKey(key)) {
            out.warnings.push_back("enemies/" + key + ": 폴더 이름은 영문 소문자·숫자·'-'·'_'만 쓸 수 있어 건너뜁니다");
            continue;
        }
        seen.push_back(key);
        EnemyKind existing;
        bool known = enemyFromKey(key, existing);
        std::filesystem::path txt = dir / "enemy.txt";
        if (!std::filesystem::exists(txt, ec)) {
            if (!known) {
                out.warnings.push_back("enemies/" + key + ": enemy.txt가 없어 새 적으로 등록하지 못했습니다");
            } else {
                ++out.folderCount;  // 이미지 전용 폴더
            }
            continue;
        }
        std::ifstream f(txt, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        EnemyParseResult r = parseEnemyText(ss.str(), key, known ? &enemyDef(existing) : nullptr);
        for (auto& w : r.warnings) out.warnings.push_back(w);
        registerOrUpdateEnemy(r.def);
        ++out.folderCount;
        if (!known) ++out.newEnemies;
    }

    // v1.6: art_from 이 가리킨 폴더가 실제로 있는지 폴더를 다 읽은 뒤에 본다.
    // 폴더 순서(알파벳)와 상관없이 보려면 여기서 봐야 한다. 조용히 넘기면 변종이 그림 없이
    // 도형으로만 나오고 왜 그런지 알 수 없다(v0.4에서 조용히 잘려 고생한 것과 같은 자리다).
    // **이번에 읽은 폴더만 본다.** 등록부에는 다른 폴더에서 읽힌 적도 남아 있어서, 전체를 돌면
    // 지금 보고 있지도 않은 적의 경고가 섞여 나온다.
    for (int i = 0; i < enemyCount(); ++i) {
        const EnemyDef& d = enemyDef(static_cast<EnemyKind>(i));
        if (d.artFrom.empty()) continue;
        if (std::find(seen.begin(), seen.end(), d.key) == seen.end()) continue;
        if (!std::filesystem::is_directory(enemiesDir / d.artFrom, ec)) {
            out.warnings.push_back("enemies/" + d.key + ": art_from=" + d.artFrom +
                                   " 폴더가 없어 그림을 빌릴 수 없습니다(도형으로 그려집니다)");
            continue;
        }
        EnemyKind src;
        if (enemyFromKey(d.artFrom, src) && !enemyDef(src).artFrom.empty()) {
            out.warnings.push_back("enemies/" + d.key + ": art_from=" + d.artFrom +
                                   " 도 art_from 을 쓰고 있습니다. 한 다리만 건너므로 원본 폴더를 직접 가리켜 주세요");
        }
    }
    return out;
}


// ---------------------------------------------------------------------------
// 탑 층 (assets/tower/floors.txt). 위 두 파서와 같은 형식·같은 보조 함수를 쓴다.
// ---------------------------------------------------------------------------

namespace {

// "slime x2 lv4 inset=60" 을 해석한다. 첫 낱말이 적 키고 나머지는 순서에 상관없다.
// 키 자리에 '-' 를 적으면 그 층의 기본 적(보스 층이면 보스)을 쓴다.
bool parseSpawn(const std::string& val, TowerSpawn& out, std::string& err) {
    std::istringstream ts(val);
    std::string tok;
    bool first = true;
    while (ts >> tok) {
        if (first) {
            first = false;
            if (tok != "-") {
                if (!validKey(tok)) {
                    err = "적 키 '" + tok + "' 에 쓸 수 없는 글자가 있습니다";
                    return false;
                }
                out.key = tok;
            }
            continue;
        }
        std::string t = lower(tok);
        if (t.size() > 1 && t[0] == 'x') {
            if (!parseInt(t.substr(1), out.count)) {
                err = "마리 수를 읽을 수 없습니다: " + tok;
                return false;
            }
        } else if (t.size() > 2 && t.compare(0, 2, "lv") == 0) {
            if (!parseInt(t.substr(2), out.level)) {
                err = "레벨을 읽을 수 없습니다: " + tok;
                return false;
            }
        } else if (t.compare(0, 6, "inset=") == 0) {
            if (!parseFloat(t.substr(6), out.inset)) {
                err = "inset 값을 읽을 수 없습니다: " + tok;
                return false;
            }
        } else {
            err = "모르는 낱말 '" + tok + "' (쓸 수 있는 것: x<마리> lv<레벨> inset=<px>)";
            return false;
        }
    }
    if (first) {
        err = "값이 비어 있습니다";
        return false;
    }
    return true;
}

}  // namespace

TowerParseResult parseTowerText(const std::string& text) {
    TowerParseResult r;

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    int curFloor = -1;  // r.floors 의 첨자. -1이면 아직 [floor.N] 이 나오지 않았다
    bool sectionValid = true;

    auto warn = [&](const std::string& msg) {
        if (r.warnings.size() < 20) r.warnings.push_back("tower/floors.txt " + std::to_string(lineNo) + "행: " + msg);
    };
    auto note = [&](const std::string& msg) {
        if (r.warnings.size() < 20) r.warnings.push_back("tower/floors.txt: " + msg);
    };

    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string line = trim(stripComment(raw));
        if (line.empty()) continue;

        if (line[0] == '[') {
            if (line.back() != ']') {
                warn("구역 이름이 ']'로 끝나지 않습니다");
                sectionValid = false;
                continue;
            }
            std::string name = lower(trim(line.substr(1, line.size() - 2)));
            sectionValid = true;
            if (name.compare(0, 6, "floor.") == 0) {
                int n = 0;
                if (!parseInt(name.substr(6), n) || n < 1) {
                    warn("층 번호를 읽을 수 없습니다: [" + name + "]. [floor.1] 부터 차례로 적어 주세요");
                    sectionValid = false;
                    continue;
                }
                if (n > cfg::kMaxTowerFloors) {
                    warn("층은 1~" + std::to_string(cfg::kMaxTowerFloors) + "까지입니다: [" + name + "]");
                    sectionValid = false;
                    continue;
                }
                // 번호를 건너뛰면 몇 층인지 사람과 코드의 생각이 달라진다. dragon.txt 의 [skill.N] 과 같은 규칙이다.
                if (n != static_cast<int>(r.floors.size()) + 1) {
                    warn("층 번호가 차례가 아닙니다: [" + name + "]. [floor." + std::to_string(r.floors.size() + 1) +
                         "] 이어야 합니다");
                    sectionValid = false;
                    continue;
                }
                r.floors.push_back(TowerFloorDef{});
                curFloor = static_cast<int>(r.floors.size()) - 1;
            } else {
                warn("모르는 구역 [" + name + "] (쓸 수 있는 것: [floor.N])");
                sectionValid = false;
            }
            continue;
        }

        if (!sectionValid) continue;
        if (curFloor < 0) {
            warn("[floor.N] 구역 안에 적어야 합니다: " + line);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn("키=값 형식이 아닙니다: " + line);
            continue;
        }
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) {
            warn(key + " 값이 비어 있습니다");
            continue;
        }

        TowerFloorDef& f = r.floors[static_cast<size_t>(curFloor)];
        bool ok = true;
        if (key == "title") {
            f.title = val;
        } else if (key == "spacing") {
            ok = parseFloat(val, f.spacing);
        } else if (key == "boss") {
            ok = parseBool(val, f.boss);
        } else if (key == "enemy") {
            if (f.spawns.size() >= static_cast<size_t>(cfg::kMaxSpawnLinesPerFloor)) {
                warn("한 층에 적을 수 있는 enemy= 는 " + std::to_string(cfg::kMaxSpawnLinesPerFloor) + "줄까지입니다");
            } else {
                TowerSpawn s;
                std::string err;
                if (parseSpawn(val, s, err)) {
                    f.spawns.push_back(s);
                } else {
                    warn("enemy=" + val + " — " + err);
                }
            }
        } else {
            warn("모르는 키 " + key + " ([floor.N]: title/spacing/boss/enemy)");
        }
        if (!ok) warn(key + "=" + val + " 값을 읽을 수 없습니다");
    }

    // 범위 밖 값은 조용히 자르지 않고 알린다 (v0.4에서 enemy.txt 가 조용히 잘려 고생했다).
    auto clampF = [&](const std::string& where, const char* nm, float& v, float lo, float hi) {
        if (v < lo || v > hi) {
            note(where + "의 " + nm + "=" + std::to_string(v) + " 가 범위를 벗어나 " +
                 std::to_string(v < lo ? lo : hi) + " 로 맞췄습니다");
            v = v < lo ? lo : hi;
        }
    };
    auto clampI = [&](const std::string& where, const char* nm, int& v, int lo, int hi) {
        if (v < lo || v > hi) {
            note(where + "의 " + nm + "=" + std::to_string(v) + " 가 범위를 벗어나 " +
                 std::to_string(v < lo ? lo : hi) + " 로 맞췄습니다");
            v = v < lo ? lo : hi;
        }
    };

    std::vector<TowerFloorDef> kept;
    for (size_t i = 0; i < r.floors.size(); ++i) {
        TowerFloorDef f = r.floors[i];
        std::string where = std::to_string(i + 1) + "층";
        clampF(where, "spacing", f.spacing, 0.f, 600.f);
        for (TowerSpawn& s : f.spawns) {
            clampI(where, "x(마리 수)", s.count, 1, cfg::kMaxEnemiesPerFloor);
            clampI(where, "lv(레벨)", s.level, 1, cfg::kMaxEnemyLevel);
            clampF(where, "inset", s.inset, -400.f, 400.f);
        }
        // 적이 없는 층은 들어가는 순간 돌파된다. 그러면 탑이 공짜로 올라가므로 빼고 알린다.
        if (f.spawns.empty()) {
            note(where + "에 enemy= 가 없어 그 층을 뺐습니다. 뒤 층의 번호가 하나씩 앞으로 옮겨집니다");
            continue;
        }
        kept.push_back(f);
    }
    r.floors = kept;
    return r;
}

TowerScanResult loadTowerFloors(const std::filesystem::path& towerDir) {
    TowerScanResult out;
    std::error_code ec;
    std::filesystem::path txt = towerDir / "floors.txt";
    if (!std::filesystem::exists(txt, ec)) return out;  // found=false → 부르는 쪽이 내장 구성을 쓴다

    std::ifstream f(txt, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    TowerParseResult r = parseTowerText(ss.str());
    out.warnings = r.warnings;
    if (r.floors.empty()) {
        out.warnings.push_back("tower/floors.txt: 층을 하나도 읽지 못해 내장 구성(3층)을 씁니다");
        return out;
    }
    out.found = true;
    out.floors = r.floors;
    return out;
}

}  // namespace dt
