#include "core/SaveData.h"

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
    o << "ui.menuOpen=" << (s.menuOpen ? 1 : 0) << "\n";
    o << "bestFloor=" << s.bestFloor << "\n";
    o << "towerClears=" << s.towerClears << "\n";
    o << "dragon.present=" << (s.hasDragon ? 1 : 0) << "\n";
    if (s.hasDragon) {
        const DragonRecord& d = s.dragon;
        o << "dragon.id=" << d.id << "\n";
        o << "dragon.species=" << speciesDef(d.species).key << "\n";
        o << "dragon.alive=" << (d.alive ? 1 : 0) << "\n";
        o << "dragon.level=" << d.level << "\n";
        o << "dragon.exp=" << d.exp << "\n";
        o << "dragon.autoExp=" << d.autoExp << "\n";
        o << "dragon.hp=" << d.hp << "\n";
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
        else if (key == "ui.menuOpen") s.menuOpen = toInt(val, 0) != 0;
        else if (key == "huntMode") {
            int m = toInt(val, 1);
            s.huntMode = (m >= 0 && m <= 2) ? static_cast<CombatMode>(m) : CombatMode::SkillManual;
        }
        else if (key == "bestFloor") s.bestFloor = std::max(0, toInt(val, 0));
        else if (key == "towerClears") s.towerClears = std::max(0, toInt(val, 0));
        else if (key == "dragon.present") dragonPresent = toInt(val, 0) != 0;
        else if (key == "dragon.id") s.dragon.id = toInt(val, 0);
        // 폴더가 사라진 종이라도 다른 종으로 바뀌지 않도록 자리 표시 종을 등록한다.
        else if (key == "dragon.species") { if (!val.empty()) s.dragon.species = ensureSpecies(val); }
        else if (key == "dragon.alive") s.dragon.alive = toInt(val, 0) != 0;
        else if (key == "dragon.level") s.dragon.level = std::max(1, toInt(val, 1));
        else if (key == "dragon.exp") s.dragon.exp = std::max(0, toInt(val, 0));
        else if (key == "dragon.autoExp") s.dragon.autoExp = std::max(0, toInt(val, 0));
        else if (key == "dragon.hp") s.dragon.hp = toFloat(val, -1.f);
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
    // 죽은 개체는 현재 개체로 불러오지 않는다(재실행으로 부활 방지).
    s.hasDragon = dragonPresent && s.dragon.alive;
    if (s.hasDragon && s.dragon.id >= s.nextDragonId) s.nextDragonId = s.dragon.id + 1;
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

}  // namespace dt
