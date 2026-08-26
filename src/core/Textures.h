#pragma once

#include <SFML/Graphics/Texture.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace neoncoil
{
    // Loads and owns the game's image assets.
    //
    // Everything here is optional: a missing file logs once and returns null, and
    // every call site falls back to the procedural drawing it used before. The
    // game must stay playable from a bare executable.
    class Textures
    {
    public:
        // Finds the assets directory by walking up from the executable, so the
        // same build works from build/bin during development and from a flat
        // depot folder once shipped.
        static std::filesystem::path assetRoot();

        // Straight load. Returns null if the file is absent or unreadable.
        const sf::Texture* get(const std::string& relativePath);

        // Load, then derive alpha from luminance. The neon logo arrived as a
        // JPEG, so its transparency had already been flattened into a visible
        // checkerboard; keying on brightness removes that and keeps the glow
        // falloff soft instead of cutting it off at a hard matte edge.
        const sf::Texture* getEmissive(const std::string& relativePath, int blackPoint = 40);

    private:
        const sf::Texture* load(const std::string& relativePath, bool emissive, int blackPoint);

        std::unordered_map<std::string, sf::Texture> m_textures;
        std::unordered_map<std::string, bool> m_missing;
    };
}
