#include "App.h"

#include "core/Input.h"
#include "core/Rng.h"
#include "core/Screen.h"
#include "game/LevelGenerator.h"
#include "game/Progression.h"
#include "net/HostSession.h"
#include "net/Identity.h"
#include "net/NetConfig.h"
#include "states/GameState.h"
#include "core/Glyphs.h"
#include "states/LobbyState.h"
#include "states/MenuState.h"
#include "states/MultiplayerMenuState.h"
#include "states/NetPlayState.h"
#include "states/OverlayStates.h"
#include "states/PlayState.h"
#include "tools/Autoplay.h"
#include "tools/NetDemo.h"
#include "ui/Draw.h"
#include "ui/Layout.h"
#include "ui/Palette.h"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace neoncoil
{
    namespace
    {
        constexpr float kMaxFrameSeconds = 0.1f;   // clamp after a stall
        constexpr int kTargetFps = 60;

        bool matches(const std::string& argument, const char* flag)
        {
            return argument == flag;
        }

        Color colourForTypeAccent(int typeIndex)
        {
            return snakeTypeAt(typeIndex).accent;
        }

        sf::Keyboard::Key keyFor(Direction direction)
        {
            switch (direction)
            {
            case Direction::Up:    return sf::Keyboard::Key::Up;
            case Direction::Down:  return sf::Keyboard::Key::Down;
            case Direction::Left:  return sf::Keyboard::Key::Left;
            case Direction::Right: return sf::Keyboard::Key::Right;
            }
            return sf::Keyboard::Key::Up;
        }

        // Hands the autopilot's decision to the game as real key events. Nothing
        // downstream can tell the difference between this and a player, which is
        // the point: the demo exercises the same input path a run does.
        void pressForDemo(Input& input, tools::Autoplay& autoplay, StateMachine& machine,
            const PlayState& play, float deltaSeconds, bool pressPause)
        {
            input.beginFrame();

            if (pressPause)
            {
                input.handleEvent(sf::Event{ sf::Event::KeyPressed{ .code = sf::Keyboard::Key::P } });
                return;
            }

            // Take the next level once the panel has been up for a beat, so a
            // long capture walks up the difficulty curve instead of stalling on
            // the first board. Nothing else is confirmed: game over is left
            // alone so a capture ends on the result screen rather than looping.
            const bool levelClear = dynamic_cast<LevelCompleteState*>(machine.top()) != nullptr;

            if (autoplay.tickLevelClear(levelClear, deltaSeconds))
                input.handleEvent(sf::Event{ sf::Event::KeyPressed{ .code = sf::Keyboard::Key::Enter } });

            if (levelClear)
                return;   // the board is frozen behind the panel; nothing to steer

            if (const std::optional<Direction> turn = autoplay.chooseTurn(play.level(), play.snake(), play.food()))
                input.handleEvent(sf::Event{ sf::Event::KeyPressed{ .code = keyFor(*turn) } });

            if (autoplay.tickAbility(deltaSeconds))
                input.handleEvent(sf::Event{ sf::Event::KeyPressed{ .code = sf::Keyboard::Key::Space } });
        }

        // Steps the state stack, optionally with the autopilot at the controls,
        // and calls `afterFrame` once per simulated frame. The screenshot, the
        // ASCII dump and the PNG sequence all run through here so they cannot
        // drift apart.
        void driveFrames(StateMachine& machine, AppContext& context, PlayState* play,
            const std::string& which, const LaunchOptions& options, const std::function<void(int)>& afterFrame,
            tools::NetDemo* netDemo = nullptr)
        {
            constexpr float kStep = 1.0f / 60.0f;
            tools::Autoplay autoplay;

            // A demo capture of the pause screen reaches it the way a player
            // does -- play for a while, then press P -- rather than opening on
            // it. Pausing at frame zero freezes the board on the level-one
            // intro, since a paused PlayState stops updating entirely.
            const int pauseAtFrame = (options.dumpFrames * 9) / 10;

            for (int frame = 0; frame < options.dumpFrames; ++frame)
            {
                if (options.demo && play != nullptr)
                {
                    const bool pause = (which == "pause") && (frame == pauseAtFrame);
                    pressForDemo(context.input, autoplay, machine, *play, kStep, pause);
                }

                // The demo guests are driven before the screen updates, so the
                // host has their input in hand for the same frame it simulates.
                // The state on top pumps the host session itself.
                if (netDemo != nullptr)
                {
                    netDemo->tick(kStep);

                    // On the multiplayer menu nothing owns the demo host, so it
                    // is pumped from here. Without that it never answers a
                    // discovery probe and the browser it exists to fill stays
                    // empty.
                    if (which == "netmenu" && netDemo->host() != nullptr)
                        netDemo->host()->update(kStep);
                }

                machine.update(context, kStep);

                if (afterFrame)
                    afterFrame(frame);
            }
        }
    }

    void printHelp()
    {
        std::cout <<
            "NEON COIL\n"
            "\n"
            "Usage: NeonCoil [options]\n"
            "\n"
            "  --seed <n>        Pin the run seed. Every level is derived from it, so a\n"
            "                    reported layout can be reproduced exactly.\n"
            "  --name <text>     Pre-fill the player name.\n"
            "  --snake <n>       Pre-select a snake type (1-" << snakeTypeCount() << ").\n"
            "  --selftest <n>    Generate and validate <n> levels, print a report, exit.\n"
            "  --dump <n>        Print level <n> as ASCII and exit.\n"
            "  --uidump <screen> Render a screen offscreen as ASCII. One of menu, play,\n"
            "                    pause, clear, over, netmenu, lobby, netplay.\n"
            "  --screenshot <screen> <file.png>\n"
            "                    Render a screen offscreen and write it to a PNG.\n"
            "  --capture <screen> <dir>\n"
            "                    Write a numbered PNG sequence into <dir>, for assembling\n"
            "                    into an animation.\n"
            "  --frames <n>      Frames to simulate before a dump or screenshot (default 200).\n"
            "  --every <n>       Save one frame in <n> while capturing (default 2).\n"
            "  --skip <n>        Simulate <n> frames before the first one is saved, so a\n"
            "                    capture can start deep into a run.\n"
            "  --demo            Let the autopilot play while capturing, so the shot shows a\n"
            "                    real run rather than an idle snake.\n"
            "\n"
            " Multiplayer (all optional -- the defaults work on a local network):\n"
            "  --nettest         Run a host and three clients over loopback, play a match out,\n"
            "                    print a report and exit. No window is opened.\n"
            "  --netconfig <file> Read networking settings from <file> (default netconfig.txt).\n"
            "  --port <n>        Port to host on, overriding the config file.\n"
            "  --discovery-port <n>\n"
            "                    Port used to find sessions on the local network.\n"
            "  --help            Show this message.\n"
            "\n"
            "With no arguments the game starts at the main menu.\n";
    }

    LaunchOptions parseArguments(int argc, char* argv[])
    {
        LaunchOptions options;

        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            const bool hasValue = (i + 1) < argc;

            if (matches(argument, "--help") || matches(argument, "-h") || matches(argument, "/?"))
            {
                options.showHelp = true;
            }
            else if (matches(argument, "--seed") && hasValue)
            {
                options.seed = std::strtoull(argv[++i], nullptr, 10);
                options.seedPinned = true;
            }
            else if (matches(argument, "--name") && hasValue)
            {
                options.playerName = ui::toWide(argv[++i]);
            }
            else if (matches(argument, "--snake") && hasValue)
            {
                options.snakeTypeIndex = std::atoi(argv[++i]) - 1;
            }
            else if (matches(argument, "--dump") && hasValue)
            {
                options.dumpLevel = std::max(1, std::atoi(argv[++i]));
            }
            else if (matches(argument, "--uidump") && hasValue)
            {
                options.uiDump = argv[++i];
            }
            else if (matches(argument, "--screenshot") && hasValue)
            {
                options.screenshot = argv[++i];
                options.screenshotPath = ((i + 1) < argc) ? argv[++i] : (options.screenshot + ".png");
            }
            else if (matches(argument, "--capture") && hasValue)
            {
                options.capture = argv[++i];
                options.captureDir = ((i + 1) < argc) ? argv[++i] : options.capture;
            }
            else if (matches(argument, "--frames") && hasValue)
            {
                options.dumpFrames = std::max(1, std::atoi(argv[++i]));
            }
            else if (matches(argument, "--every") && hasValue)
            {
                options.captureEvery = std::max(1, std::atoi(argv[++i]));
            }
            else if (matches(argument, "--skip") && hasValue)
            {
                options.captureSkip = std::max(0, std::atoi(argv[++i]));
            }
            else if (matches(argument, "--demo"))
            {
                options.demo = true;
            }
            else if (matches(argument, "--nettest"))
            {
                options.netSelfTest = true;
            }
            else if (matches(argument, "--netconfig") && hasValue)
            {
                options.netConfigPath = argv[++i];
            }
            else if (matches(argument, "--port") && hasValue)
            {
                options.hostPort = std::atoi(argv[++i]);
            }
            else if (matches(argument, "--discovery-port") && hasValue)
            {
                options.discoveryPort = std::atoi(argv[++i]);
            }
            else if (matches(argument, "--selftest"))
            {
                options.selfTestLevels = hasValue ? std::atoi(argv[++i]) : 2000;
                if (options.selfTestLevels <= 0)
                    options.selfTestLevels = 2000;
            }
            else
            {
                options.invalid = true;
                options.error = ui::toWide("unrecognised argument: " + argument);
                break;
            }
        }

        if (!options.seedPinned)
            options.seed = Rng::seedFromClock();

        return options;
    }

    int runGeneratorSelfTest(int levelsToTest, std::uint64_t seed)
    {
        std::cout << "Level generator self-test\n"
                  << "  base seed  : " << seed << "\n"
                  << "  iterations : " << levelsToTest << "\n\n";

        int failures = 0;
        int smallestArea = kBoardWidth * kBoardHeight;
        long long totalArea = 0;
        int sentinelTotal = 0;

        // Sweep level indices so every archetype and difficulty band is covered,
        // and both start lengths the roster actually uses.
        for (int i = 0; i < levelsToTest; ++i)
        {
            const int levelIndex = 1 + (i % 40);
            const int startLength = (i % 2 == 0) ? 4 : 5;
            const std::uint64_t runSeed = Rng::mix(seed, static_cast<std::uint64_t>(i));

            const Level level = LevelGenerator::generate(levelIndex, runSeed, startLength);
            const LevelReport report = LevelGenerator::validate(level, startLength);

            totalArea += report.reachableTiles;
            smallestArea = std::min(smallestArea, report.reachableTiles);
            sentinelTotal += static_cast<int>(level.sentinels().size());

            if (!report.valid)
            {
                ++failures;
                if (failures <= 10)
                {
                    std::wcout << L"  FAIL level " << levelIndex
                               << L" seed " << level.seed
                               << L" (" << level.archetypeName << L"): "
                               << report.failure << L"\n";
                }
            }
        }

        const double averageArea = levelsToTest > 0
            ? static_cast<double>(totalArea) / levelsToTest : 0.0;

        std::cout << "\n  reachable area: min " << smallestArea
                  << ", average " << averageArea
                  << " of " << (kBoardWidth - 2) * (kBoardHeight - 2) << " interior tiles\n"
                  << "  sentinels placed: " << sentinelTotal << "\n"
                  << "  failures: " << failures << "\n";

        if (failures == 0)
            std::cout << "\nAll generated levels satisfied every playability invariant.\n";

        return failures == 0 ? 0 : 1;
    }

    int dumpLevelToConsole(int levelIndex, std::uint64_t seed)
    {
        constexpr int kStartLength = 4;

        const Level level = LevelGenerator::generate(levelIndex, seed, kStartLength);
        const LevelReport report = LevelGenerator::validate(level, kStartLength);
        const LevelPlan plan = planFor(levelIndex);

        std::wcout << L"level " << levelIndex
                   << L"   archetype " << level.archetypeName
                   << L"   seed " << level.seed << L"\n"
                   << L"target " << plan.targetScore
                   << L"   food value " << plan.foodValue
                   << L"   tick " << plan.tickSeconds << L"s"
                   << L"   sentinels " << level.sentinels().size() << L"\n"
                   << L"open " << report.openTiles << L"/" << (kBoardWidth - 2) * (kBoardHeight - 2)
                   << L"   valid: " << (report.valid ? L"yes" : report.failure.c_str()) << L"\n\n";

        for (int y = 0; y < level.height(); ++y)
        {
            std::wstring row;
            row.reserve(static_cast<std::size_t>(level.width()));

            for (int x = 0; x < level.width(); ++x)
            {
                const Vec2 tile{ x, y };

                if (tile == level.spawn)
                    row.push_back(L'@');
                else if (level.hazardAt(tile))
                    row.push_back(L'!');
                else if (level.isBorder(tile))
                    row.push_back(L'#');
                else if (level.isWall(tile))
                    row.push_back(L'X');
                else
                    row.push_back(L'.');
            }

            std::wcout << row << L"\n";
        }

        return report.valid ? 0 : 1;
    }

    namespace
    {
        // Maps the game's Unicode glyphs onto ASCII stand-ins so a rendered
        // screen survives being printed through any console code page.
        char asciiFor(wchar_t glyph)
        {
            if (glyph >= 32 && glyph < 127)
                return static_cast<char>(glyph);

            switch (glyph)
            {
            case 0:
            case glyph::Space:        return ' ';
            case glyph::Block:        return '#';
            case glyph::ShadeDark:    return '%';
            case glyph::ShadeMedium:  return '+';
            case glyph::ShadeLight:   return '-';
            case glyph::HalfLeft:
            case glyph::HalfRight:
            case glyph::HalfLower:
            case glyph::HalfUpper:    return '|';
            case glyph::Circle:
            case glyph::CircleSmall:  return 'o';
            case glyph::Star:
            case glyph::Sparkle:      return '*';
            case glyph::Diamond:
            case glyph::DiamondSmall: return 'D';
            case glyph::Square:
            case glyph::SquareSmall:  return 's';
            case glyph::Dot:
            case glyph::Bullet:       return '.';
            case glyph::TriRight:     return '>';
            case glyph::TriLeft:      return '<';
            case glyph::Bolt:         return '!';
            case glyph::Shield:       return 'U';
            case glyph::BoxH:
            case glyph::ThinH:        return '-';
            case glyph::BoxV:
            case glyph::ThinV:        return '|';
            case glyph::BoxTopLeft:
            case glyph::BoxTopRight:
            case glyph::BoxBottomLeft:
            case glyph::BoxBottomRight:
            case glyph::ThinTopLeft:
            case glyph::ThinTopRight:
            case glyph::ThinBottomLeft:
            case glyph::ThinBottomRight: return '+';
            default:                  return '?';
            }
        }

        void printScreenAsAscii(const Screen& screen)
        {
            std::cout << '.' << std::string(static_cast<std::size_t>(screen.width()), '-') << ".\n";

            for (int y = 0; y < screen.height(); ++y)
            {
                std::string row;
                row.reserve(static_cast<std::size_t>(screen.width()));
                for (int x = 0; x < screen.width(); ++x)
                    row.push_back(asciiFor(screen.glyphAt(x, y)));

                std::cout << '|' << row << "|\n";
            }

            std::cout << '\'' << std::string(static_cast<std::size_t>(screen.width()), '-') << "'\n";
        }
    }

    // Pushes the state stack that `which` names. Shared by the ASCII layout dump
    // and the PNG screenshot so both always capture exactly the same thing.
    //
    // `playOut`, when given, receives the live PlayState so the demo autopilot
    // can read the board it is steering on. It stays null for "menu", which has
    // no run behind it.
    bool buildScreen(StateMachine& machine, AppContext& context, const std::string& which,
        const LaunchOptions& options, PlayState** playOut = nullptr,
        tools::NetDemo* netDemo = nullptr)
    {
        const std::uint64_t seed = options.seed;

        RunSummary summary;
        summary.score = 1450;
        summary.level = 4;
        summary.levelTarget = planFor(4).targetScore;
        summary.foodEaten = 47;
        summary.longestSnake = 19;
        summary.abilitiesUsed = 6;
        summary.runSeed = seed;
        summary.causeOfDeath = L"Ran into an obstacle";

        if (which == "menu")
        {
            machine.apply(Transition::reset(std::make_unique<MenuState>()), context);
            return true;
        }

        if (which == "netmenu")
        {
            // A real session on a real socket, purely so the browser has
            // something to find. The open-sessions panel is half this screen,
            // and a capture of it empty shows the half that does not matter.
            // Failing to open one is not fatal: the screen is still the screen.
            if (netDemo != nullptr)
            {
                std::wstring demoError;
                if (!netDemo->start(1, false, demoError))
                    std::wcerr << L"could not open a demo session to browse: " << demoError << L"\n";
            }

            machine.apply(Transition::reset(std::make_unique<MultiplayerMenuState>()), context);
            return true;
        }

        if ((which == "lobby" || which == "netplay") && netDemo != nullptr)
        {
            // A four-seat session, over real loopback sockets, so the capture
            // shows the screen as a player would actually meet it rather than a
            // lobby with one occupant and three empty chairs.
            std::wstring demoError;
            if (!netDemo->start(kMaxMatchPlayers - 1, which == "netplay", demoError))
            {
                std::wcerr << L"could not open a demo session: " << demoError << L"\n";
                return false;
            }

            if (which == "netplay")
                netDemo->startMatch();

            net::HostSession* const live = netDemo->host()
                ? static_cast<net::HostSession*>(netDemo->host())
                : nullptr;

            machine.apply(Transition::reset(std::make_unique<LobbyState>(netDemo->takeHost())), context);

            if (which == "netplay" && live != nullptr)
                machine.apply(Transition::push(std::make_unique<NetPlayState>(live)), context);

            return true;
        }

        if (which == "lobby" || which == "netplay")
        {
            // A real session on a real socket, not a mock-up: the lobby screen
            // is driven by whatever the host session actually reports, so a
            // faked one would not be checking the thing that can break. Guests
            // are not simulated here -- a populated lobby is covered by
            // --nettest and by running two copies of the game.
            auto session = std::make_unique<net::HostSession>(net::NetConfig::instance(),
                net::identityProvider());

            std::wstring error;
            if (!session->open(context.profile.name,
                static_cast<std::uint8_t>(ui::playerColourIndex(context.profile.colour)),
                static_cast<std::uint8_t>(context.profile.snakeTypeIndex), net::HostSession::Reach::Direct, error))
            {
                std::wcerr << L"could not open a session to dump: " << error << L"\n";
                return false;
            }

            // The lobby owns the session for the rest of its life; the match
            // screen only borrows it, exactly as it does in the running game.
            net::HostSession* const live = session.get();
            machine.apply(Transition::reset(std::make_unique<LobbyState>(std::move(session))), context);

            if (which == "netplay")
            {
                live->requestStartMatch();
                machine.apply(Transition::push(std::make_unique<NetPlayState>(live)), context);
            }

            return true;
        }

        auto play = std::make_unique<PlayState>(seed);
        PlayState* const playState = play.get();
        machine.apply(Transition::reset(std::move(play)), context);

        if (playOut != nullptr)
            *playOut = playState;

        if (which == "pause")
        {
            // Under --demo the overlay is left to the driver, which presses P
            // once there is a board worth pausing on.
            if (!options.demo)
                machine.apply(Transition::push(std::make_unique<PauseState>()), context);
        }
        else if (which == "clear")
            machine.apply(Transition::push(std::make_unique<LevelCompleteState>(summary, 250)), context);
        else if (which == "over")
            machine.apply(Transition::push(std::make_unique<GameOverState>(summary)), context);
        else if (which != "play")
        {
            std::cerr << "unknown screen '" << which
                      << "' (menu|play|pause|clear|over|netmenu|lobby|netplay)\n";
            return false;
        }

        return true;
    }

    int dumpUserInterface(const std::string& which, const LaunchOptions& options)
    {
        Screen screen(ui::kScreenWidth, ui::kScreenHeight, Screen::Offscreen{});
        Input input;
        Rng rng(options.seed);

        PlayerProfile profile;
        if (!options.playerName.empty())
            profile.name = options.playerName;
        if (options.snakeTypeIndex >= 0)
            profile.snakeTypeIndex = std::clamp(options.snakeTypeIndex, 0, snakeTypeCount() - 1);

        AppContext context{ screen, input, rng, profile, options.seed, options.seedPinned };

        StateMachine machine;
        PlayState* play = nullptr;
        tools::NetDemo netDemo;
        if (!buildScreen(machine, context, which, options, &play, &netDemo))
            return 2;

        // A few frames so animations, timers and the level intro settle.
        driveFrames(machine, context, play, which, options, nullptr, &netDemo);

        machine.render(context);

        std::cout << "screen: " << which << "  (" << screen.width() << "x" << screen.height() << ")\n";
        printScreenAsAscii(screen);
        return 0;
    }

    App::App(const LaunchOptions& options)
        : m_options(options)
    {
    }

    void applyNetworkOptions(const LaunchOptions& options)
    {
        // File first, then command line. Nothing further down reads a port or a
        // timeout from anywhere else.
        net::NetConfig& netConfig = net::NetConfig::instance();
        netConfig.loadFromFile(options.netConfigPath);

        if (options.hostPort > 0)
            netConfig.hostPort = static_cast<std::uint16_t>(options.hostPort);
        if (options.discoveryPort > 0)
            netConfig.discoveryPort = static_cast<std::uint16_t>(options.discoveryPort);

        // The identity provider is installed here and nowhere else. Swapping
        // this one line for an account-backed provider is the whole of adding
        // logins as far as the networking layer is concerned.
        net::setIdentityProvider(std::make_unique<net::LocalIdentityProvider>(netConfig.identityFile));
    }

    int App::run()
    {
        Screen screen(ui::kScreenWidth, ui::kScreenHeight, L"NEON COIL");
        Input input;

        if (const sf::Texture* icon = screen.textures().get("ui/icon.png"))
            screen.setIcon(*icon);

        Rng rng(m_options.seed);

        PlayerProfile profile;
        if (!m_options.playerName.empty())
            profile.name = m_options.playerName;
        if (m_options.snakeTypeIndex >= 0)
        {
            profile.snakeTypeIndex = std::clamp(m_options.snakeTypeIndex, 0, snakeTypeCount() - 1);
            profile.colour = colourForTypeAccent(profile.snakeTypeIndex);
        }

        AppContext context{ screen, input, rng, profile, m_options.seed, m_options.seedPinned };

        StateMachine machine;
        machine.apply(Transition::reset(std::make_unique<MenuState>()), context);

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        const auto frameBudget = std::chrono::microseconds(1000000 / kTargetFps);

        while (screen.isOpen() && !machine.wantsQuit() && !machine.empty())
        {
            const auto frameStart = Clock::now();

            // Clamped so a stalled frame (window drag, alt-tab, a breakpoint)
            // cannot make the game fast-forward through the player's snake.
            float deltaSeconds = std::chrono::duration<float>(frameStart - previous).count();
            deltaSeconds = std::clamp(deltaSeconds, 0.0f, kMaxFrameSeconds);
            previous = frameStart;

            screen.pumpEvents(input);
            if (!screen.isOpen())
                break;

            machine.update(context, deltaSeconds);

            if (machine.wantsQuit() || machine.empty())
                break;

            machine.render(context);
            screen.present();

            // Vsync normally paces us; this is the fallback for when the driver
            // has vsync forced off.
            const auto elapsed = Clock::now() - frameStart;
            if (elapsed < frameBudget)
                std::this_thread::sleep_for(frameBudget - elapsed);
        }

        screen.requestClose();
        return 0;
    }

    int captureScreenshot(const std::string& which, const std::string& path, const LaunchOptions& options)
    {
        Screen screen(ui::kScreenWidth, ui::kScreenHeight, Screen::Offscreen{});
        Input input;
        Rng rng(options.seed);

        PlayerProfile profile;
        if (!options.playerName.empty())
            profile.name = options.playerName;
        if (options.snakeTypeIndex >= 0)
        {
            profile.snakeTypeIndex = std::clamp(options.snakeTypeIndex, 0, snakeTypeCount() - 1);
            profile.colour = colourForTypeAccent(profile.snakeTypeIndex);
        }

        AppContext context{ screen, input, rng, profile, options.seed, options.seedPinned };

        StateMachine machine;
        PlayState* play = nullptr;
        tools::NetDemo netDemo;
        if (!buildScreen(machine, context, which, options, &play, &netDemo))
            return 2;

        driveFrames(machine, context, play, which, options, nullptr, &netDemo);

        machine.render(context);

        if (!screen.saveScreenshot(path))
        {
            std::cerr << "could not write " << path << "\n";
            return 1;
        }

        std::cout << "wrote " << path << " (" << which << ", " << options.dumpFrames << " frames)\n";
        return 0;
    }

    int captureSequence(const std::string& which, const std::string& directory, const LaunchOptions& options)
    {
        std::error_code created;
        std::filesystem::create_directories(directory, created);
        if (created)
        {
            std::cerr << "could not create " << directory << ": " << created.message() << "\n";
            return 1;
        }

        Screen screen(ui::kScreenWidth, ui::kScreenHeight, Screen::Offscreen{});
        Input input;
        Rng rng(options.seed);

        PlayerProfile profile;
        if (!options.playerName.empty())
            profile.name = options.playerName;
        if (options.snakeTypeIndex >= 0)
        {
            profile.snakeTypeIndex = std::clamp(options.snakeTypeIndex, 0, snakeTypeCount() - 1);
            profile.colour = colourForTypeAccent(options.snakeTypeIndex);
        }

        AppContext context{ screen, input, rng, profile, options.seed, options.seedPinned };

        StateMachine machine;
        PlayState* play = nullptr;
        tools::NetDemo netDemo;
        if (!buildScreen(machine, context, which, options, &play, &netDemo))
            return 2;

        int written = 0;
        bool failed = false;

        driveFrames(machine, context, play, which, options, [&](int frame)
            {
                if (failed || frame < options.captureSkip)
                    return;
                if (((frame - options.captureSkip) % options.captureEvery) != 0)
                    return;

                machine.render(context);

                char name[32];
                std::snprintf(name, sizeof(name), "frame_%05d.png", written);

                if (!screen.saveScreenshot((std::filesystem::path{ directory } / name).string()))
                {
                    std::cerr << "could not write frame " << written << " into " << directory << "\n";
                    failed = true;
                    return;
                }

                ++written;
            });

        if (failed)
            return 1;

        std::cout << "wrote " << written << " frames into " << directory
            << " (" << which << ", " << options.dumpFrames << " simulated, every " << options.captureEvery << ")\n";
        return 0;
    }
}
