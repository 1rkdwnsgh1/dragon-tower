// 월드 그리기: 드래곤·적·효과(Fx).
#include "RendererInternal.h"

namespace dt::win {

void Renderer::drawDragon(Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                          float height, bool preview) {
    if (!preview) markRegion(RectF(x - height * 0.5f, y - height * 0.5f, height, height), 4.f);
    const SpeciesDef& sp = speciesDef(d.species);
    bool flip = sp.flipAllowed && d.facing.x < 0.f;
    float alpha = (!preview && d.invuln > 0.f) ? 0.55f : 1.f;
    // v1.5: 맞았을 때 번쩍임을 낮췄다(0.6 -> 0.25). 전에는 그림이 안 보일 만큼 밝았다.
    float flash = (!preview && d.hitFlash > 0.f) ? 0.25f : 0.f;

    // 시트 애니메이션이 있으면 그것으로 그린다(v0.2). 미리보기는 항상 대기 동작.
    AnimState state = preview ? AnimState::Idle : d.anim;
    Sheet sh = sprites.sheet(sp.key, state);
    // v1.4: 스킬 전용 본체 모션이 재생 중이고 그 시트가 있으면 공용 attack 대신 그것을 쓴다.
    // 시트가 없으면 아래에서 공용 attack(sh)으로 그려진다 — 데이터만 있고 그림이 없어도 괜찮다.
    const AnimDef* skillAnim = nullptr;
    if (!preview && d.skillMotion >= 0 && d.skillMotion < static_cast<int>(sp.skills.size())) {
        const SkillDef& sk = sp.skills[static_cast<size_t>(d.skillMotion)];
        Sheet ms = sprites.skillSheet(sp.key, sk.motion);
        if (ms.bmp) {
            sh = ms;
            skillAnim = &sk.motionAnim;
        }
    }
    bool animated = sh.bmp != nullptr && sh.frames > 1;

    // 시트가 여러 칸이면 그림 자체가 움직이므로 위아래 흔들기를 하지 않는다.
    float bob = animated ? 0.f : std::sin(time * (d.moving ? 9.f : 3.f)) * (d.moving ? 4.f : 2.5f);
    float cy = y + bob;

    // 그림자. 날고 있으면 작고 옅게 그려 공중에 떠 있는 것을 알린다.
    {
        float shrink = (!preview && d.flying) ? 0.6f : 1.f;
        BYTE a = static_cast<BYTE>(70 * alpha * (shrink < 1.f ? 0.55f : 1.f));
        float w = height * 0.64f * shrink;
        SolidBrush sh2(Color(a, 0, 0, 0));
        g.FillEllipse(&sh2, x - w * 0.5f, y + height * 0.38f, w, height * 0.14f * shrink);
    }

    if (sh.bmp) {
        const AnimDef& ad = skillAnim ? *skillAnim : sp.anims[static_cast<int>(state)];
        float t = preview ? time : d.animTime;
        // render_scale 로 이 상태만 크게/작게 그린다. 칸 안의 바닥 기준선(240/256)이
        // 제자리에 남도록 그만큼 위로 올려 그려, 발 위치가 흔들리지 않게 한다.
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float drawH = height * k;
        float cyAdj = cy - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        drawSheetFrame(g, sh, animFrame(t, ad.fps, sh.frames, ad.loop), x, cyAdj, drawH, flip, flash, alpha);
        return;
    }

    Bitmap* bmp = sprites.get(std::string("dragon_") + sp.key);
    if (bmp) {
        drawSprite(g, bmp, x, cy, height, flip, flash, alpha);
        return;
    }

    // 임시 도형 드래곤
    float s = flip ? -1.f : 1.f;
    float h = height;
    uint32_t body = flash > 0.f ? lighten(sp.color, 0.6f) : sp.color;
    Color bodyC = col(body, alpha);
    Color wingC = col(lighten(body, 0.35f), alpha * 0.95f);
    Color darkC = col(darken(body, 0.35f), alpha);
    float flap = std::sin(time * 8.f) * h * 0.08f;

    PointF tail[3] = {PointF(x - s * h * 0.30f, cy + h * 0.05f), PointF(x - s * h * 0.62f, cy - h * 0.05f),
                      PointF(x - s * h * 0.28f, cy + h * 0.20f)};
    SolidBrush darkB(darkC);
    g.FillPolygon(&darkB, tail, 3);

    PointF wingBack[3] = {PointF(x - s * h * 0.02f, cy - h * 0.06f), PointF(x - s * h * 0.30f, cy - h * 0.46f + flap),
                          PointF(x - s * h * 0.20f, cy + h * 0.04f)};
    SolidBrush wingB(wingC);
    g.FillPolygon(&wingB, wingBack, 3);

    SolidBrush bodyB(bodyC);
    g.FillEllipse(&bodyB, x - h * 0.36f, cy - h * 0.14f, h * 0.72f, h * 0.46f);
    // 다리
    g.FillEllipse(&darkB, x - h * 0.22f, cy + h * 0.22f, h * 0.14f, h * 0.16f);
    g.FillEllipse(&darkB, x + h * 0.08f, cy + h * 0.22f, h * 0.14f, h * 0.16f);
    // 목과 머리
    PointF neck[4] = {PointF(x + s * h * 0.12f, cy - h * 0.04f), PointF(x + s * h * 0.30f, cy - h * 0.26f),
                      PointF(x + s * h * 0.40f, cy - h * 0.18f), PointF(x + s * h * 0.26f, cy + h * 0.06f)};
    g.FillPolygon(&bodyB, neck, 4);
    float hx = x + s * h * 0.40f;
    float hy = cy - h * 0.26f;
    g.FillEllipse(&bodyB, hx - h * 0.15f, hy - h * 0.12f, h * 0.30f, h * 0.22f);
    PointF horn[3] = {PointF(hx - s * h * 0.06f, hy - h * 0.08f), PointF(hx - s * h * 0.16f, hy - h * 0.26f),
                      PointF(hx + s * h * 0.02f, hy - h * 0.10f)};
    SolidBrush hornB(col(0xFFF4E3C0, alpha));
    g.FillPolygon(&hornB, horn, 3);
    fillCircle(g, hx + s * h * 0.05f, hy - h * 0.02f, h * 0.045f, col(0xFFFFFFFF, alpha));
    fillCircle(g, hx + s * h * 0.065f, hy - h * 0.02f, h * 0.022f, col(0xFF101018, alpha));
    // 앞쪽 날개
    PointF wingFront[3] = {PointF(x + s * h * 0.02f, cy - h * 0.04f), PointF(x - s * h * 0.16f, cy - h * 0.52f - flap),
                           PointF(x - s * h * 0.10f, cy + h * 0.06f)};
    g.FillPolygon(&wingB, wingFront, 3);
    Pen outline(col(0xFF000000, 0.35f * alpha), 1.5f);
    g.DrawEllipse(&outline, x - h * 0.36f, cy - h * 0.14f, h * 0.72f, h * 0.46f);
}

void Renderer::drawEnemy(Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale) {
    const EnemyDef& edef = enemyDef(e.kind);
    // v1.5: 적 그림은 왼쪽을 보고 그려져 있다. 드래곤(오른쪽 기준)과 반대라 전에는 거꾸로 걸었다.
    bool flip = edef.artFacesRight ? (e.facing < 0.f) : (e.facing > 0.f);
    // v1.5: 맞았을 때 번쩍임이 너무 밝다는 이야기가 있어 낮췄다(0.7 -> 0.3).
    float flash = e.hitFlash > 0.f ? 0.3f : 0.f;
    float bob = std::sin(time * 6.f + e.id) * 2.f;
    float x = e.pos.x;
    float y = e.pos.y + bob;
    const EnemyDef& ed = edef;
    bool isBoss = ed.role == EnemyRole::Boss;
    float height = e.radius * 2.6f * charScale;

    SolidBrush sh(Color(70, 0, 0, 0));
    g.FillEllipse(&sh, x - e.radius, e.pos.y + e.radius * 0.9f, e.radius * 2.f, e.radius * 0.5f);

    if (isBoss && e.phase2) {
        float pulse = 0.5f + 0.5f * std::sin(time * 6.f);
        fillCircle(g, x, y, e.radius * 1.45f, Color(static_cast<BYTE>(40 + 40 * pulse), 255, 60, 40));
    }
    if (e.castTimer > 0.f) {
        strokeCircle(g, x, y, e.radius * 1.25f, Color(200, 255, 90, 60), 2.f);
    }

    bool drawnBySheet = false;
    // v0.4: 적도 시트 애니메이션을 쓴다. 상태는 움직임·예고·피격으로 정한다.
    AnimState st = AnimState::Idle;
    if (e.hitFlash > 0.f) st = AnimState::Hit;
    else if (e.castTimer > 0.f) st = AnimState::Attack;
    else if (e.stun <= 0.f) st = AnimState::Move;
    Sheet sh2 = sprites.sheet("enemy:" + ed.key, st);
    if (sh2.bmp) {
        const AnimDef& ad = ed.anims[static_cast<int>(st)];
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float cyAdj = y - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        drawSheetFrame(g, sh2, animFrame(time, ad.fps, sh2.frames, ad.loop), x, cyAdj, height * k, flip, flash, 1.f);
        drawnBySheet = true;
    }
    Bitmap* bmp = drawnBySheet ? nullptr : sprites.get(isBoss ? "boss_guardian" : "enemy_imp");
    if (drawnBySheet) {
        // 시트로 이미 그렸다
    } else if (bmp) {
        drawSprite(g, bmp, x, y, height, flip, flash, 1.f);
    } else if (!isBoss) {
        float r = e.radius;
        uint32_t c = flash > 0.f ? 0xFFF0E0FF : 0xFF8E5BD8;
        SolidBrush hornB(col(0xFF4A2B78));
        PointF h1[3] = {PointF(x - r * 0.7f, y - r * 0.5f), PointF(x - r * 0.9f, y - r * 1.35f), PointF(x - r * 0.25f, y - r * 0.8f)};
        PointF h2[3] = {PointF(x + r * 0.7f, y - r * 0.5f), PointF(x + r * 0.9f, y - r * 1.35f), PointF(x + r * 0.25f, y - r * 0.8f)};
        g.FillPolygon(&hornB, h1, 3);
        g.FillPolygon(&hornB, h2, 3);
        fillCircle(g, x, y, r, col(c));
        strokeCircle(g, x, y, r, Color(120, 0, 0, 0), 1.5f);
        float s = flip ? -1.f : 1.f;
        fillCircle(g, x + s * r * 0.15f - r * 0.3f, y - r * 0.15f, r * 0.17f, col(0xFFFFE34D));
        fillCircle(g, x + s * r * 0.15f + r * 0.3f, y - r * 0.15f, r * 0.17f, col(0xFFFFE34D));
        Pen mouth(Color(255, 40, 10, 60), 2.f);
        g.DrawArc(&mouth, x - r * 0.35f + s * r * 0.15f, y + r * 0.05f, r * 0.7f, r * 0.45f, 20.f, 140.f);
    } else {
        float r = e.radius;
        uint32_t c = flash > 0.f ? 0xFFFFD0D0 : 0xFF7A1F2B;
        fillRound(g, RectF(x - r, y - r * 0.9f, r * 2.f, r * 1.9f), r * 0.5f, col(c));
        strokeRound(g, RectF(x - r, y - r * 0.9f, r * 2.f, r * 1.9f), r * 0.5f, Color(160, 0, 0, 0), 2.f);
        SolidBrush gold(col(0xFFF2C14E));
        for (int i = 0; i < 5; ++i) {
            float bx = x - r * 0.8f + i * r * 0.4f;
            PointF spike[3] = {PointF(bx - r * 0.14f, y - r * 0.85f), PointF(bx, y - r * 1.35f), PointF(bx + r * 0.14f, y - r * 0.85f)};
            g.FillPolygon(&gold, spike, 3);
        }
        uint32_t eye = e.phase2 ? 0xFFFF4040 : 0xFFFFE070;
        fillCircle(g, x - r * 0.35f, y - r * 0.2f, r * 0.14f, col(eye));
        fillCircle(g, x + r * 0.35f, y - r * 0.2f, r * 0.14f, col(eye));
        SolidBrush armor(col(darken(c, 0.4f)));
        g.FillRectangle(&armor, x - r * 0.6f, y + r * 0.25f, r * 1.2f, r * 0.18f);
    }

    if (e.stun > 0.f) {
        for (int i = 0; i < 3; ++i) {
            float a = time * 4.f + i * 2.094f;
            fillCircle(g, x + std::cos(a) * e.radius * 0.7f, y - e.radius * 1.2f + std::sin(a) * 5.f, 4.f,
                       Color(255, 255, 240, 120));
        }
    }

    // 체력 바(보스는 탑 HUD에 크게 표시)
    if (!isBoss) {
        float w = 56.f;
        bar(g, RectF(x - w * 0.5f, e.pos.y - e.radius - 18.f, w, 7.f), e.hp / e.maxHp, 0xFFFF6B6B);
        text(g, "Lv." + std::to_string(e.level), x, e.pos.y - e.radius - 36.f, 12.f, 0xFFE0D0FF, 1, false, true);
    }
}

void Renderer::drawWorld(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx) {
    const World& w = game.world();
    float time = ctx.time;
    uint32_t spColor = speciesDef(w.dragon().species).color;

    // 지속 피해 장판
    for (const DotZone& z : w.dots()) {
        float life = std::clamp(z.remaining / std::max(0.01f, z.duration), 0.f, 1.f);
        fillCircle(g, z.pos.x, z.pos.y, z.radius, col(spColor, 0.22f * (0.4f + 0.6f * life)));
        GraphicsState st = g.Save();
        g.TranslateTransform(z.pos.x, z.pos.y);
        g.RotateTransform(time * 220.f);
        Pen p(col(lighten(spColor, 0.3f), 0.8f * life + 0.2f), 3.f);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        for (int i = 0; i < 3; ++i) {
            g.DrawArc(&p, -z.radius * 0.8f, -z.radius * 0.8f, z.radius * 1.6f, z.radius * 1.6f, i * 120.f, 70.f);
            g.DrawArc(&p, -z.radius * 0.45f, -z.radius * 0.45f, z.radius * 0.9f, z.radius * 0.9f, i * 120.f + 60.f, 60.f);
        }
        g.Restore(st);
    }

    // 적 공격 예고
    for (const AttackZone& z : w.zones()) {
        float p = std::clamp(z.t / std::max(0.01f, z.windup), 0.f, 1.f);
        BYTE fillA = static_cast<BYTE>(35 + 70 * p);
        if (z.shape == ZoneShape::Circle) {
            fillCircle(g, z.a.x, z.a.y, z.radius, Color(fillA, 255, 60, 40));
            fillCircle(g, z.a.x, z.a.y, z.radius * p, Color(80, 255, 40, 20));
            strokeCircle(g, z.a.x, z.a.y, z.radius, Color(230, 255, 80, 60), 2.f);
        } else {
            Vec2 dir = (z.b - z.a).normalized();
            Vec2 n{-dir.y * z.radius, dir.x * z.radius};
            PointF pts[4] = {PointF(z.a.x + n.x, z.a.y + n.y), PointF(z.b.x + n.x, z.b.y + n.y),
                             PointF(z.b.x - n.x, z.b.y - n.y), PointF(z.a.x - n.x, z.a.y - n.y)};
            SolidBrush b(Color(fillA, 255, 60, 40));
            g.FillPolygon(&b, pts, 4);
            Vec2 mid = z.a + (z.b - z.a) * p;
            PointF inner[4] = {PointF(z.a.x + n.x, z.a.y + n.y), PointF(mid.x + n.x, mid.y + n.y),
                               PointF(mid.x - n.x, mid.y - n.y), PointF(z.a.x - n.x, z.a.y - n.y)};
            SolidBrush ib(Color(80, 255, 40, 20));
            g.FillPolygon(&ib, inner, 4);
            Pen pen(Color(230, 255, 80, 60), 2.f);
            g.DrawPolygon(&pen, pts, 4);
        }
    }

    // v0.8: 캐릭터 보기 크기. 그림만 커지고 충돌·사거리 판정은 그대로다.
    const float cs = game.charScale();
    for (const Enemy& e : w.enemies()) drawEnemy(g, e, sprites, time, cs);

    const DragonActor& d = w.dragon();
    if (d.alive && game.hasDragon()) {
        if (d.shieldTime > 0.f) {
            float pulse = 0.5f + 0.5f * std::sin(time * 5.f);
            fillCircle(g, d.pos.x, d.pos.y, 58.f, Color(static_cast<BYTE>(40 + 30 * pulse), 140, 200, 255));
            strokeCircle(g, d.pos.x, d.pos.y, 58.f, Color(220, 160, 215, 255), 2.5f);
        }
        if (d.hasteTime > 0.f) {
            Pen streak(col(spColor, 0.6f), 2.f);
            for (int i = 0; i < 3; ++i) {
                float yy = d.pos.y - 14.f + i * 14.f;
                float sx = d.facing.x >= 0.f ? -1.f : 1.f;
                g.DrawLine(&streak, d.pos.x + sx * 40.f, yy, d.pos.x + sx * (70.f + 10.f * std::sin(time * 20.f + i)), yy);
            }
        }
        drawDragon(g, d, sprites, time, d.pos.x, d.pos.y, cfg::kDragonRadius * 3.f * cs, false);
        float bw = 74.f;
        bar(g, RectF(d.pos.x - bw * 0.5f, d.pos.y - cfg::kDragonRadius * 1.5f * cs - 22.f, bw, 8.f),
            d.hp / d.stats.maxHp, kHpC);
        text(g, "Lv." + std::to_string(game.dragonRecord().level), d.pos.x,
             d.pos.y - cfg::kDragonRadius * 1.5f * cs - 42.f,
             13.f, 0xFFFFF3C4, 1, true, true);
    }

    // 투사체
    for (const Projectile& p : w.projectiles()) {
        float r = p.big ? 11.f : 6.f;
        fillCircle(g, p.pos.x, p.pos.y, r * 2.f, col(spColor, 0.25f));
        fillCircle(g, p.pos.x, p.pos.y, r, col(lighten(spColor, 0.3f)));
        fillCircle(g, p.pos.x, p.pos.y, r * 0.45f, Color(255, 255, 255, 255));
    }

    drawFx(g, w);

    for (const FloatText& t : w.texts()) {
        float a = 1.f - std::clamp((t.t - t.duration * 0.6f) / (t.duration * 0.4f), 0.f, 1.f);
        uint32_t c = (t.color & 0x00FFFFFF) | (static_cast<uint32_t>(255 * a) << 24);
        text(g, t.text, t.pos.x, t.pos.y, t.size, c, 1, true, true);
    }
}

void Renderer::drawFx(Graphics& g, const World& w) {
    for (const Fx& f : w.fx()) {
        float p = std::clamp(f.t / std::max(0.01f, f.duration), 0.f, 1.f);
        float fade = 1.f - p;
        switch (f.type) {
            case FxType::Ring: {
                float r = f.radius * (0.6f + 0.4f * p);
                fillCircle(g, f.a.x, f.a.y, r, col(f.color, 0.25f * fade));
                strokeCircle(g, f.a.x, f.a.y, r, col(f.color, fade), 4.f * fade + 1.f);
                break;
            }
            case FxType::LineBlast: {
                Pen pen(col(f.color, 0.7f * fade), f.radius * 2.f * (0.7f + 0.3f * fade));
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, f.a.x, f.a.y, f.b.x, f.b.y);
                Pen core(Color(static_cast<BYTE>(200 * fade), 255, 255, 255), f.radius * 0.5f);
                g.DrawLine(&core, f.a.x, f.a.y, f.b.x, f.b.y);
                break;
            }
            case FxType::Dash: {
                Pen pen(col(f.color, 0.5f * fade), f.radius * 1.2f * fade + 2.f);
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, f.a.x, f.a.y, f.b.x, f.b.y);
                break;
            }
            case FxType::Heal: {
                for (int i = 0; i < 4; ++i) {
                    float ox = std::cos(i * 1.57f + 0.4f) * f.radius * 0.6f;
                    float oy = -p * 50.f - i * 6.f;
                    SolidBrush b(col(f.color, fade));
                    g.FillRectangle(&b, f.a.x + ox - 2.f, f.a.y + oy - 8.f, 4.f, 16.f);
                    g.FillRectangle(&b, f.a.x + ox - 8.f, f.a.y + oy - 2.f, 16.f, 4.f);
                }
                strokeCircle(g, f.a.x, f.a.y, f.radius * (0.7f + 0.3f * p), col(f.color, 0.6f * fade), 3.f);
                break;
            }
            case FxType::Dodge: {
                Pen pen(col(f.color, fade), 3.f);
                float r = f.radius * (0.8f + 0.5f * p);
                g.DrawArc(&pen, f.a.x - r, f.a.y - r, r * 2.f, r * 2.f, 200.f, 140.f);
                g.DrawArc(&pen, f.a.x - r * 0.8f, f.a.y - r * 0.8f, r * 1.6f, r * 1.6f, 20.f, 140.f);
                break;
            }
            case FxType::Hit: {
                Pen pen(col(f.color, fade), 3.f);
                for (int i = 0; i < 6; ++i) {
                    float a = i * 1.047f + 0.3f;
                    float r1 = f.radius * 0.3f + f.radius * 0.4f * p;
                    float r2 = r1 + f.radius * 0.35f;
                    g.DrawLine(&pen, f.a.x + std::cos(a) * r1, f.a.y + std::sin(a) * r1, f.a.x + std::cos(a) * r2,
                               f.a.y + std::sin(a) * r2);
                }
                break;
            }
            case FxType::Poof: {
                fillCircle(g, f.a.x, f.a.y, f.radius * (0.5f + 0.6f * p), col(f.color, 0.35f * fade));
                break;
            }
            case FxType::Death: {
                fillCircle(g, f.a.x, f.a.y, f.radius * (0.4f + 0.8f * p), col(f.color, 0.4f * fade));
                strokeCircle(g, f.a.x, f.a.y, f.radius * (0.4f + 0.8f * p), col(0xFFFFFFFF, fade), 2.f);
                break;
            }
            case FxType::LevelUp: {
                for (int i = 0; i < 3; ++i) {
                    float q = std::clamp(p * 1.3f - i * 0.15f, 0.f, 1.f);
                    strokeCircle(g, f.a.x, f.a.y, f.radius * (0.3f + q), col(f.color, (1.f - q)), 3.f);
                }
                break;
            }
            case FxType::Shield: {
                strokeCircle(g, f.a.x, f.a.y, f.radius * (1.4f - 0.4f * p), col(f.color, fade), 4.f);
                break;
            }
        }
    }
}

}  // namespace dt::win
