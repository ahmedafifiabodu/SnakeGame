#pragma once

#include "../core/Colors.h"
#include "../core/Input.h"
#include "../core/Rng.h"
#include "../core/Screen.h"
#include "../game/SnakeType.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neoncoil
{
    // What the player configured in the menu. Carried across the whole run.
    struct PlayerProfile
    {
        std::wstring name{ L"PLAYER" };
        Color colour{ Color::Green };
        int snakeTypeIndex{ 0 };

        const SnakeType& type() const { return snakeTypeAt(snakeTypeIndex); }
    };

    // What a finished run looked like. Passed to the result screens so they do
    // not need a pointer back into PlayState.
    struct RunSummary
    {
        int score{ 0 };
        int level{ 1 };
        int levelTarget{ 100 };
        int foodEaten{ 0 };
        int longestSnake{ 0 };
        int abilitiesUsed{ 0 };
        std::uint64_t runSeed{ 0 };
        std::uint64_t levelSeed{ 0 };
        std::wstring causeOfDeath{ L"" };
    };

    // Shared services handed to every state. Passed by reference on each call
    // rather than stored, so no state can outlive what it points at.
    struct AppContext
    {
        Screen& screen;
        Input& input;
        Rng& rng;
        PlayerProfile& profile;

        std::uint64_t runSeed{ 0 };

        // Set when the player passed --seed. A pinned seed is reused for every
        // run so a reported level stays reproducible; otherwise each run rolls
        // a fresh one.
        bool seedPinned{ false };

        std::uint64_t nextRunSeed()
        {
            if (!seedPinned)
                runSeed = Rng::seedFromClock();
            return runSeed;
        }
    };

    class GameState;

    enum class TransitionKind
    {
        None,
        Push,     // overlay: the state below stays alive
        Pop,      // return to the state below
        Replace,  // swap the top of the stack
        Reset,    // clear the stack and start again with `state`
        Quit
    };

    struct Transition
    {
        TransitionKind kind{ TransitionKind::None };
        std::unique_ptr<GameState> state;

        static Transition none() { return {}; }
        static Transition pop() { return Transition{ TransitionKind::Pop, nullptr }; }
        static Transition quit() { return Transition{ TransitionKind::Quit, nullptr }; }
        static Transition push(std::unique_ptr<GameState> next) { return Transition{ TransitionKind::Push, std::move(next) }; }
        static Transition replace(std::unique_ptr<GameState> next) { return Transition{ TransitionKind::Replace, std::move(next) }; }
        static Transition reset(std::unique_ptr<GameState> next) { return Transition{ TransitionKind::Reset, std::move(next) }; }
    };

    class GameState
    {
    public:
        virtual ~GameState() = default;

        virtual void onEnter(AppContext&) {}
        virtual void onExit(AppContext&) {}

        virtual Transition update(AppContext& context, float deltaSeconds) = 0;
        virtual void render(AppContext& context) = 0;

        // Overlays (pause, level complete, game over) draw the live world
        // underneath themselves instead of replacing it.
        virtual bool isOverlay() const { return false; }
    };

    // A stack rather than a single current state, so overlays can suspend the
    // world without tearing it down and restoring it afterwards.
    class StateMachine
    {
    public:
        void apply(Transition&& transition, AppContext& context);

        void update(AppContext& context, float deltaSeconds);
        void render(AppContext& context);

        bool empty() const { return m_stack.empty(); }
        bool wantsQuit() const { return m_wantsQuit; }

        // The state currently receiving input, or null when the stack is empty.
        // Used by the headless capture driver to tell which screen it is
        // looking at; the game itself never needs to ask.
        GameState* top() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }

    private:
        std::vector<std::unique_ptr<GameState>> m_stack;
        bool m_wantsQuit{ false };
    };
}
