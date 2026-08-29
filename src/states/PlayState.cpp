#include "PlayState.h"

#include "OverlayStates.h"
#include "../core/Glyphs.h"
#include "../game/LevelGenerator.h"
#include "../ui/Art.h"
#include "../ui/Draw.h"
#include "../ui/Layout.h"

#include <deque>
#include <algorithm>
#include <cmath>

namespace neoncoil
{
    namespace
    {
        // Seconds a pickup keeps the combo alive.
        constexpr float kComboWindow = 3.5f;
        constexpr int kMaxComboMultiplier = 5;

        // Ceiling on catch-up steps per frame. Without it, a stall (a drag of
        // the window, a breakpoint) would fire dozens of moves at once and kill
        // the player through no fault of their own.
        constexpr int kMaxStepsPerFrame = 3;

        constexpr float kIntroSeconds = 1.5f;
        constexpr float kDeathSeconds = 1.0f;

    }

    PlayState::PlayState(std::uint64_t runSeed)
        : m_runSeed(runSeed)
        , m_rng(runSeed)
        , m_effects(Rng::mix(runSeed, 0xE55EC75ull))
    {
    }

    void PlayState::onEnter(AppContext& context)
    {
        m_ability.configure(context.profile.type().ability);
        startLevel(context, 1);
    }

    void PlayState::startLevel(AppContext& context, int levelIndex)
    {
        const SnakeType& type = context.profile.type();

        m_levelIndex = levelIndex;
        m_plan = planFor(levelIndex);
        m_level = LevelGenerator::generate(levelIndex, m_runSeed, type.startLength);

        m_rng.reseed(Rng::mix(m_level.seed, 0xF00Dull));

        m_snake.reset(m_level.size(), m_level.spawn, m_level.spawnDirection, type.startLength);
        m_ability.reset();
        m_food.clear();
        m_effects.clear();

        m_scoreThisLevel = 0;
        m_foodEatenThisLevel = 0;
        m_tickAccumulator = 0.0f;
        m_comboTimer = 0.0f;
        m_comboCount = 0;
        m_introTimer = kIntroSeconds;
        m_longestSnake = std::max(m_longestSnake, m_snake.length());

        m_food.spawn(FoodKind::Normal, m_level, m_snake, m_rng);
    }

    int PlayState::comboMultiplier() const
    {
        if (m_comboCount <= 1)
            return 1;
        return std::min(kMaxComboMultiplier, 1 + (m_comboCount - 1) / 2);
    }

    float PlayState::currentTickSeconds(const AppContext& context) const
    {
        const float speed = context.profile.type().speedMultiplier * m_ability.speedScale();
        return m_plan.tickSeconds / std::max(0.1f, speed);
    }

    void PlayState::readDirectionInput(const Input& input)
    {
        // Queue every direction pressed this frame in order. Snake rejects
        // illegal turns itself, so this layer stays free of game rules.
        if (input.pressed(Action::Up))    m_snake.queueDirection(Direction::Up);
        if (input.pressed(Action::Down))  m_snake.queueDirection(Direction::Down);
        if (input.pressed(Action::Left))  m_snake.queueDirection(Direction::Left);
        if (input.pressed(Action::Right)) m_snake.queueDirection(Direction::Right);
    }

    Transition PlayState::update(AppContext& context, float deltaSeconds)
    {
        // Returning here after the level-complete overlay popped.
        if (m_advanceOnResume)
        {
            m_advanceOnResume = false;
            startLevel(context, m_levelIndex + 1);
            return Transition::none();
        }

        m_elapsed += deltaSeconds;
        m_effects.update(deltaSeconds);

        if (m_dead)
        {
            m_deathTimer -= deltaSeconds;
            if (m_deathTimer <= 0.0f)
                return Transition::push(std::make_unique<GameOverState>(buildSummary()));
            return Transition::none();
        }

        const Input& input = context.input;

        if (input.pressed(Action::Pause) || input.pressed(Action::Back))
            return Transition::push(std::make_unique<PauseState>());

        readDirectionInput(input);

        if (input.pressed(Action::Ability))
            activateAbility(context);

        m_ability.update(deltaSeconds);
        m_level.updateHazards(deltaSeconds);
        m_food.update(deltaSeconds);

        // A sentinel that walks onto the head has to be lethal too, otherwise
        // hazards are only dangerous when the player moves into them.
        if (m_introTimer <= 0.0f && m_level.hazardAt(m_snake.head()))
        {
            if (m_ability.consumeShield())
            {
                m_level.clearSentinels();
                m_effects.burst(m_snake.head(), 22, Color::Blue, 9.0f);
                m_effects.popText(m_snake.head(), L"SHIELD!", Color::Blue);
                m_effects.flash(Color::Blue, 0.3f);
            }
            else
            {
                die(context, L"Caught by a sentinel");
                return Transition::none();
            }
        }

        m_comboTimer = std::max(0.0f, m_comboTimer - deltaSeconds);
        if (m_comboTimer <= 0.0f)
            m_comboCount = 0;

        if (m_introTimer > 0.0f)
        {
            m_introTimer -= deltaSeconds;
            return Transition::none();
        }

        // A bonus fruit can expire while the normal one is still on the board;
        // this keeps exactly one normal fruit available at all times.
        if (!m_food.hasNormal() && !m_food.spawn(FoodKind::Normal, m_level, m_snake, m_rng))
        {
            // No free tile anywhere: the snake has filled the board. That is a
            // win, not a hang.
            m_scoreThisLevel = m_plan.targetScore;
        }

        const float tick = currentTickSeconds(context);
        m_tickAccumulator += deltaSeconds;

        for (int step = 0; step < kMaxStepsPerFrame && m_tickAccumulator >= tick && !m_dead; ++step)
        {
            m_tickAccumulator -= tick;
            stepSnake(context);
        }
        if (m_tickAccumulator > tick)
            m_tickAccumulator = tick;

        if (m_dead)
            return Transition::none();

        if (m_scoreThisLevel >= m_plan.targetScore)
        {
            const int bonus = levelCompletionBonus(m_levelIndex, m_scoreThisLevel, m_plan.targetScore);
            m_totalScore += bonus;

            m_effects.flash(Color::Lime, 0.5f);
            m_advanceOnResume = true;
            return Transition::push(std::make_unique<LevelCompleteState>(buildSummary(), bonus));
        }

        return Transition::none();
    }

    void PlayState::stepSnake(AppContext& context)
    {
        const Vec2 next = m_snake.nextHead();

        const bool phasing = m_ability.canPhaseWalls();
        const bool blockedByBorder = m_level.isBorder(next);           // never passable
        const bool blockedByWall = !blockedByBorder && m_level.isWall(next) && !phasing;
        const bool blockedBySelf = m_snake.occupiesAfterStep(next) && !m_ability.canPhaseSelf();
        const bool hitHazard = m_level.hazardAt(next);

        if (blockedByBorder || blockedByWall || blockedBySelf || hitHazard)
        {
            // Iron Scales turns the first lethal hit into an opportunity.
            if (m_ability.consumeShield() && !blockedByBorder)
            {
                if (blockedByWall)
                    m_level.destroyWall(next);

                if (hitHazard)
                {
                    m_level.clearSentinels();
                    m_effects.popText(next, L"SENTINELS PURGED", Color::Blue);
                }

                m_effects.burst(next, 22, Color::Blue, 9.0f);
                m_effects.addShake(1.2f);
                m_effects.popText(next, L"SHIELD!", Color::Blue);
                m_effects.flash(Color::Blue, 0.3f);
            }
            else
            {
                if (blockedByBorder)
                    die(context, L"Ran into the arena wall");
                else if (blockedByWall)
                    die(context, L"Ran into an obstacle");
                else if (hitHazard)
                    die(context, L"Caught by a sentinel");
                else
                    die(context, L"Bit your own tail");
                return;
            }
        }

        m_snake.commitStep();
        m_longestSnake = std::max(m_longestSnake, m_snake.length());

        // Phase can expire mid-corridor. Solidifying inside geometry is fatal:
        // it is what stops Phase from being a free pass through every level.
        if (!m_ability.canPhaseWalls() && m_level.isWall(m_snake.head()))
        {
            die(context, L"Solidified inside a wall");
            return;
        }

        if (const std::optional<std::size_t> index = m_food.indexAt(m_snake.head()); index.has_value())
            consumeFood(context, *index);
    }

    void PlayState::consumeFood(AppContext& context, std::size_t foodIndex)
    {
        const SnakeType& type = context.profile.type();
        const Food food = m_food.items()[foodIndex];
        m_food.removeAt(foodIndex);

        m_comboCount += 1;
        m_comboTimer = kComboWindow;

        const float kindMultiplier = food.kind == FoodKind::Bonus ? 3.0f : 1.0f;
        const float raw = static_cast<float>(m_plan.foodValue) * kindMultiplier *
            type.scoreMultiplier * m_ability.scoreMultiplier() * static_cast<float>(comboMultiplier());
        const int value = static_cast<int>(raw + 0.5f);

        m_scoreThisLevel += value;
        m_totalScore += value;
        ++m_foodEatenThisLevel;
        ++m_foodEatenTotal;

        m_snake.grow(type.growthPerFood + (food.kind == FoodKind::Bonus ? 1 : 0));

        const Color burstColour = food.kind == FoodKind::Bonus ? Color::Gold : Color::Coral;
        m_effects.burst(food.position, food.kind == FoodKind::Bonus ? 18 : 10, burstColour);
        m_effects.popText(food.position, L"+" + std::to_wstring(value), burstColour);
        if (comboMultiplier() > 1)
            m_effects.popText(food.position + Vec2{ 0, -1 }, L"x" + std::to_wstring(comboMultiplier()), Color::Gold);

        if (food.kind == FoodKind::Normal)
            m_food.spawn(FoodKind::Normal, m_level, m_snake, m_rng);

        const bool scheduledBonus = m_plan.bonusEveryNFoods > 0 &&
            m_foodEatenThisLevel % m_plan.bonusEveryNFoods == 0;

        if (scheduledBonus || m_ability.spawnsBonusOnEat())
            m_food.spawn(FoodKind::Bonus, m_level, m_snake, m_rng, m_plan.bonusLifetimeSeconds);
    }

    void PlayState::activateAbility(AppContext& context)
    {
        // Shed on a short snake would burn the cooldown for nothing, so it is
        // refused rather than wasted.
        if (m_ability.definition().kind == AbilityKind::Shed && m_snake.length() <= 6)
        {
            if (m_ability.isReady())
                m_effects.popText(m_snake.head(), L"TOO SHORT", Color::Slate);
            return;
        }

        const std::optional<AbilityKind> fired = m_ability.tryActivate();
        if (!fired.has_value())
            return;

        ++m_abilitiesUsed;
        const Color accent = context.profile.type().accent;

        switch (*fired)
        {
        case AbilityKind::Shed:
        {
            const int keep = std::max(4, m_snake.length() / 2);
            const std::vector<Vec2> shed = m_snake.shedTo(keep);

            const int banked = static_cast<int>(shed.size()) * 5;
            m_scoreThisLevel += banked;
            m_totalScore += banked;

            for (const Vec2& segment : shed)
                m_effects.burst(segment, 2, accent, 4.0f);

            if (banked > 0)
                m_effects.popText(m_snake.head(), L"+" + std::to_wstring(banked) + L" SHED", accent);
            break;
        }

        case AbilityKind::IronScales:
            m_effects.popText(m_snake.head(), L"IRON SCALES", Color::Blue);
            break;

        case AbilityKind::Dash:
            m_effects.popText(m_snake.head(), L"DASH", Color::Gold);
            break;

        case AbilityKind::Phase:
            m_effects.popText(m_snake.head(), L"PHASE", Color::Aqua);
            break;

        case AbilityKind::GoldRush:
            m_effects.popText(m_snake.head(), L"GOLD RUSH", Color::Gold);
            m_food.spawn(FoodKind::Bonus, m_level, m_snake, m_rng, m_plan.bonusLifetimeSeconds);
            break;
        }

        m_effects.burst(m_snake.head(), 16, accent, 8.0f);
        m_effects.addShake(0.6f);
    }

    void PlayState::die(AppContext& context, std::wstring cause)
    {
        (void)context;

        m_dead = true;
        m_deathTimer = kDeathSeconds;
        m_causeOfDeath = std::move(cause);

        m_effects.addShake(2.5f);
        m_effects.flash(Color::Red, 0.45f);

        int index = 0;
        for (const Vec2& segment : m_snake.body())
        {
            if (index++ % 2 == 0)
                m_effects.burst(segment, 3, Color::Red, 6.0f);
        }
    }

    RunSummary PlayState::buildSummary() const
    {
        RunSummary summary;
        summary.score = m_totalScore;
        summary.level = m_levelIndex;
        summary.levelTarget = m_plan.targetScore;
        summary.foodEaten = m_foodEatenTotal;
        summary.longestSnake = m_longestSnake;
        summary.abilitiesUsed = m_abilitiesUsed;
        summary.runSeed = m_runSeed;
        summary.levelSeed = m_level.seed;
        summary.causeOfDeath = m_causeOfDeath;
        return summary;
    }

    ui::HudModel PlayState::buildHudModel(const AppContext& context) const
    {
        const SnakeType& type = context.profile.type();

        ui::HudModel model;
        model.playerName = context.profile.name;
        model.snakeTypeName = type.name;
        model.snakeColour = context.profile.colour;
        model.score = m_totalScore;
        model.level = m_levelIndex;
        model.levelTarget = m_plan.targetScore;
        model.scoreThisLevel = m_scoreThisLevel;
        model.length = m_snake.length();
        model.comboMultiplier = comboMultiplier();
        model.abilityName = type.ability.name;
        model.abilityCharge = m_ability.chargeFraction();
        model.abilityReady = m_ability.isReady();
        model.abilityActive = m_ability.isActive();
        model.abilityActiveFraction = m_ability.activeFraction();
        model.shieldHeld = m_ability.hasShield();
        model.archetypeName = m_level.archetypeName;
        model.runSeed = m_runSeed;
        return model;
    }


    void PlayState::render(AppContext& context)
    {
        Screen& screen = context.screen;
        screen.clear(Color::Black);

        ui::drawHud(screen, buildHudModel(context));
        renderBoard(context);
        ui::drawPlayFooter(screen, context.profile.type().ability.name, m_ability.isReady());

        // Level intro: the board is visible but frozen, so the player gets a
        // beat to read the layout before anything can kill them.
        if (m_introTimer > 0.0f)
        {
            const int bannerY = 15;
            ui::drawBannerCentered(screen, bannerY, L"LEVEL " + std::to_wstring(m_levelIndex),
                Color::Gold, Color::Transparent);
            screen.textCentered(bannerY + 7, m_level.archetypeName, Color::Aqua, Color::Transparent);

            const std::wstring target = L"TARGET " + std::to_wstring(m_plan.targetScore);
            screen.textCentered(bannerY + 9, target, Color::Silver, Color::Transparent);
        }
    }

    void PlayState::renderBoard(AppContext& context) const
    {
        Screen& screen = context.screen;
        const SnakeType& type = context.profile.type();

        // Screen shake is a pixel offset on the board origin, so nothing else
        // has to know about it.
        const Vec2 shake = m_effects.shakeOffset();
        const ui::BoardView view{
            ui::kBoardPixelX + static_cast<float>(shake.x),
            ui::kBoardPixelY + static_cast<float>(shake.y),
            ui::kTilePixels
        };

        // --- frame and floor --------------------------------------------------
        const float frame = ui::kBoardFrameThickness;
        screen.rect(view.originX - frame, view.originY - frame,
            ui::kBoardPixelWidth + frame * 2.0f, ui::kBoardPixelHeight + frame * 2.0f,
            Color::Slate);
        screen.rect(view.originX, view.originY, ui::kBoardPixelWidth, ui::kBoardPixelHeight, Color::Navy);

        const std::wstring caption = m_level.archetypeName + L"  SEED " +
            std::to_wstring(m_level.seed % 100000ull);
        screen.text(ui::kScreenWidth - static_cast<int>(caption.size()) - 4, ui::kBoardCaptionRow,
            caption, Color::Slate, Color::Transparent);

        // --- walls ------------------------------------------------------------
        for (int y = 0; y < m_level.height(); ++y)
        {
            for (int x = 0; x < m_level.width(); ++x)
            {
                const Vec2 tile{ x, y };

                if (m_level.isBorder(tile))
                {
                    ui::boardTile(screen, view, tile, Color::Blue.scaled(0.55f));
                    continue;
                }

                if (m_level.isWall(tile))
                {
                    ui::boardTile(screen, view, tile, Color::Slate);

                    // Lit edge only where the block is actually exposed --
                    // bevelling every tile draws seams through the middle of
                    // multi-tile pillars.
                    if (!m_level.isWall({ x, y - 1 }))
                        screen.rect(view.left(x), view.top(y), view.tileSize, 3.0f, Color::Slate.scaled(1.55f));
                    if (!m_level.isWall({ x, y + 1 }))
                        screen.rect(view.left(x), view.top(y) + view.tileSize - 3.0f, view.tileSize, 3.0f,
                            Color::Slate.scaled(0.62f));
                    continue;
                }

                // Deterministic speckle so the floor has texture without
                // costing memory or flickering between frames.
                const std::uint32_t hash = static_cast<std::uint32_t>(x) * 73856093u ^
                    static_cast<std::uint32_t>(y) * 19349663u ^
                    static_cast<std::uint32_t>(m_level.seed);
                if ((hash % 41u) == 0u)
                    ui::boardGlyph(screen, view, tile, glyph::Dot, Color::Slate.scaled(1.2f), 0.5f);
            }
        }

        // Illustrated board pieces, if the art is present. Each falls back to the
        // procedural glyph so a bare build still plays. Walls deliberately stay
        // procedural: their art is near-identical to the drawn bevel, and a wall
        // sprite would render over a phasing snake passing through it.
        // Loaded emissive: the art was delivered on a near-black field, which
        // would otherwise show as a dark box on the navy floor. Keying alpha to
        // luminance drops the surround and leaves a soft edge on the glow.
        const sf::Texture* foodArt = screen.textures().getEmissive("objects/food_normal.png", 22);
        const sf::Texture* bonusArt = screen.textures().getEmissive("objects/food_bonus.png", 22);
        const sf::Texture* sentinelArt = screen.textures().getEmissive("objects/sentinel.png", 22);

        const auto boardSprite = [&](const sf::Texture& texture, Vec2 tile, float scale)
        {
            const float size = view.tileSize * scale;
            const float offset = (view.tileSize - size) * 0.5f;
            screen.sprite(texture, view.left(tile.x) + offset, view.top(tile.y) + offset,
                size, size, Screen::SpriteLayer::World);
        };

        // --- food -------------------------------------------------------------
        for (const Food& food : m_food.items())
        {
            if (food.kind == FoodKind::Normal)
            {
                const float pulse = 0.80f + 0.12f * std::sin(m_elapsed * 4.0f);
                screen.glow(view.centreX(static_cast<float>(food.position.x)),
                    view.centreY(static_cast<float>(food.position.y)),
                    view.tileSize * 0.95f, Color::Coral, 1.1f);

                if (foodArt != nullptr)
                    boardSprite(*foodArt, food.position, 1.7f);
                else
                    ui::boardGlyph(screen, view, food.position, glyph::Circle, Color::Coral, pulse * 0.8f);
            }
            else
            {
                // Blink faster as it runs out.
                const bool urgent = food.secondsRemaining < 2.5f;
                if (urgent && static_cast<int>(food.secondsRemaining * 8.0f) % 2 != 0)
                    continue;

                screen.glow(view.centreX(static_cast<float>(food.position.x)),
                    view.centreY(static_cast<float>(food.position.y)),
                    view.tileSize * 1.2f, Color::Gold, 1.4f);

                if (bonusArt != nullptr)
                    boardSprite(*bonusArt, food.position, 1.8f);
                else
                    ui::boardGlyph(screen, view, food.position, glyph::Star, Color::Gold, 1.0f);
            }
        }

        // --- hazards ----------------------------------------------------------
        for (const Sentinel& sentinel : m_level.sentinels())
        {
            screen.glow(view.centreX(static_cast<float>(sentinel.position.x)),
                view.centreY(static_cast<float>(sentinel.position.y)),
                view.tileSize, Color::Red, 1.2f);

            if (sentinelArt != nullptr)
                boardSprite(*sentinelArt, sentinel.position, 1.6f);
            else
                ui::boardGlyph(screen, view, sentinel.position, glyph::Diamond, Color::Red, 0.95f);
        }

        // --- snake ------------------------------------------------------------
        const bool phasing = m_ability.canPhaseWalls();
        const Color bodyColour = phasing ? Color::Aqua : context.profile.colour;
        const Color headColour = m_ability.hasShield() ? Color::Blue
            : (m_dead ? Color::Red : Color::White);

        // Segments are inset so the body reads as linked blocks rather than one
        // continuous bar, which was the single biggest legibility problem.
        constexpr float kSegmentInset = 2.0f;
        const float alpha = phasing ? 0.45f : 1.0f;

        // How far through the current step the snake is. Drawn on tile
        // boundaries it does not move, it teleports about seven times a second;
        // sliding each segment out of the tile behind it turns the same
        // simulation into continuous motion.
        //
        // A dead snake holds still: sliding a corpse into the wall that killed
        // it reads as the crash not having registered.
        const float stepSeconds = std::max(0.02f, currentTickSeconds(context));
        const float slide = m_dead ? 0.0f
            : std::clamp(m_tickAccumulator / stepSeconds, 0.0f, 1.0f);

        const std::deque<Vec2>& segments = m_snake.body();

        for (std::size_t i = segments.size(); i-- > 0; )
        {
            const bool isHead = i == 0;
            const std::size_t fromTail = segments.size() - 1 - i;

            Color colour = isHead ? headColour : bodyColour;
            if (type.altBodyGlyph != 0 && !isHead && fromTail % 2 == 0)
                colour = colour.scaled(0.72f);          // banding, for Ouroboros
            if (!isHead && fromTail == 0)
                colour = colour.scaled(0.62f);          // the tail fades out
            colour = colour.withAlpha(static_cast<std::uint8_t>(alpha * 255.0f));

            // Where segment i+1 is now is exactly where segment i was a step
            // ago, so the slide needs no history of its own. The tail has
            // nothing behind it and stays put.
            const Vec2 from = (i + 1 < segments.size()) ? segments[i + 1] : segments[i];
            const Vec2 to = segments[i];

            ui::boardTileLerp(screen, view, from, to, slide, colour, kSegmentInset);

            const float px = view.left(from.x) + (view.left(to.x) - view.left(from.x)) * slide;
            const float py = view.top(from.y) + (view.top(to.y) - view.top(from.y)) * slide;

            if (isHead)
            {
                screen.glowRect(px, py, view.tileSize, view.tileSize,
                    headColour, view.tileSize * 0.9f, 1.3f);
            }
            else if (fromTail % 2 == 0)
            {
                // Glowing every other segment keeps the bloom from washing the
                // whole body out, and costs half the quads.
                screen.glowRect(px + kSegmentInset, py + kSegmentInset,
                    view.tileSize - kSegmentInset * 2.0f, view.tileSize - kSegmentInset * 2.0f,
                    bodyColour, view.tileSize * 0.35f, 0.55f);
            }
        }

        m_effects.render(screen, view);

        // --- full-screen tint -------------------------------------------------
        if (m_effects.isFlashing())
        {
            const float strength = m_effects.flashFraction();
            screen.rect(view.originX, view.originY, ui::kBoardPixelWidth, ui::kBoardPixelHeight,
                m_effects.flashColour().withAlpha(static_cast<std::uint8_t>(strength * 90.0f)));
        }
    }
}
