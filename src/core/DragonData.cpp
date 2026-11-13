#include "core/DragonData.h"

#include "core/EnemyData.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
    d.skills[0] = {"스킬 1", "가장 가까운 적을 공격한다.", 1, 4.f, SkillKind::Projectile, 1.9f, 0.f, 370.f, 0.f, 0.f};
    d.skills[1] = {"스킬 2", "주변에 피해를 준다.", 3, 8.f, SkillKind::Burst, 1.6f, 110.f, 0.f, 0.f, 0.f};
    d.skills[2] = {"스킬 3", "일직선으로 피해를 준다.", 6, 10.f, SkillKind::Line, 2.6f, 36.f, 320.f, 0.f, 0.f};
    d.anims[static_cast<int>(AnimState::Idle)] = {8.f, true, 0};
    d.anims[static_cast<int>(AnimState::Move)] = {10.f, true, 0};
    d.anims[static_cast<int>(AnimState::Attack)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Hit)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Death)] = {10.f, false, 0};
    d.anims[static_cast<int>(AnimState::Fly)] = {10.f, true, 0};
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
                int n;
                if (parseInt(name.substr(6), n) && n >= 1 && n <= 3) { section = "skill"; skillSec = n - 1; }
                else { warn("스킬 번호는 1~3입니다: [" + name + "]"); sectionValid = false; }
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
            SkillDef& s = r.def.skills[skillSec];
            if (key == "name") s.name = val;
            else if (key == "desc") s.desc = val;
            else if (key == "kind") {
                SkillKind k;
                if (skillKindFromName(val, k)) s.kind = k;
                else warn("모르는 kind=" + val + " (Projectile/Burst/Line/Dash/Heal/Shield/Haste/Zone)");
            }
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
        if (s.unlockLevel < 1) s.unlockLevel = 1;
        if (s.cooldown < 0.5f) s.cooldown = 0.5f;
    }
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

    for (const auto& dir : dirs) {
        std::string key = dir.filename().string();
        if (!key.empty() && (key[0] == '_' || key[0] == '.')) continue;
        if (!validKey(key)) {
            out.warnings.push_back("enemies/" + key + ": 폴더 이름은 영문 소문자·숫자·'-'·'_'만 쓸 수 있어 건너뜁니다");
            continue;
        }
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
    return out;
}

}  // namespace dt
