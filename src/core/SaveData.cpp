#include "core/SaveData.h"

#include <algorithm>
#include <fstream>
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
    for (int i = 0; i < kSpeciesCount; ++i) {
        if (!s.speciesSeen[i]) continue;
        if (!first) o << ",";
        o << speciesDef(static_cast<Species>(i)).key;
        first = false;
    }
    o << "\n";
    o << "codex.imp=" << (s.impSeen ? 1 : 0) << "\n";
    o << "codex.guardian=" << (s.guardianSeen ? 1 : 0) << "\n";
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
        else if (key == "huntMode") {
            int m = toInt(val, 1);
            s.huntMode = (m >= 0 && m <= 2) ? static_cast<CombatMode>(m) : CombatMode::SkillManual;
        }
        else if (key == "bestFloor") s.bestFloor = std::max(0, toInt(val, 0));
        else if (key == "towerClears") s.towerClears = std::max(0, toInt(val, 0));
        else if (key == "dragon.present") dragonPresent = toInt(val, 0) != 0;
        else if (key == "dragon.id") s.dragon.id = toInt(val, 0);
        else if (key == "dragon.species") { Species sp; if (speciesFromKey(val, sp)) s.dragon.species = sp; }
        else if (key == "dragon.alive") s.dragon.alive = toInt(val, 0) != 0;
        else if (key == "dragon.level") s.dragon.level = std::max(1, toInt(val, 1));
        else if (key == "dragon.exp") s.dragon.exp = std::max(0, toInt(val, 0));
        else if (key == "dragon.autoExp") s.dragon.autoExp = std::max(0, toInt(val, 0));
        else if (key == "dragon.hp") s.dragon.hp = toFloat(val, -1.f);
        else if (key == "codex.species") {
            for (const std::string& k : split(val, ',')) {
                Species sp;
                if (speciesFromKey(k, sp)) s.speciesSeen[static_cast<int>(sp)] = true;
            }
        }
        else if (key == "codex.imp") s.impSeen = toInt(val, 0) != 0;
        else if (key == "codex.guardian") s.guardianSeen = toInt(val, 0) != 0;
        else if (key == "grave") {
            auto parts = split(val, '|');
            if (parts.size() >= 4) {
                GraveEntry g;
                g.id = toInt(parts[0], 0);
                speciesFromKey(parts[1], g.species);
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
