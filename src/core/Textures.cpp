#include "Textures.h"

#include <SFML/Graphics/Image.hpp>

#include <algorithm>
#include <iostream>

namespace neoncoil
{
    namespace
    {
        std::filesystem::path executableDirectory()
        {
            std::error_code ec;
            const std::filesystem::path current = std::filesystem::current_path(ec);
            return ec ? std::filesystem::path{ "." } : current;
        }
    }

    std::filesystem::path Textures::assetRoot()
    {
        // Cached: the walk touches the filesystem and the answer cannot change
        // during a run.
        static const std::filesystem::path root = []
        {
            std::filesystem::path directory = executableDirectory();

            for (int depth = 0; depth < 6; ++depth)
            {
                std::error_code ec;
                const std::filesystem::path candidate = directory / "assets";
                if (std::filesystem::is_directory(candidate, ec))
                    return candidate;

                if (!directory.has_parent_path() || directory.parent_path() == directory)
                    break;
                directory = directory.parent_path();
            }

            return std::filesystem::path{ "assets" };
        }();

        return root;
    }

    const sf::Texture* Textures::get(const std::string& relativePath)
    {
        return load(relativePath, false, 0);
    }

    const sf::Texture* Textures::getEmissive(const std::string& relativePath, int blackPoint)
    {
        return load(relativePath, true, blackPoint);
    }

    const sf::Texture* Textures::load(const std::string& relativePath, bool emissive, int blackPoint)
    {
        if (auto it = m_textures.find(relativePath); it != m_textures.end())
            return &it->second;

        // Only complain about a given file once.
        if (m_missing.contains(relativePath))
            return nullptr;

        const std::filesystem::path full = assetRoot() / relativePath;

        sf::Image image;
        if (!image.loadFromFile(full.string()))
        {
            m_missing[relativePath] = true;
            std::cerr << "asset not found: " << full.string() << " (falling back to procedural art)\n";
            return nullptr;
        }

        if (emissive)
        {
            const sf::Vector2u size = image.getSize();
            const float range = std::max(1.0f, 255.0f - static_cast<float>(blackPoint));

            for (unsigned y = 0; y < size.y; ++y)
            {
                for (unsigned x = 0; x < size.x; ++x)
                {
                    const sf::Color pixel = image.getPixel({ x, y });
                    const int luminance = std::max({ pixel.r, pixel.g, pixel.b });

                    const float alpha = (static_cast<float>(luminance) - static_cast<float>(blackPoint)) / range;
                    const auto scaled = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);

                    image.setPixel({ x, y }, sf::Color(pixel.r, pixel.g, pixel.b, scaled));
                }
            }
        }

        sf::Texture texture;
        if (!texture.loadFromImage(image))
        {
            m_missing[relativePath] = true;
            return nullptr;
        }

        texture.setSmooth(true);

        auto [inserted, ok] = m_textures.emplace(relativePath, std::move(texture));
        return ok ? &inserted->second : nullptr;
    }
}
