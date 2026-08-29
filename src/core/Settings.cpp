#include "Settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace neoncoil
{
    namespace
    {
        std::string g_path = "neoncoil_settings.txt";

        std::string trim(const std::string& text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        bool asBool(const std::string& value)
        {
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }

        int asVolume(const std::string& value)
        {
            return std::clamp(std::atoi(value.c_str()), 0, 100);
        }
    }

    const wchar_t* displayModeName(DisplayMode mode)
    {
        switch (mode)
        {
        case DisplayMode::Windowed:   return L"WINDOWED";
        case DisplayMode::Borderless: return L"BORDERLESS";
        case DisplayMode::Fullscreen: return L"FULLSCREEN";
        case DisplayMode::Count:      break;
        }
        return L"WINDOWED";
    }

    Settings& Settings::instance()
    {
        static Settings settings;
        return settings;
    }

    const std::string& Settings::path()
    {
        return g_path;
    }

    void Settings::setPath(std::string path)
    {
        g_path = std::move(path);
    }

    void Settings::save()
    {
        (void)instance().saveToFile(g_path);
    }

    bool Settings::loadFromFile(const std::string& file)
    {
        std::ifstream in(file);
        if (!in)
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#')
                continue;

            const auto equals = trimmed.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = trim(trimmed.substr(0, equals));
            const std::string value = trim(trimmed.substr(equals + 1));
            if (key.empty() || value.empty())
                continue;

            if (key == "display_mode")
            {
                // Matched by name rather than by number so the file stays
                // readable, and so reordering the enum cannot silently put a
                // player into fullscreen they never asked for.
                if (value == "windowed")        displayMode = DisplayMode::Windowed;
                else if (value == "borderless") displayMode = DisplayMode::Borderless;
                else if (value == "fullscreen") displayMode = DisplayMode::Fullscreen;
            }
            else if (key == "vsync")            vsync = asBool(value);
            else if (key == "master_volume")    masterVolume = asVolume(value);
            else if (key == "music_volume")     musicVolume = asVolume(value);
            else if (key == "effects_volume")   effectsVolume = asVolume(value);
            else if (key == "screen_shake")     screenShake = asBool(value);
            else if (key == "bloom")            bloom = asBool(value);
            else if (key == "show_ping")        showPing = asBool(value);
        }

        return true;
    }

    bool Settings::saveToFile(const std::string& file) const
    {
        std::ofstream out(file, std::ios::trunc);
        if (!out)
            return false;

        const char* mode = "windowed";
        if (displayMode == DisplayMode::Borderless)
            mode = "borderless";
        else if (displayMode == DisplayMode::Fullscreen)
            mode = "fullscreen";

        out << "# NEON COIL player settings.\n"
            << "#\n"
            << "# Written by the options screen. Safe to delete -- the game puts\n"
            << "# every value back to its default and writes it again.\n"
            << "\n"
            << "display_mode = " << mode << "\n"
            << "vsync = " << (vsync ? 1 : 0) << "\n"
            << "\n"
            << "# Nothing reads these yet: this build has no audio. They are kept\n"
            << "# so a volume chosen today survives into the build that has sound.\n"
            << "master_volume = " << masterVolume << "\n"
            << "music_volume = " << musicVolume << "\n"
            << "effects_volume = " << effectsVolume << "\n"
            << "\n"
            << "screen_shake = " << (screenShake ? 1 : 0) << "\n"
            << "bloom = " << (bloom ? 1 : 0) << "\n"
            << "show_ping = " << (showPing ? 1 : 0) << "\n";

        return static_cast<bool>(out);
    }
}
