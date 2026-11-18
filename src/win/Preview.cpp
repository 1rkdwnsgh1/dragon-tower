#include "Platform.h"
#include "Renderer.h"
#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"

#include <shellapi.h>
#include <shobjidl.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace dt::win {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace Gdiplus;

enum Control {
    Open = 100, Reload, Motion, Play, Restart, Previous, Next, Loop, Speed,
    Layer, Flip, Zoom, Background, Ground, Info
};

constexpr float kWorldWidth = 1000.f;
constexpr float kWorldHeight = 500.f;
constexpr float kFloorY = 330.f;
constexpr float kStep = 1.f / 120.f;
constexpr int kToolbarHeight = 80;
constexpr int kInfoHeight = 112;

class PreviewWindow {
public:
    explicit PreviewWindow(HINSTANCE instance) : instance_(instance) {}

    int run(const fs::path& initial, int show, const fs::path& jobs = {});

private:
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT message, WPARAM wp, LPARAM lp);
    void createControls();
    void chooseFolder();
    bool load(const fs::path& path);
    void reset();
    void seek(float time);
    void tick();
    void command(int id, int notification);
    void paint();
    int capture(const fs::path& initial, const fs::path& jobs);
    bool saveCapture(const fs::path& file, const Enemy* enemy, AnimState state, float time, float scale, int layer);
    void updateInfo();
    void setPlaying(bool playing);
    void select(int id, int index) const { SendMessageW(item(id), CB_SETCURSEL, index, 0); }
    int selected(int id) const { return static_cast<int>(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
    bool checked(int id) const { return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED; }
    HWND item(int id) const { return GetDlgItem(hwnd_, id); }

    const SkillDef* skill() const {
        const int index = selected(Motion) - kAnimStateCount;
        const auto& skills = speciesDef(species_).skills;
        return index >= 0 && static_cast<size_t>(index) < skills.size() ? &skills[index] : nullptr;
    }

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    fs::path folder_;
    Species species_{};
    Sprites sprites_;
    Renderer renderer_;
    World world_;
    std::vector<std::string> warnings_;
    std::wstring error_;
    std::wstring details_;
    std::unique_ptr<Bitmap> buffer_;
    Vec2 target_;
    bool playing_ = false;
    float time_ = 0.f;
    float duration_ = 1.f;
    float stepFps_ = 8.f;
    float captureScale_ = 1.f;
    Clock::time_point lastTick_{};
};

LRESULT CALLBACK PreviewWindow::dispatch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* preview = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        preview = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        preview->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(preview));
    }
    return preview ? preview->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

int PreviewWindow::run(const fs::path& initial, int show, const fs::path& jobs) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = dispatch;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"DragonTowerAnimationPreview";
    if (!RegisterClassW(&wc)) return 1;
    if (!CreateWindowW(wc.lpszClassName, L"드래곤 타워 모션 미리보기", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                       CW_USEDEFAULT, CW_USEDEFAULT, 940, 685, nullptr, nullptr, instance_, this)) return 1;

    if (!jobs.empty()) return capture(initial, jobs);

    std::error_code ec;
    const fs::path bundled = exeDirectory() / "assets" / "dragons" / "revenant";
    if (!initial.empty()) load(initial);
    else if (fs::is_regular_file(bundled / "dragon.txt", ec)) load(bundled);
    else updateInfo();

    ShowWindow(hwnd_, show);
    ACCEL key{FVIRTKEY, VK_F5, Reload};
    HACCEL shortcuts = CreateAcceleratorTableW(&key, 1);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (shortcuts && TranslateAcceleratorW(hwnd_, shortcuts, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (shortcuts) DestroyAcceleratorTable(shortcuts);
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}

void PreviewWindow::createControls() {
    auto create = [this](const wchar_t* cls, const wchar_t* label, DWORD style, int id,
                         int x, int y, int w, int h, DWORD ex = 0) {
        HWND control = CreateWindowExW(ex, cls, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                       x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    };
    auto button = [&](int id, const wchar_t* label, int x, int y, int w, DWORD style = BS_PUSHBUTTON) {
        create(L"BUTTON", label, style, id, x, y, w, 28);
    };
    auto combo = [&](int id, int x, int y, int w, std::initializer_list<const wchar_t*> labels, int initial = 0) {
        HWND control = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, id, x, y, w, 310);
        for (const wchar_t* label : labels) SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        select(id, initial);
    };

    button(Open, L"캐릭터 폴더 열기", 10, 10, 122);
    button(Reload, L"새로고침 (F5)", 140, 10, 115);
    combo(Motion, 265, 11, 296, {});
    button(Play, L"재생", 571, 10, 92);
    button(Restart, L"처음부터", 673, 10, 86);
    button(Loop, L"반복", 770, 10, 67, BS_AUTOCHECKBOX);
    button(Previous, L"◀ 1프레임", 10, 45, 86);
    button(Next, L"1프레임 ▶", 102, 45, 86);
    combo(Speed, 198, 46, 79, {L"0.25×", L"0.5×", L"1×", L"2×"}, 2);
    combo(Layer, 287, 46, 133, {L"본체 + 이펙트", L"본체만", L"이펙트만"});
    button(Flip, L"왼쪽 방향", 431, 45, 89, BS_AUTOCHECKBOX);
    combo(Zoom, 526, 46, 88, {L"50%", L"100%", L"150%", L"200%", L"300%"}, 1);
    combo(Background, 624, 46, 103, {L"어두운 배경", L"밝은 배경", L"체크 배경"});
    button(Ground, L"발 기준선", 738, 45, 95, BS_AUTOCHECKBOX);
    create(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, Info,
           10, 0, 450, kInfoHeight - 12, WS_EX_CLIENTEDGE);
    CheckDlgButton(hwnd_, Loop, BST_CHECKED);
    CheckDlgButton(hwnd_, Ground, BST_CHECKED);
    setPlaying(false);
    DragAcceptFiles(hwnd_, TRUE);
}

void PreviewWindow::chooseFolder() {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        MessageBoxW(hwnd_, L"폴더 선택 창을 열 수 없습니다. 탐색기에서 폴더를 끌어 놓을 수 있습니다.",
                    L"캐릭터 폴더 열기", MB_OK | MB_ICONWARNING);
        return;
    }
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"dragon.txt가 들어 있는 캐릭터 폴더 선택");
    if (SUCCEEDED(dialog->Show(hwnd_))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                load(path);
                CoTaskMemFree(path);
            }
            result->Release();
        }
    }
    dialog->Release();
}

bool PreviewWindow::load(const fs::path& path) {
    std::error_code ec;
    fs::path dir = fs::absolute(path, ec).lexically_normal();
    if (ec) dir = path;
    if (dir.filename() == L"dragon.txt") dir = dir.parent_path();
    if (dir.filename().empty() && dir.has_relative_path()) dir = dir.parent_path();
    std::ifstream file(dir / "dragon.txt", std::ios::binary);
    if (!file) {
        error_ = L"dragon.txt를 열 수 없습니다. " + dir.wstring();
        updateInfo();
        return false;
    }
    const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        error_ = L"dragon.txt 읽기 실패. " + dir.wstring();
        updateInfo();
        return false;
    }

    const bool sameFolder = dir == folder_;
    const int selection = sameFolder ? selected(Motion) : 0;
    const bool resume = !sameFolder || playing_;
    const auto name = dir.filename().u8string();
    auto parsed = parseDragonText(contents, name.empty() ? "preview" : name, nullptr);
    sprites_.loadPreviewDragon(dir, parsed.def);
    species_ = registerOrUpdateSpecies(parsed.def);
    warnings_ = std::move(parsed.warnings);
    warnings_.insert(warnings_.end(), sprites_.warnings().begin(), sprites_.warnings().end());
    folder_ = dir;
    error_.clear();
    SendMessageW(item(Motion), CB_RESETCONTENT, 0, 0);

    constexpr const wchar_t* names[] = {L"대기", L"걷기", L"공격", L"피격", L"사망", L"비행", L"휴식"};
    for (int i = 0; i < kAnimStateCount; ++i) {
        const std::wstring label = L"모션 · " + std::wstring(names[i]) + L" (" +
                                   widen(animStateName(static_cast<AnimState>(i))) + L")";
        SendMessageW(item(Motion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    const SpeciesDef& def = speciesDef(species_);
    for (const SkillDef& sk : def.skills) {
        const std::wstring label = L"스킬 · " + widen(sk.name);
        SendMessageW(item(Motion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    const int count = kAnimStateCount + static_cast<int>(def.skills.size());
    select(Motion, selection >= 0 && selection < count ? selection : 0);
    reset();
    setPlaying(resume);
    updateInfo();
    return true;
}

void PreviewWindow::reset() {
    if (folder_.empty()) return;
    time_ = 0.f;
    world_ = World{};
    world_.seed(1);
    world_.setSizeScale(captureScale_);
    world_.setBounds({0.f, 0.f, kWorldWidth, kWorldHeight});
    const bool left = checked(Flip);
    const float direction = left ? -1.f : 1.f;
    const Vec2 origin{left ? 650.f : 350.f, kFloorY - cfg::kDragonRadius};
    const SpeciesDef& def = speciesDef(species_);
    world_.setupDragon(species_, cfg::kMaxLevel, 0.f, origin);
    DragonActor& dragon = world_.dragon();
    dragon.facing = {direction, 0.f};
    dragon.basicCd = 10000.f;
    const SkillDef* sk = skill();
    const auto state = sk ? (skillIsPassive(sk->kind) ? AnimState::Idle : AnimState::Attack)
                          : static_cast<AnimState>(std::max(0, selected(Motion)));
    dragon.anim = state;
    dragon.flying = state == AnimState::Fly;
    dragon.moving = state == AnimState::Move;

    const Sheet custom = sk && !skillIsPassive(sk->kind) ? sprites_.skillSheet(def.key, sk->motion) : Sheet{};
    const Sheet body = custom.bmp ? custom : sprites_.sheet(def.key, state);
    const AnimDef& anim = sk && custom.bmp ? sk->motionAnim : def.anims[static_cast<int>(state)];
    stepFps_ = std::max(1.f, anim.fps);
    duration_ = std::max(1.f, body.frames / stepFps_);

    if (!sk) return;

    const float distance = std::clamp(sk->range * 0.7f, 0.f, 280.f);
    target_ = origin + Vec2{direction * distance, 0.f};
    // 고정 표적에 생존과 무행동만 설정하고 스킬 발동은 게임의 수동 입력 경로를 쓴다.
    Enemy& target = world_.spawnEnemy(defaultMelee(), 1, target_);
    target.radius = 8.f;
    target.hp = target.maxHp = 1e20f;
    target.stun = 10000.f;
    if (sk->kind == SkillKind::PassiveOnKill) {
        world_.addSkillFx(*sk, FxType::Heal, target_, origin, 44.f, 0.5f, def.color);
    } else if (!skillIsPassive(sk->kind)) {
        dragon.anim = AnimState::Idle;
        world_.setSlots({selected(Motion) - kAnimStateCount, -1, -1, -1});
        PlayerInput input{};
        input.skill[0] = true;
        world_.updateCombat(0.f, CombatMode::Direct, input);
        dragon.invuln = 0.f;
    }

    duration_ = std::max(1.f, dragon.animHold);
    for (const Fx& effect : world_.fx()) duration_ = std::max(duration_, effect.duration);
    for (const DotZone& zone : world_.dots()) duration_ = std::max(duration_, zone.duration);
    for (const Projectile& projectile : world_.projectiles()) {
        const float impact = sk->fxImpact.empty() ? 0.2f :
            (sk->fxWindup > 0.f ? sk->fxWindup : cfg::kFxWindup) +
            (sk->fxHold > 0.f ? sk->fxHold : cfg::kFxHold) +
            (sk->fxTail > 0.f ? sk->fxTail : cfg::kFxTail);
        duration_ = std::max(duration_, distance / projectile.speed + impact);
    }
    duration_ += 0.15f;
    if (selected(Layer) == 2 || !body.bmp || skillIsPassive(sk->kind)) stepFps_ = std::max(1.f, sk->fxAnim.fps);
}

void PreviewWindow::seek(float time) {
    if (folder_.empty()) return;
    if (time < time_) reset();
    time = std::clamp(time, 0.f, duration_);
    if (!skill()) {
        world_.dragon().animTime = time;
    } else {
        for (float remaining = time - time_; remaining > 0.f; remaining -= kStep) {
            const float dt = std::min(kStep, remaining);
            world_.updateFx(dt);
            world_.updateCombat(dt, CombatMode::Direct, {});
        }
    }
    time_ = time;
    updateInfo();
}

void PreviewWindow::setPlaying(bool playing) {
    playing_ = playing && !folder_.empty();
    SetWindowTextW(item(Play), playing_ ? L"일시정지" : L"재생");
    lastTick_ = Clock::now();
}

void PreviewWindow::tick() {
    if (!playing_) return;
    const auto now = Clock::now();
    constexpr float speed[] = {0.25f, 0.5f, 1.f, 2.f};
    const float dt = std::min(0.1f, std::chrono::duration<float>(now - lastTick_).count());
    lastTick_ = now;
    float next = time_ + dt * speed[std::clamp(selected(Speed), 0, 3)];
    if (next >= duration_) {
        if (checked(Loop)) {
            next = std::fmod(next, duration_);
            reset();
        } else {
            next = duration_;
            setPlaying(false);
        }
    }
    seek(next);
}

void PreviewWindow::command(int id, int notification) {
    if (id == Open && notification == BN_CLICKED) {
        chooseFolder();
        return;
    }
    if (folder_.empty()) return;
    if (id == Reload && (notification == BN_CLICKED || notification == 1)) load(folder_);
    if (id == Motion && notification == CBN_SELCHANGE) reset();
    if (id == Layer && notification == CBN_SELCHANGE) {
        const float time = time_;
        reset();
        seek(time);
        return;
    }
    if (id == Flip && notification == BN_CLICKED) {
        const float time = time_;
        reset();
        seek(time);
        return;
    }
    if (id == Restart && notification == BN_CLICKED) {
        reset();
        setPlaying(true);
    }
    if (id == Play && notification == BN_CLICKED) {
        if (time_ >= duration_) reset();
        setPlaying(!playing_);
    }
    if ((id == Previous || id == Next) && notification == BN_CLICKED) {
        setPlaying(false);
        const float current = time_ * stepFps_;
        const float frame = id == Next ? std::floor(current + 0.001f) + 1.f
                                       : std::ceil(current - 0.001f) - 1.f;
        seek(frame <= 0.f ? 0.f : (frame + 0.0001f) / stepFps_);
        return;
    }
    updateInfo();
}

void PreviewWindow::updateInfo() {
    InvalidateRect(hwnd_, nullptr, FALSE);
    std::wostringstream out;
    if (!error_.empty()) out << error_ << L"\r\n";
    if (folder_.empty()) {
        out << L"dragon.txt가 있는 폴더를 선택하거나 이 창에 끌어 놓으세요.";
    } else {
        const SpeciesDef& def = speciesDef(species_);
        const SkillDef* sk = skill();
        const auto state = sk ? (skillIsPassive(sk->kind) ? AnimState::Idle : AnimState::Attack)
                              : static_cast<AnimState>(std::max(0, selected(Motion)));
        AnimState resolved = state;
        const Sheet custom = sk && !skillIsPassive(sk->kind) ? sprites_.skillSheet(def.key, sk->motion) : Sheet{};
        const Sheet body = custom.bmp ? custom : sprites_.sheet(def.key, state, &resolved);
        const AnimDef& anim = sk && custom.bmp ? sk->motionAnim : def.anims[static_cast<int>(state)];
        const auto& dragon = world_.dragon();
        out << widen(def.name) << L"   |   " << std::fixed << std::setprecision(2) << time_ << L" / "
            << duration_ << L"초   |   " << folder_.wstring() << L"\r\n본체  ";
        if (body.bmp) {
            out << (sk && custom.bmp ? L"skill_" + widen(sk->motion) : widen(animStateName(resolved))) << L".png"
                << L"  ·  " << body.frames << L"프레임  ·  " << anim.fps << L" FPS";
            if (dragon.anim == state) {
                out << L"  ·  현재 " << animFrame(dragon.animTime, anim.fps, body.frames, anim.loop) + 1
                    << L"/" << body.frames;
            } else {
                out << L"  ·  시전 모션 종료";
            }
        } else {
            out << (sprites_.get("dragon_" + def.key) ? L"dragon.png (정지 이미지)" : L"기본 도형");
        }
        if (resolved != state || (sk && !skillIsPassive(sk->kind) && !sk->motion.empty() && !custom.bmp))
            out << L"  ·  대체 표시";
        if (sk && !skillIsPassive(sk->kind) && body.bmp) {
            const float hold = sk->motion.empty() ? cfg::kAnimAttackHold
                                                  : std::max(cfg::kSkillMotionHold, cfg::kAnimAttackHold);
            if (anim.fps > 0.f && (body.frames - 1) / anim.fps >= hold)
                out << L"\r\n주의  모션 유지 시간 " << hold << L"초 안에 마지막 프레임까지 재생되지 않습니다.";
        }
        if (sk) {
            for (const std::string* key : {&sk->fx, &sk->fxImpact}) {
                if (key->empty()) continue;
                const Sheet effect = sprites_.fxSheet(def.key, *key);
                out << L"\r\n이펙트  fx_" << widen(*key) << L".png";
                if (effect.bmp) out << L"  ·  " << effect.frames << L"프레임  ·  " << sk->fxAnim.fps << L" FPS";
                else out << L"  ·  시트를 읽을 수 없어 기본 효과 사용";
            }
            if (sk->fx.empty() && !skillIsPassive(sk->kind)) out << L"\r\n이펙트  기본 도형";
            if (sk->kind == SkillKind::PassiveOnKill) out << L"\r\n처치가 일어난 위치에 회복 효과를 바로 표시합니다.";
            if (sk->kind == SkillKind::PassiveStat) out << L"\r\n상시 적용 스킬에는 발동 연출이 없습니다.";
            if (!skillIsPassive(sk->kind) && time_ == 0.f && dragon.anim != AnimState::Attack)
                out << L"\r\n스킬이 발동하지 않았습니다. 사거리와 해제 레벨을 확인하세요.";
        }
        for (const auto& warning : warnings_) out << L"\r\n주의  " << widen(warning);
    }
    const std::wstring next = out.str();
    if (next == details_) return;
    const LRESULT line = SendMessageW(item(Info), EM_GETFIRSTVISIBLELINE, 0, 0);
    SetWindowTextW(item(Info), next.c_str());
    SendMessageW(item(Info), EM_LINESCROLL, 0, line);
    details_ = next;
}

bool PreviewWindow::saveCapture(const fs::path& file, const Enemy* enemy, AnimState state, float time, float scale, int layer) {
    constexpr int width = 900;
    constexpr int height = 500;
    Bitmap bitmap(width, height, PixelFormat32bppPARGB);
    {
        Graphics canvas(&bitmap);
        canvas.Clear(Color(26, 30, 39));
        canvas.SetSmoothingMode(SmoothingModeAntiAlias);
        canvas.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        // At large character sizes, effects can extend well beyond the body and below the floor.
        // Keep the framing fixed across directions, layers and frames at each size.
        const float extraRoom = std::clamp((scale - 1.f) / 2.f, 0.f, 1.f);
        const float camera = (0.85f - 0.20f * extraRoom) / std::max(1.f, scale * 0.50f);
        canvas.TranslateTransform(width * 0.5f, height * (0.65f - 0.10f * extraRoom));
        canvas.ScaleTransform(camera, camera);
        canvas.TranslateTransform(-kWorldWidth * 0.5f, -kFloorY);
        if (enemy) renderer_.drawEnemyPreview(canvas, *enemy, sprites_, state, time, scale);
        else renderer_.drawAnimationPreview(canvas, world_, sprites_, time, layer != 2, layer != 1, scale);
    }
    CLSID png{};
    if (FAILED(CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png))) return false;
    return bitmap.Save(file.c_str(), &png, nullptr) == Ok;
}

int PreviewWindow::capture(const fs::path& initial, const fs::path& jobs) {
    std::ifstream requests(jobs);
    if (!requests) return 2;
    Enemy previewEnemy;
    std::error_code ec;
    const bool isEnemy = fs::is_regular_file(initial / "enemy.txt", ec);
    if (isEnemy) {
        std::ifstream source(initial / "enemy.txt", std::ios::binary);
        if (!source) return 2;
        const std::string contents{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
        const std::string key = initial.filename().u8string();
        EnemyKind original{};
        const EnemyDef* base = enemyFromKey(key, original) ? &enemyDef(original) : nullptr;
        auto parsed = parseEnemyText(contents, key, base);
        const auto& def = parsed.def;
        previewEnemy.kind = registerOrUpdateEnemy(def);
        previewEnemy.radius = def.radius;
        previewEnemy.pos = {500.f, kFloorY - def.radius};
        const fs::path art = def.artFrom.empty() ? initial : initial.parent_path() / def.artFrom;
        sprites_.loadPreviewEnemy(art, def);
    } else if (!load(initial)) {
        return 2;
    }

    std::string name;
    int selectedItem;
    float time, scale;
    char direction;
    int layer;
    while (requests >> name >> selectedItem >> time >> direction >> scale >> layer) {
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
                std::string::npos || fs::path(name).extension() != ".png" ||
            (direction != 'L' && direction != 'R') || !std::isfinite(time) || time < 0.f || time > 60.f ||
            !std::isfinite(scale) || scale < cfg::kCharScaleMin || scale > cfg::kCharScaleMax ||
            selectedItem < 0 || layer < 0 || layer > 2) return 2;
        const auto output = jobs.parent_path() / name;
        if (isEnemy) {
            if (selectedItem >= kAnimStateCount) return 2;
            previewEnemy.facing = direction == 'L' ? -1.f : 1.f;
            const auto state = static_cast<AnimState>(selectedItem);
            previewEnemy.hitFlash = state == AnimState::Hit ? 1.f : 0.f;
            previewEnemy.castTimer = state == AnimState::Attack ? 1.f : 0.f;
            previewEnemy.stun = state == AnimState::Idle ? 1.f : 0.f;
            if (!saveCapture(output, &previewEnemy, state, time, scale, layer)) return 3;
        } else {
            if (selectedItem >= SendMessageW(item(Motion), CB_GETCOUNT, 0, 0)) return 2;
            captureScale_ = scale;
            select(Motion, selectedItem);
            CheckDlgButton(hwnd_, Flip, direction == 'L' ? BST_CHECKED : BST_UNCHECKED);
            reset();
            seek(time);
            if (!saveCapture(output, nullptr, AnimState::Idle, time_, scale, layer)) return 3;
        }
    }
    return requests.eof() ? 0 : 2;
}

void PreviewWindow::paint() {
    RECT window{};
    GetClientRect(hwnd_, &window);
    const int width = window.right;
    const int height = window.bottom - kToolbarHeight - kInfoHeight;
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT toolbar{0, 0, width, kToolbarHeight};
    RECT footer{0, window.bottom - kInfoHeight, width, window.bottom};
    FillRect(dc, &toolbar, GetSysColorBrush(COLOR_BTNFACE));
    FillRect(dc, &footer, GetSysColorBrush(COLOR_BTNFACE));

    if (width > 0 && height > 0) {
        if (!buffer_ || buffer_->GetWidth() != static_cast<UINT>(width) ||
            buffer_->GetHeight() != static_cast<UINT>(height))
            buffer_ = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
        {
            Graphics canvas(buffer_.get());
            canvas.SetSmoothingMode(SmoothingModeAntiAlias);
            canvas.SetInterpolationMode(InterpolationModeHighQualityBilinear);
            const int background = selected(Background);
            const bool dark = background == 0;
            canvas.Clear(dark ? Color(26, 30, 39) : Color(229, 232, 237));
            if (background == 2) {
                SolidBrush check(Color(199, 204, 213));
                for (int y = 0; y < height; y += 24)
                    for (int x = (y / 24 % 2) * 24; x < width; x += 48)
                        canvas.FillRectangle(&check, x, y, 24, 24);
            }
            if (!folder_.empty()) {
                constexpr float zoom[] = {0.5f, 1.f, 1.5f, 2.f, 3.f};
                const float scale = std::min(width / kWorldWidth, height / kWorldHeight) *
                                    zoom[std::clamp(selected(Zoom), 0, 4)];
                canvas.TranslateTransform(width * 0.5f, height * 0.5f);
                canvas.ScaleTransform(scale, scale);
                canvas.TranslateTransform(-kWorldWidth * 0.5f, -kWorldHeight * 0.5f);
                if (checked(Ground)) {
                    Pen ground(dark ? Color(100, 149, 183) : Color(85, 122, 152), 1.f / scale);
                    ground.SetDashStyle(DashStyleDash);
                    canvas.DrawLine(&ground, -kWorldWidth, kFloorY, 2.f * kWorldWidth, kFloorY);
                }
                if (skill()) {
                    Pen target(dark ? Color(216, 225, 239) : Color(70, 80, 99), 1.4f / scale);
                    canvas.DrawEllipse(&target, target_.x - 8.f, target_.y - 8.f, 16.f, 16.f);
                    canvas.DrawLine(&target, target_.x - 13.f, target_.y, target_.x + 13.f, target_.y);
                    canvas.DrawLine(&target, target_.x, target_.y - 13.f, target_.x, target_.y + 13.f);
                }
                renderer_.drawAnimationPreview(canvas, world_, sprites_, time_, selected(Layer) != 2,
                                               selected(Layer) != 1);
            }
        }
        Graphics screen(dc);
        screen.DrawImage(buffer_.get(), 0, kToolbarHeight, width, height);
    }
    EndPaint(hwnd_, &ps);
}

LRESULT PreviewWindow::handle(UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE:
            createControls();
            SetTimer(hwnd_, 1, 16, nullptr);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* size = reinterpret_cast<MINMAXINFO*>(lp);
            size->ptMinTrackSize = {860, 440};
            return 0;
        }
        case WM_SIZE:
            MoveWindow(item(Info), 10, HIWORD(lp) - kInfoHeight + 4, std::max(0, LOWORD(lp) - 20),
                       kInfoHeight - 12, TRUE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) != Info) command(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_TIMER:
            tick();
            return 0;
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wp);
            const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(length + 1, L'\0');
            if (DragQueryFileW(drop, 0, path.data(), length + 1)) {
                path.resize(length);
                load(fs::path(path));
            }
            DragFinish(drop);
            return 0;
        }
        case WM_PAINT:
            paint();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, wp, lp);
}
}  // namespace
}  // namespace dt::win

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR gdiplus = 0;
    Gdiplus::GdiplusStartupInput startup;
    int status = 1;
    if (Gdiplus::GdiplusStartup(&gdiplus, &startup, nullptr) == Gdiplus::Ok) {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        const bool capture = args && count == 4 && std::wstring(args[1]) == L"--capture";
        const std::filesystem::path initial = capture ? args[2] : (args && count > 1 ? args[1] : L"");
        const std::filesystem::path jobs = capture ? args[3] : L"";
        if (args) LocalFree(args);
        {
            dt::win::PreviewWindow window(instance);
            status = window.run(initial, show, jobs);
        }
        Gdiplus::GdiplusShutdown(gdiplus);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return status;
}
