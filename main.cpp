// ================================================================
//  QuadroCoop — Local Multiplayer Co-op (Platformer Mode)
//  v2.1: Mobile Macro Fix + Popup Header + toggleDualMode Fix
//
//  New in v2.0:
//    - Section 8:  Trigger cloning — GJBaseGameLayer::triggerObject
//      intercepts movement-type triggers and applies the same delta
//      to P3/P4.  Uses position-delta strategy; guarded by
//      g_triggerCloning to prevent recursion.
//    - Section 9:  Dual portal logic — PlayLayer::bumpPlayer hook.
//      When P3/P4 hit portal ID 99 (dual on) or 101 (dual off),
//      their per-player isDual flag is toggled and visual state
//      is updated via PlayerObject::toggleDualMode.
//    - Section 10: Settings popup (geode::Popup) with Enable P3 /
//      Enable P4 toggles backed by Mod::get() saved values.
//      Injected into PauseLayer::customSetup.
//    - g_extraPlayers[]: global mirror of Fields::extraPlayers,
//      populated in init and cleared in onQuit so that the trigger
//      and portal hooks can access extra players without breaking
//      the Fields encapsulation.
//
//  PC Bindings:
//    P1 -> A (left)  W (jump)  D (right)
//    P2 -> Left (left)  Up (jump)  Right (right)
//    P3 -> J (left)  I (jump)  L (right)     [if enabled]
//    P4 -> F (left)  T (jump)  H (right)     [if enabled]
//
//  Mobile: Screen divided into a 2x2 quadrant grid.
//    Top-Left -> P1  |  Top-Right -> P2
//    Bot-Left -> P3  |  Bot-Right -> P4
//
//  Geode target: 2.2081
// ================================================================

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <array>
#include <map>
#include <set>
#include <unordered_map>

using namespace geode::prelude;


// ================================================================
//  Section 1 — Core Types
// ================================================================

enum class PlayerID : int { P1 = 1, P2 = 2, P3 = 3, P4 = 4 };

enum class PlayerAction { MoveLeft, Jump, MoveRight };

struct PlayerInputEvent {
    PlayerID     player;
    PlayerAction action;
    bool         pressed;
};


// ================================================================
//  Section 2 — PC Key Binding Table
// ================================================================

using KeyBinding = std::pair<PlayerID, PlayerAction>;

static const std::unordered_map<int, KeyBinding> KEY_BINDINGS = {
    { static_cast<int>(enumKeyCodes::KEY_A),     { PlayerID::P1, PlayerAction::MoveLeft  } },
    { static_cast<int>(enumKeyCodes::KEY_W),     { PlayerID::P1, PlayerAction::Jump      } },
    { static_cast<int>(enumKeyCodes::KEY_D),     { PlayerID::P1, PlayerAction::MoveRight } },
    { static_cast<int>(enumKeyCodes::KEY_Left),  { PlayerID::P2, PlayerAction::MoveLeft  } },
    { static_cast<int>(enumKeyCodes::KEY_Up),    { PlayerID::P2, PlayerAction::Jump      } },
    { static_cast<int>(enumKeyCodes::KEY_Right), { PlayerID::P2, PlayerAction::MoveRight } },
    { static_cast<int>(enumKeyCodes::KEY_J),     { PlayerID::P3, PlayerAction::MoveLeft  } },
    { static_cast<int>(enumKeyCodes::KEY_I),     { PlayerID::P3, PlayerAction::Jump      } },
    { static_cast<int>(enumKeyCodes::KEY_L),     { PlayerID::P3, PlayerAction::MoveRight } },
    { static_cast<int>(enumKeyCodes::KEY_F),     { PlayerID::P4, PlayerAction::MoveLeft  } },
    { static_cast<int>(enumKeyCodes::KEY_T),     { PlayerID::P4, PlayerAction::Jump      } },
    { static_cast<int>(enumKeyCodes::KEY_H),     { PlayerID::P4, PlayerAction::MoveRight } },
};


// ================================================================
//  Section 3 — Input State + Logging
// ================================================================

static constexpr const char* playerIDToString(PlayerID id) {
    switch (id) {
        case PlayerID::P1: return "1"; case PlayerID::P2: return "2";
        case PlayerID::P3: return "3"; case PlayerID::P4: return "4";
        default: return "?";
    }
}
static constexpr const char* actionToString(PlayerAction a) {
    switch (a) {
        case PlayerAction::MoveLeft:  return "MoveLeft";
        case PlayerAction::Jump:      return "Jump";
        case PlayerAction::MoveRight: return "MoveRight";
        default: return "Unknown";
    }
}

static std::map<PlayerID, std::set<PlayerAction>> g_heldInputs;

static void dispatchPlayerInput(const PlayerInputEvent& evt) {
    if (evt.pressed) {
        g_heldInputs[evt.player].insert(evt.action);
        log::info("Player {} PRESSED  {} | Held: {}", playerIDToString(evt.player),
            actionToString(evt.action), g_heldInputs[evt.player].size());
    } else {
        g_heldInputs[evt.player].erase(evt.action);
        log::info("Player {} RELEASED {} | Held: {}", playerIDToString(evt.player),
            actionToString(evt.action), g_heldInputs[evt.player].size());
    }
}


// ================================================================
//  Section 4 — PC: CCKeyboardDispatcher Hook
// ================================================================

class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool isArrowKey) {
        const auto it = KEY_BINDINGS.find(static_cast<int>(key));
        if (it != KEY_BINDINGS.end()) {
            const auto& [id, action] = it->second;
            dispatchPlayerInput({ .player = id, .action = action, .pressed = down });
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, isArrowKey);
    }
};


// ================================================================
//  Section 5 — Mobile: Touch Quadrant + Sub-Zone Detection
// ================================================================

static std::map<int, PlayerInputEvent> g_activeTouches;

static PlayerID detectTouchQuadrant(const CCPoint& pos, const CCSize& win) {
    const bool r = pos.x >= win.width * 0.5f, t = pos.y >= win.height * 0.5f;
    if (!r &&  t) return PlayerID::P1;
    if ( r &&  t) return PlayerID::P2;
    if (!r && !t) return PlayerID::P3;
    return PlayerID::P4;
}

static PlayerAction resolveTouchAction(float lx, float ly, float qw, float qh) {
    if (ly >= qh * 0.67f) return PlayerAction::Jump;
    return (lx < qw * 0.5f) ? PlayerAction::MoveLeft : PlayerAction::MoveRight;
}


// ================================================================
//  Section 6 — Physics Helpers
// ================================================================

static PlayerButton actionToButton(PlayerAction a) {
    switch (a) {
        case PlayerAction::MoveLeft:  return PlayerButton::Left;
        case PlayerAction::MoveRight: return PlayerButton::Right;
        default:                      return PlayerButton::Jump;
    }
}

static constexpr int   EXTRA_PLAYER_COUNT = 3;
static constexpr float SPAWN_OFFSET_X     = 40.0f;

// Dual portal object IDs in GD 2.2
// 99  = Dual Mode ON portal
// 101 = Dual Mode OFF portal
// Note: verify against GD 2.2081 object registry if IDs differ in a level.
static constexpr int PORTAL_DUAL_ON  = 99;
static constexpr int PORTAL_DUAL_OFF = 101;

// ── Global extra-player mirror ────────────────────────────────────
// Populated by CoopPlayLayer::init, cleared by onQuit.
// Provides access to extra players from the trigger and portal hooks
// without breaking CoopPlayLayer's Fields encapsulation.
static std::array<PlayerObject*, EXTRA_PLAYER_COUNT> g_extraPlayers = {
    nullptr, nullptr, nullptr
};

// Recursion guard: prevents trigger clone from re-entering itself.
static bool g_triggerCloning = false;

// Convenience: read Geode saved settings with a fallback default.
static bool isP3Enabled() {
    return Mod::get()->getSavedValue<bool>("enable-p3", true);
}
static bool isP4Enabled() {
    return Mod::get()->getSavedValue<bool>("enable-p4", true);
}


// ================================================================
//  Section 7 — CoopPlayLayer: Spawner + Physics + Touch Hooks
// ================================================================

class $modify(CoopPlayLayer, PlayLayer) {

    struct Fields {
        std::array<PlayerObject*, EXTRA_PLAYER_COUNT> extraPlayers = {
            nullptr, nullptr, nullptr
        };
        std::array<std::set<PlayerAction>, EXTRA_PLAYER_COUNT> prevHeld;
        std::set<PlayerAction> prevHeldP1;

        // Per-player dual-mode state.  Index contract: 0=P2, 1=P3, 2=P4.
        std::array<bool, EXTRA_PLAYER_COUNT> isDual = { false, false, false };

        bool ownP2 = false;
    };

    struct PlayerVisuals {
        ccColor3B primary;
        ccColor3B secondary;
        const char* label;
    };

    static constexpr std::array<PlayerVisuals, EXTRA_PLAYER_COUNT> PLAYER_VISUALS = {{
        { {  80, 140, 255 }, {   0,  60, 200 }, "P2" },
        { { 255,  70,  70 }, { 180,   0,   0 }, "P3" },
        { {  60, 210,  80 }, {   0, 140,  20 }, "P4" },
    }};

    // ── Helper: apply colour and label to any PlayerObject ────────
    static void applyVisuals(PlayerObject* player, const PlayerVisuals& vis) {
        player->setColor(vis.primary);
        player->setSecondColor(vis.secondary);
        auto* lbl = CCLabelBMFont::create(vis.label, "bigFont.fnt");
        if (lbl) {
            lbl->setScale(0.4f);
            lbl->setPosition({
                player->getContentSize().width  * 0.5f,
                player->getContentSize().height + 8.0f
            });
            player->addChild(lbl, 10);
        }
    }

    // ── Helper: spawn, colour, label, and retain one extra player ─
    PlayerObject* spawnExtraPlayer(int index, const CCPoint& basePos) {
        PlayerObject* player = PlayerObject::create(1, 1, this, m_objectLayer, false);
        if (!player) {
            log::error("QuadroCoop: PlayerObject::create failed for P{}.", index + 2);
            return nullptr;
        }
        player->setPosition({
            basePos.x + SPAWN_OFFSET_X * static_cast<float>(index + 1),
            basePos.y
        });
        applyVisuals(player, PLAYER_VISUALS[index]);
        m_objectLayer->addChild(player, m_player1->getZOrder());
        player->retain();
        log::info("QuadroCoop: Spawned P{} ({})", index + 2, PLAYER_VISUALS[index].label);
        return player;
    }

    // ── init ──────────────────────────────────────────────────────
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (!level->m_isPlatformer) {
            log::info("QuadroCoop: Classic level — extra players skipped.");
            return true;
        }
        if (!m_player1 || !m_objectLayer) {
            log::error("QuadroCoop: m_player1 or m_objectLayer null — aborting spawn.");
            return true;
        }

        // P1 visual identity
        m_player1->setColor({255, 200, 0});
        m_player1->setSecondColor({200, 130, 0});
        {
            auto* lbl = CCLabelBMFont::create("P1", "bigFont.fnt");
            if (lbl) {
                lbl->setScale(0.4f);
                lbl->setPosition({
                    m_player1->getContentSize().width  * 0.5f,
                    m_player1->getContentSize().height + 8.0f
                });
                m_player1->addChild(lbl, 10);
            }
        }

        const CCPoint base = m_player1->getPosition();

        // P2 — dual or spawned
        if (!level->m_twoPlayerMode) {
            if (m_player2) m_player2->setVisible(false);
            if (PlayerObject* p2 = spawnExtraPlayer(0, base)) {
                m_fields->extraPlayers[0] = p2;
                m_fields->ownP2 = true;
            }
        } else {
            if (m_player2) {
                applyVisuals(m_player2, PLAYER_VISUALS[0]);
                m_fields->extraPlayers[0] = m_player2;
                m_player2->retain();
                log::info("QuadroCoop: Dual mode — m_player2 registered and styled (Blue).");
            }
        }

        // P3 (if enabled in settings)
        if (isP3Enabled()) {
            if (PlayerObject* p3 = spawnExtraPlayer(1, base))
                m_fields->extraPlayers[1] = p3;
        } else {
            log::info("QuadroCoop: P3 disabled in settings — skipped.");
        }

        // P4 (if enabled in settings)
        if (isP4Enabled()) {
            if (PlayerObject* p4 = spawnExtraPlayer(2, base))
                m_fields->extraPlayers[2] = p4;
        } else {
            log::info("QuadroCoop: P4 disabled in settings — skipped.");
        }

        // Mirror into the global array so the trigger and portal hooks
        // can access extra players without depending on m_fields.
        for (int i = 0; i < EXTRA_PLAYER_COUNT; ++i)
            g_extraPlayers[i] = m_fields->extraPlayers[i];

        return true;
    }

    // ── postUpdate — physics driver ───────────────────────────────
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        // Drive P1 manually on mobile (touches are consumed).
        if (m_player1) {
            const auto& cur  = g_heldInputs[PlayerID::P1];
            const auto& prev = m_fields->prevHeldP1;
            for (auto a : cur)  if (!prev.count(a))  m_player1->pushButton(actionToButton(a));
            for (auto a : prev) if (!cur.count(a))   m_player1->releaseButton(actionToButton(a));
            m_fields->prevHeldP1 = cur;
        }
#endif

        constexpr std::array<PlayerID, EXTRA_PLAYER_COUNT> IDS = {
            PlayerID::P2, PlayerID::P3, PlayerID::P4
        };

        for (int i = 0; i < EXTRA_PLAYER_COUNT; ++i) {
            PlayerObject* player = m_fields->extraPlayers[i];
            if (!player) continue;

            const auto& cur  = g_heldInputs[IDS[i]];
            const auto& prev = m_fields->prevHeld[i];

            for (auto a : cur)  if (!prev.count(a))  player->pushButton(actionToButton(a));
            for (auto a : prev) if (!cur.count(a))   player->releaseButton(actionToButton(a));

            m_fields->prevHeld[i] = cur;
        }
    }

    // ── Dual Portal: bumpPlayer hook ──────────────────────────────
    //
    //  GD calls bumpPlayer(player, portal) when any player collides
    //  with a portal object.  We check if the colliding player is one
    //  of our managed extra players and whether the portal is a dual-
    //  mode toggle.  If so, flip the per-player isDual flag and call
    //  toggleDualMode to update the visual split-player state.
    //
    //  Portal IDs: 99 = Dual ON, 101 = Dual OFF.
    //  NOTE: verify these IDs against GD 2.2081's object registry.
    void bumpPlayer(PlayerObject* player, GameObject* portal) {
        PlayLayer::bumpPlayer(player, portal);

        if (!portal) return;
        const int portalID = portal->m_objectID;
        if (portalID != PORTAL_DUAL_ON && portalID != PORTAL_DUAL_OFF) return;

        // Check if the colliding player is one of our extra players.
        for (int i = 0; i < EXTRA_PLAYER_COUNT; ++i) {
            if (m_fields->extraPlayers[i] != player) continue;

            const bool wantDual = (portalID == PORTAL_DUAL_ON);
            if (m_fields->isDual[i] == wantDual) break; // already in correct state

            m_fields->isDual[i] = wantDual;
            // Update mirror
            // toggleDualMode(bool) toggles the split-character visual.
            // If GD's API differs, replace with setDualMode or equivalent.
            player->toggleDualMode(wantDual, false, true, true);

            log::info("QuadroCoop: P{} dual mode -> {}",
                i + 2, wantDual ? "ON" : "OFF");
            break;
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────
    void onQuit() {
        for (int i = 0; i < EXTRA_PLAYER_COUNT; ++i) {
            if (m_fields->extraPlayers[i]) {
                m_fields->extraPlayers[i]->release();
                m_fields->extraPlayers[i] = nullptr;
            }
        }
        g_extraPlayers.fill(nullptr); // clear global mirror
        g_heldInputs.clear();
        g_activeTouches.clear();
        PlayLayer::onQuit();
    }

    // ── Mobile touch hooks ────────────────────────────────────────

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        const CCSize  win  = CCDirector::get()->getWinSize();
        const float   hw   = win.width * 0.5f, hh = win.height * 0.5f;
        const CCPoint pos  = touch->getLocation();
        const PlayerID pid = detectTouchQuadrant(pos, win);
        const bool isLeft  = (pid == PlayerID::P1 || pid == PlayerID::P3);
        const float lx     = pos.x - (isLeft ? 0.f : hw);
        const float ly     = pos.y - ((pid == PlayerID::P3 || pid == PlayerID::P4) ? 0.f : hh);
        const PlayerAction action = resolveTouchAction(lx, ly, hw, hh);
        const PlayerInputEvent evt = { .player = pid, .action = action, .pressed = true };
        dispatchPlayerInput(evt);
        g_activeTouches[touch->getID()] = evt;
        return true; // consume — prevent native P1 double-move
#else
        return PlayLayer::ccTouchBegan(touch, event);
#endif
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        auto it = g_activeTouches.find(touch->getID());
        if (it != g_activeTouches.end()) {
            auto rel = it->second; rel.pressed = false;
            dispatchPlayerInput(rel); g_activeTouches.erase(it);
        }
#else
        PlayLayer::ccTouchEnded(touch, event);
#endif
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        auto it = g_activeTouches.find(touch->getID());
        if (it != g_activeTouches.end()) {
            auto can = it->second; can.pressed = false;
            dispatchPlayerInput(can); g_activeTouches.erase(it);
        }
#else
        PlayLayer::ccTouchCancelled(touch, event);
#endif
    }
};


// ================================================================
//  Section 8 — Trigger System
//
//  Strategy: position-delta cloning.
//
//  Before calling the parent triggerObject, record the world
//  positions of m_player1 and m_player2.  After the call, compute
//  the displacement vector.  If any movement occurred, apply the
//  same vector to each enabled extra player.
//
//  This correctly handles Move triggers with "Player Only" targeting
//  without needing to inspect GD's internal trigger type IDs or
//  group ID assignments.
//
//  The g_triggerCloning guard prevents this hook from re-entering
//  itself if triggerObject is called recursively by a trigger chain.
//
//  Triggers that do not move players (Color, Alpha, Toggle, etc.)
//  produce a zero delta and are therefore ignored — the extra players
//  live inside the same layer and share the same group-visibility
//  state as the rest of the level, so group-based triggers already
//  affect them correctly without any special handling.
// ================================================================

class $modify(CoopGameLayer, GJBaseGameLayer) {
    void triggerObject(GameObject* obj, int p1, CCPoint const* p2) {
        // Skip clone logic if we are already inside a cloned call,
        // or if extra players have not been spawned (non-platformer).
        if (g_triggerCloning || !m_player1) {
            GJBaseGameLayer::triggerObject(obj, p1, p2);
            return;
        }

        // Snapshot positions before the trigger fires.
        const CCPoint preP1 = m_player1->getPosition();
        const CCPoint preP2 = m_player2 ? m_player2->getPosition() : CCPointZero;

        GJBaseGameLayer::triggerObject(obj, p1, p2);

        // Compute the displacement the trigger applied to the native players.
        const CCPoint deltaP1 = m_player1->getPosition() - preP1;
        const CCPoint deltaP2 = m_player2
            ? (m_player2->getPosition() - preP2)
            : CCPointZero;

        // Use the larger of the two deltas as the representative movement.
        // In most levels only one player is targeted at a time.
        const float mag1 = deltaP1.x * deltaP1.x + deltaP1.y * deltaP1.y;
        const float mag2 = deltaP2.x * deltaP2.x + deltaP2.y * deltaP2.y;
        const CCPoint delta = (mag1 >= mag2) ? deltaP1 : deltaP2;

        // No movement: nothing to clone.
        if (delta.x == 0.f && delta.y == 0.f) return;

        g_triggerCloning = true;

        // Apply the same displacement to every live extra player.
        // g_extraPlayers[0] = P2 (already moved above if it's m_player2,
        // but our spawned P2 is a separate object so it needs the delta).
        for (PlayerObject* ep : g_extraPlayers) {
            if (!ep) continue;
            // Skip if this pointer IS m_player2 (already moved by the trigger).
            if (ep == m_player2) continue;
            ep->setPosition(ep->getPosition() + delta);
        }

        g_triggerCloning = false;

        log::debug("QuadroCoop: Trigger cloned delta ({:.1f},{:.1f}) to extra players.",
            delta.x, delta.y);
    }
};


// ================================================================
//  Section 9 — Settings Popup + PauseLayer Hook
//
//  CoopSettingsPopup: a Geode Popup<> with two CCMenuItemToggler
//  items.  Toggle state is persisted to Mod::get() saved values
//  ("enable-p3" and "enable-p4").  Changes take effect on the
//  next level load (respawning during a session is not supported).
//
//  CoopPauseLayer: hooks PauseLayer::customSetup to inject a
//  "QuadroCoop" button in the bottom-left of the pause menu.
// ================================================================

class CoopSettingsPopup : public geode::Popup<> {
public:
    static CoopSettingsPopup* create() {
        auto* ret = new CoopSettingsPopup();
        if (ret && ret->initAnchored(280.f, 180.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    bool setup() override {
        this->setTitle("QuadroCoop Players");

        auto* menu = CCMenu::create();

        // ── P3 Toggle ─────────────────────────────────────────────
        auto* toggleP3 = CCMenuItemToggler::createWithStandardSprites(
            this,
            menu_selector(CoopSettingsPopup::onToggleP3),
            1.0f
        );
        if (toggleP3) {
            toggleP3->toggle(isP3Enabled());
            toggleP3->setPosition({-60.f, 20.f});
            menu->addChild(toggleP3);
        }

        auto* labelP3 = CCLabelBMFont::create("Enable P3 (Red, J/I/L)", "bigFont.fnt");
        if (labelP3) {
            labelP3->setScale(0.35f);
            labelP3->setAnchorPoint({0.f, 0.5f});
            labelP3->setPosition({-35.f, 20.f});
            menu->addChild(labelP3);
        }

        // ── P4 Toggle ─────────────────────────────────────────────
        auto* toggleP4 = CCMenuItemToggler::createWithStandardSprites(
            this,
            menu_selector(CoopSettingsPopup::onToggleP4),
            1.0f
        );
        if (toggleP4) {
            toggleP4->toggle(isP4Enabled());
            toggleP4->setPosition({-60.f, -20.f});
            menu->addChild(toggleP4);
        }

        auto* labelP4 = CCLabelBMFont::create("Enable P4 (Green, F/T/H)", "bigFont.fnt");
        if (labelP4) {
            labelP4->setScale(0.35f);
            labelP4->setAnchorPoint({0.f, 0.5f});
            labelP4->setPosition({-35.f, -20.f});
            menu->addChild(labelP4);
        }

        auto* note = CCLabelBMFont::create("Takes effect on next level load", "chatFont.fnt");
        if (note) {
            note->setScale(0.5f);
            note->setPosition({0.f, -65.f});
            note->setOpacity(160);
            menu->addChild(note);
        }

        menu->setPosition(this->m_mainLayer->getContentSize() / 2);
        this->m_mainLayer->addChild(menu);

        return true;
    }

private:
    void onToggleP3(CCObject* sender) {
        const bool newVal = !isP3Enabled();
        Mod::get()->setSavedValue("enable-p3", newVal);
        log::info("QuadroCoop: P3 {} via settings popup.", newVal ? "enabled" : "disabled");
    }

    void onToggleP4(CCObject* sender) {
        const bool newVal = !isP4Enabled();
        Mod::get()->setSavedValue("enable-p4", newVal);
        log::info("QuadroCoop: P4 {} via settings popup.", newVal ? "enabled" : "disabled");
    }
};

class $modify(CoopPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        // Place the button at the bottom-left of the pause menu,
        // anchored to a fixed screen offset so it does not overlap
        // the existing GD pause buttons.
        auto* btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("QuadroCoop", "bigFont.fnt", "GJ_button_01.png"),
            this,
            menu_selector(CoopPauseLayer::onCoopSettings)
        );
        if (!btn) return;

        auto* menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition({60.f, 50.f});
        this->addChild(menu, 10);
    }

    void onCoopSettings(CCObject*) {
        if (auto* popup = CoopSettingsPopup::create()) {
            popup->show();
        }
    }
};


// ================================================================
//  Section 10 — Mod Entry Point
// ================================================================

$on_mod(Loaded) {
    log::info("QuadroCoop v2.1 loaded.");
    log::info("  Input    : P1(AWD) P2(Arrows) P3(IJL) P4(FTH)");
    log::info("  P3       : {}", isP3Enabled() ? "enabled" : "disabled (settings)");
    log::info("  P4       : {}", isP4Enabled() ? "enabled" : "disabled (settings)");
    log::info("  Triggers : position-delta cloning active");
    log::info("  Portals  : dual portal hook on bumpPlayer");
    log::info("  Menu     : settings button in PauseLayer");
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    log::info("  Mobile   : 2x2 quadrant grid, touches consumed");
#endif
}
