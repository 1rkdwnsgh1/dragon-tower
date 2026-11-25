#include "core/SaveData.h"
#include "core/Tower.h"

#include <algorithm>
#include <fstream>

#include "core/Config.h"
#include <sstream>
#include <system_error>

namespace dt {

const char* combatModeName(CombatMode m) {
    switch (m) {
        case CombatMode::Direct: return "직접 조작";
        case CombatMode::SkillManual: return "스킬 수동";
        case CombatMode::Auto: return "자동 사냥";
    }
    return "";
}

std::string placeLabel(const std::string& place) {
    if (place == "hunt") return "바탕화면 사냥";
    if (place == "auto") return "자동 사냥";
    if (place.rfind("tower", 0) == 0 && place.size() > 5) return "탑 " + place.substr(5) + "층";
    return place;
}

namespace {

int toInt(const std::string& v, int fallback) {
    try {
        size_t used = 0;
        int x = std::stoi(v, &used);
        return used > 0 ? x : fallback;
    } catch (...) {
        return fallback;
    }
}

long long toInt64(const std::string& v, long long fallback) {
    try {
        size_t used = 0;
        long long x = std::stoll(v, &used);
        return used > 0 ? x : fallback;
    } catch (...) {
        return fallback;
    }
}

float toFloat(const std::string& v, float fallback) {
    try {
        size_t used = 0;
        float x = std::stof(v, &used);
        return used > 0 ? x : fallback;
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}
std::set<int> readFloors(const std::string& value) {
    std::set<int> floors;
    for (const auto& part : split(value, ',')) {
        const int floor = toInt(part, 0);
        if (floorType(floor) != FloorType::Invalid) floors.insert(floor);
    }
    return floors;
}

void writeFloors(std::ostream& out, const std::string& key, const std::set<int>& floors) {
    out << key << '=';
    const char* separator = "";
    for (int floor : floors) {
        out << separator << floor;
        separator = ",";
    }
    out << "\n";
}


}  // namespace

namespace {
// v1.4 장착 칸을 "0,2,-1,5" 처럼 한 줄로 적는다. 칸 수가 바뀌어도 읽을 수 있게
// 쉼표로 나눠 읽고, 모자라면 -1(빈 칸)로 둔다.
std::string slotsText(const std::array<int, kSkillSlots>& slots) {
    std::string out;
    for (int i = 0; i < kSkillSlots; ++i) {
        if (i) out += ',';
        out += std::to_string(slots[static_cast<size_t>(i)]);
    }
    return out;
}

void parseSlots(const std::string& val, std::array<int, kSkillSlots>& out) {
    out.fill(-1);
    size_t pos = 0;
    for (int i = 0; i < kSkillSlots && pos <= val.size(); ++i) {
        size_t comma = val.find(',', pos);
        std::string part = val.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        out[static_cast<size_t>(i)] = part.empty() ? -1 : toInt(part, -1);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}
}  // namespace

std::string serializeSave(const SaveData& s) {
    std::ostringstream o;
    o << "# Dragon Tower save\n";
    o << "version=" << s.version << "\n";
    o << "nextDragonId=" << s.nextDragonId << "\n";
    o << "huntMode=" << static_cast<int>(s.huntMode) << "\n";
    o << "ui.panelX=" << s.panelX << "\n";
    o << "ui.panelY=" << s.panelY << "\n";
    o << "ui.panelCollapsed=" << (s.panelCollapsed ? 1 : 0) << "\n";
    o << "ui.confineToBand=" << (s.confineToBand ? 1 : 0) << "\n";
    o << "ui.hintShown=" << (s.hintShown ? 1 : 0) << "\n";
    o << "keepRange=" << s.keepRange << "\n";
    o << "ui.menuX=" << s.menuX << "\n";
    o << "ui.menuY=" << s.menuY << "\n";
    o << "ui.menuScale=" << s.menuScale << "\n";
    o << "ui.charScale=" << s.charScale << "\n";
    o << "ui.powerSave=" << (s.powerSave ? 1 : 0) << "\n";
    o << "ui.taskbarSkin=" << (s.taskbarSkin ? 1 : 0) << "\n";
    o << "ui.menuOpen=" << (s.menuOpen ? 1 : 0) << "\n";
    o << "ui.menuTab=" << s.menuTab << "\n";
    o << "bestFloor=" << s.bestFloor << "\n";
    o << "towerClears=" << s.towerClears << "\n";
    // 탑 100층 T2(2026-11-18). 모르는 키는 옛 빌드가 무시하므로 호환이 깨지지 않는다.
    o << "credits=" << s.credits << "\n";
    o << "inv.potionSmall=" << s.potionSmall << "\n";
    o << "inv.potionLarge=" << s.potionLarge << "\n";
    o << "inv.potionFury=" << s.potionFury << "\n";
    o << "inv.potionGuard=" << s.potionGuard << "\n";
    o << "inv.potionSwift=" << s.potionSwift << "\n";
    o << "hunterCharmUntil=" << s.hunterCharmUntil << "\n";
    o << "inv.eggs=" << s.eggs << "\n";
    o << "freeDragonsUsed=" << s.freeDragonsUsed << "\n";
    o << "tower.exact=1\n";
    writeFloors(o, "tower.firstClearFloors", s.firstClearFloors);
    o << "tower.firstClear=" << s.firstClear << "\n";
    o << "tower.fieldBossCleared=";
    {
        bool first = true;
        for (int f : s.fieldBossCleared) {
            if (!first) o << ",";
            o << f;
            first = false;
        }
    }
    o << "\n";
    o << "dragon.present=" << (s.hasDragon ? 1 : 0) << "\n";
    if (s.hasDragon) {
        const DragonRecord& d = s.dragon;
        o << "dragon.id=" << d.id << "\n";
        o << "dragon.species=" << speciesDef(d.species).key << "\n";
        o << "dragon.alive=" << (d.alive ? 1 : 0) << "\n";
        o << "dragon.level=" << d.level << "\n";
        o << "dragon.exp=" << d.exp << "\n";
        o << "dragon.autoExp=" << d.autoExp << "\n";
        if (d.awakened) o << "dragon.awakened=1\n";
        o << "dragon.hp=" << d.hp << "\n";
        o << "dragon.slots=" << slotsText(d.slots) << "\n";
        o << "dragon.floor=" << d.floor << "\n";
        writeFloors(o, "dragon.clearedFloors", d.clearedFloors);
        o << "dragon.cleared=" << d.cleared << "\n";
    }
    // v1.3 보유 목록. 위의 dragon.* 는 지금 플레이 중인 개체를 가리키며 그대로 둔다
    // (옛 빌드에서도 저장 파일을 읽을 수 있게 하기 위해서다).
    o << "stable.count=" << s.stable.size() << "\n";
    o << "stable.active=" << s.activeIndex << "\n";
    for (size_t i = 0; i < s.stable.size(); ++i) {
        const DragonRecord& d = s.stable[i];
        std::string pre = "stable." + std::to_string(i) + ".";
        o << pre << "id=" << d.id << "\n";
        o << pre << "species=" << speciesDef(d.species).key << "\n";
        o << pre << "alive=" << (d.alive ? 1 : 0) << "\n";
        o << pre << "level=" << d.level << "\n";
        o << pre << "exp=" << d.exp << "\n";
        o << pre << "autoExp=" << d.autoExp << "\n";
        if (d.awakened) o << pre << "awakened=1\n";
        o << pre << "hp=" << d.hp << "\n";
        o << pre << "slots=" << slotsText(d.slots) << "\n";
        o << pre << "floor=" << d.floor << "\n";
        writeFloors(o, pre + "clearedFloors", d.clearedFloors);
        o << pre << "cleared=" << d.cleared << "\n";
    }
    o << "codex.species=";
    bool first = true;
    for (const std::string& k : s.speciesSeen) {  // set이라 저장 순서가 항상 같다
        if (k.empty()) continue;
        if (!first) o << ",";
        o << k;
        first = false;
    }
    o << "\n";
    o << "codex.enemies=";
    {
        bool f2 = true;
        for (const std::string& k : s.enemiesSeen) {
            if (k.empty()) continue;
            if (!f2) o << ",";
            o << k;
            f2 = false;
        }
    }
    o << "\n";
    for (const GraveEntry& g : s.graves) {
        o << "grave=" << g.id << "|" << speciesDef(g.species).key << "|" << g.level << "|" << g.place << "\n";
    }
    return o.str();
}

bool deserializeSave(const std::string& text, SaveData& out) {
    SaveData s;
    bool sawVersion = false;
    bool exactTower = false;
    bool sawFreeUsed = false;
    bool dragonPresent = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "version") { s.version = toInt(val, 1); sawVersion = true; }
        else if (key == "nextDragonId") s.nextDragonId = std::max(1, toInt(val, 1));
        else if (key == "ui.panelX") s.panelX = toFloat(val, -1.f);
        else if (key == "ui.panelY") s.panelY = toFloat(val, -1.f);
        else if (key == "ui.panelCollapsed") s.panelCollapsed = toInt(val, 0) != 0;
        else if (key == "ui.confineToBand") s.confineToBand = toInt(val, 0) != 0;
        else if (key == "ui.hintShown") s.hintShown = toInt(val, 0) != 0;
        else if (key == "keepRange") s.keepRange = std::clamp(toInt(val, 0), 0, cfg::kKeepRangeCount - 1);
        else if (key == "ui.menuX") s.menuX = toFloat(val, -1.f);
        else if (key == "ui.menuY") s.menuY = toFloat(val, -1.f);
        // 배수는 범위를 벗어난 값이 들어와도 쓸 수 있게 맞춘다(손으로 고친 저장 파일 대비).
        else if (key == "ui.menuScale") s.menuScale = std::clamp(toFloat(val, 1.f), cfg::kMenuScaleMin, cfg::kMenuScaleMax);
        else if (key == "ui.charScale") s.charScale = std::clamp(toFloat(val, 1.f), cfg::kCharScaleMin, cfg::kCharScaleMax);
        else if (key == "ui.powerSave") s.powerSave = toInt(val, 0) != 0;
        else if (key == "ui.taskbarSkin") s.taskbarSkin = toInt(val, 1) != 0;
        else if (key == "ui.menuOpen") s.menuOpen = toInt(val, 0) != 0;
        else if (key == "ui.menuTab") s.menuTab = std::clamp(toInt(val, 0), 0, 1);
        else if (key == "huntMode") {
            int m = toInt(val, 1);
            s.huntMode = (m >= 0 && m <= 2) ? static_cast<CombatMode>(m) : CombatMode::SkillManual;
        }
        else if (key == "bestFloor") s.bestFloor = std::max(0, toInt(val, 0));
        else if (key == "towerClears") s.towerClears = std::max(0, toInt(val, 0));
        else if (key == "dragon.present") dragonPresent = toInt(val, 0) != 0;
        else if (key == "stable.count") {
            // T2: 슬롯이 늘어날 수 있어 상한은 kMaxStableTotal 이다(실제 수는 stableCapacity).
            int n = std::clamp(toInt(val, 0), 0, cfg::kMaxStableTotal);
            s.stable.assign(static_cast<size_t>(n), DragonRecord{});
        }
        else if (key == "stable.active") s.activeIndex = toInt(val, -1);
        else if (key.rfind("stable.", 0) == 0) {
            // stable.<번호>.<항목>
            size_t dot = key.find('.', 7);
            if (dot != std::string::npos) {
                int idx = toInt(key.substr(7, dot - 7), -1);
                std::string f = key.substr(dot + 1);
                if (idx >= 0 && idx < static_cast<int>(s.stable.size())) {
                    DragonRecord& d = s.stable[static_cast<size_t>(idx)];
                    if (f == "id") d.id = toInt(val, 0);
                    else if (f == "species") { Species sp; if (speciesFromKey(val, sp)) d.species = sp; }
                    else if (f == "alive") d.alive = toInt(val, 1) != 0;
                    else if (f == "level") d.level = std::clamp(toInt(val, 1), 1, cfg::kMaxLevel);
                    else if (f == "exp") d.exp = std::max(0, toInt(val, 0));
                    else if (f == "autoExp") d.autoExp = std::max(0, toInt(val, 0));
                    else if (f == "awakened") d.awakened = val == "1";
                    else if (f == "hp") d.hp = toFloat(val, -1.f);
                    else if (f == "slots") parseSlots(val, d.slots);
                    else if (f == "floor") d.floor = std::clamp(toInt(val, 1), 1, cfg::kTowerTopFloor);
                    else if (f == "clearedFloors") d.clearedFloors = readFloors(val);
                    else if (f == "cleared") d.cleared = std::clamp(toInt(val, 0), 0, cfg::kTowerTopFloor);
                }
            }
        }
        else if (key == "dragon.id") s.dragon.id = toInt(val, 0);
        // 폴더가 사라진 종이라도 다른 종으로 바뀌지 않도록 자리 표시 종을 등록한다.
        else if (key == "dragon.species") { if (!val.empty()) s.dragon.species = ensureSpecies(val); }
        else if (key == "dragon.alive") s.dragon.alive = toInt(val, 0) != 0;
        // 보유 목록의 stable.N.level 과 **같은 규칙**을 쓴다. 예전에는 여기만 상한이 없어
        // 손으로 고친 저장 파일이 레벨 34로 읽혔고, 그 값이 다시 저장됐다가 다음에 켤 때
        // 30으로 잘렸다 — 켤 때마다 값이 달라지는 상태였다. (퍼저가 찾았다.)
        else if (key == "dragon.level") s.dragon.level = std::clamp(toInt(val, 1), 1, cfg::kMaxLevel);
        else if (key == "dragon.exp") s.dragon.exp = std::max(0, toInt(val, 0));
        else if (key == "dragon.autoExp") s.dragon.autoExp = std::max(0, toInt(val, 0));
        else if (key == "dragon.awakened") s.dragon.awakened = val == "1";
        else if (key == "dragon.hp") s.dragon.hp = toFloat(val, -1.f);
        else if (key == "dragon.slots") parseSlots(val, s.dragon.slots);
        else if (key == "dragon.floor") s.dragon.floor = std::clamp(toInt(val, 1), 1, cfg::kTowerTopFloor);
        else if (key == "dragon.clearedFloors") s.dragon.clearedFloors = readFloors(val);
        else if (key == "tower.exact") exactTower = val == "1";
        else if (key == "tower.firstClearFloors") s.firstClearFloors = readFloors(val);
        else if (key == "dragon.cleared") s.dragon.cleared = std::clamp(toInt(val, 0), 0, cfg::kTowerTopFloor);
        else if (key == "credits") s.credits = std::clamp(toInt64(val, 0), 0LL, cfg::kMaxCredits);
        else if (key == "inv.potionSmall") s.potionSmall = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "inv.potionLarge") s.potionLarge = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "inv.potionFury") s.potionFury = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "inv.potionGuard") s.potionGuard = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "inv.potionSwift") s.potionSwift = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "hunterCharmUntil") s.hunterCharmUntil = std::max(0LL, toInt64(val, 0));
        else if (key == "inv.eggs") s.eggs = std::clamp(toInt(val, 0), 0, cfg::kMaxInventory);
        else if (key == "freeDragonsUsed") { s.freeDragonsUsed = std::clamp(toInt(val, 0), 0, cfg::kFreeDragonPicks); sawFreeUsed = true; }
        else if (key == "tower.firstClear") s.firstClear = std::clamp(toInt(val, 0), 0, cfg::kTowerTopFloor);
        else if (key == "tower.fieldBossCleared") {
            // 필드 보스층(10의 배수)만 받는다. 손으로 고친 값이 섞여도 슬롯 수가 부풀지 않게.
            s.fieldBossCleared.clear();
            for (const std::string& part : split(val, ',')) {
                if (part.empty()) continue;
                int f = toInt(part, 0);
                if (floorType(f) == FloorType::FieldBoss) s.fieldBossCleared.insert(f);
            }
        }
        else if (key == "codex.species") {
            // 모르는 키도 그대로 보관한다(폴더를 다시 넣으면 도감이 되살아난다).
            for (const std::string& k : split(val, ',')) {
                if (!k.empty()) s.speciesSeen.insert(k);
            }
        }
        else if (key == "codex.enemies") {
            for (const std::string& k : split(val, ',')) {
                if (!k.empty()) s.enemiesSeen.insert(k);
            }
        }
        // v0.3 이전 저장 파일 호환: 임프·수호자 기록을 키로 옮긴다.
        else if (key == "codex.imp") { if (toInt(val, 0) != 0) s.enemiesSeen.insert("imp"); }
        else if (key == "codex.guardian") { if (toInt(val, 0) != 0) s.enemiesSeen.insert("guardian"); }
        else if (key == "grave") {
            auto parts = split(val, '|');
            if (parts.size() >= 4) {
                GraveEntry g;
                g.id = toInt(parts[0], 0);
                if (!parts[1].empty()) g.species = ensureSpecies(parts[1]);
                g.level = std::max(1, toInt(parts[2], 1));
                g.place = parts[3];
                s.graves.push_back(g);
            }
        }
    }
    if (!sawVersion) return false;
    if (!exactTower) {
        // 기존 순차 탑은 실제 10층까지였으므로 그 구간만 이관한다.
        const auto migrate = [](int highest, std::set<int>& records) {
            for (int f = 1; f <= std::min(highest, 10); ++f) records.insert(f);
        };
        migrate(s.dragon.cleared, s.dragon.clearedFloors);
        for (auto& d : s.stable) migrate(d.cleared, d.clearedFloors);
        migrate(s.firstClear, s.firstClearFloors);
        if (s.towerClears > 0) s.firstClearFloors.insert(10);
    }
    // 죽은 개체는 현재 개체로 불러오지 않는다(재실행으로 부활 방지).
    s.hasDragon = dragonPresent && s.dragon.alive;

    // v1.3: 보유 목록이 없는(v1.2 이전) 저장 파일이면 지금 개체 하나로 목록을 만든다.
    if (s.stable.empty()) {
        if (s.hasDragon) {
            s.stable.push_back(s.dragon);
            s.activeIndex = 0;
        } else {
            s.activeIndex = -1;
        }
    } else {
        // 죽은 개체는 목록에 남기지 않는다(묘비로만 남는다).
        std::vector<DragonRecord> alive;
        int newActive = -1;
        for (size_t i = 0; i < s.stable.size(); ++i) {
            if (!s.stable[i].alive) continue;
            if (static_cast<int>(i) == s.activeIndex) newActive = static_cast<int>(alive.size());
            alive.push_back(s.stable[i]);
        }
        s.stable = std::move(alive);
        s.activeIndex = newActive;
        if (s.activeIndex >= 0) {
            s.dragon = s.stable[static_cast<size_t>(s.activeIndex)];
            s.hasDragon = true;
        } else {
            s.hasDragon = false;
        }
    }
    // v1.4: 장착 칸이 없는(v1.3 이전) 저장 파일이면 여기서 채워진다. 값이 있어도
    // 종의 스킬 목록이 바뀌었을 수 있으므로 한 번 검사하고 빈 칸을 메운다.
    for (DragonRecord& d : s.stable) fillEmptySkillSlots(d);
    if (s.hasDragon) {
        fillEmptySkillSlots(s.dragon);
        if (s.activeIndex >= 0 && s.activeIndex < static_cast<int>(s.stable.size()))
            s.stable[static_cast<size_t>(s.activeIndex)].slots = s.dragon.slots;
    }
    for (const DragonRecord& d : s.stable)
        if (d.id >= s.nextDragonId) s.nextDragonId = d.id + 1;
    if (s.hasDragon && s.dragon.id >= s.nextDragonId) s.nextDragonId = s.dragon.id + 1;
    // 2026-11-24 이전 저장: 이미 가진·잃은 드래곤을 모두 공짜로 고른 것으로 친다.
    if (!sawFreeUsed)
        s.freeDragonsUsed = std::min(cfg::kFreeDragonPicks,
                                     static_cast<int>(s.stable.size() + s.graves.size()));
    out = s;
    return true;
}

bool saveToFile(const SaveData& s, const std::filesystem::path& path) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << serializeSave(s);
        f.flush();
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // 일부 환경에서는 대상 파일이 있으면 rename이 실패한다.
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

bool loadFromFile(const std::filesystem::path& path, SaveData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return deserializeSave(ss.str(), out);
}

int SaveData::stableCapacity() const { return stableCapacityFor(fieldBossCleared); }

const char* potionName(PotionKind k) {
    switch (k) {
        case PotionKind::Small: return "물약(소)";
        case PotionKind::Large: return "물약(대)";
        case PotionKind::Fury: return "분노의 물약";
        case PotionKind::Guard: return "수호의 물약";
        case PotionKind::Swift: return "신속의 물약";
    }
    return "";
}

}  // namespace dt
