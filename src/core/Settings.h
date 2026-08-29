#pragma once

#include <string>

namespace neoncoil
{
    enum class DisplayMode
    {
        Windowed,       // titlebar, resizable by the window manager
        Borderless,     // desktop-sized, no decoration, alt-tabs instantly
        Fullscreen,     // exclusive, takes the display mode

        Count
    };

    const wchar_t* displayModeName(DisplayMode mode);

    // Everything the options screen writes and the game reads back on the next
    // launch.
    //
    // Deliberately separate from NetConfig: that file is deployment
    // configuration, authored by whoever ships the build and never touched by a
    // player. This one is the opposite -- it exists only because a player
    // changed something, and it is written every time they do.
    struct Settings
    {
        // --- display ----------------------------------------------------------
        DisplayMode displayMode{ DisplayMode::Windowed };
        bool vsync{ true };

        // --- audio ------------------------------------------------------------
        // Stored and persisted, but nothing reads them yet: this build has no
        // audio at all. They are here rather than added later so that the day
        // sound arrives, every player who already set a volume still has it --
        // and so the options screen can show the rows greyed out with an honest
        // reason instead of pretending the sliders do something.
        int masterVolume{ 80 };   // 0..100
        int musicVolume{ 70 };
        int effectsVolume{ 90 };

        // --- gameplay ---------------------------------------------------------
        // Screen shake and bloom are the two effects most likely to be turned
        // off for comfort rather than for performance, which is why they are
        // player-facing rather than a quality preset.
        bool screenShake{ true };
        bool bloom{ true };
        bool showPing{ true };

        // Returns false if the file is absent, which is not an error: it means
        // this player has never opened the options screen.
        bool loadFromFile(const std::string& path);
        bool saveToFile(const std::string& path) const;

        static Settings& instance();
        static const std::string& path();
        static void setPath(std::string path);

        // Writes the singleton to path(). Called whenever the options screen
        // changes something, so a crash never loses a setting the player just
        // chose.
        static void save();
    };
}
