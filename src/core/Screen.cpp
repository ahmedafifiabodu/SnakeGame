#include "Screen.h"

#include "Glyphs.h"
#include "Input.h"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <algorithm>
#include <cmath>

namespace neoncoil
{
    namespace
    {
        sf::Color toSfml(Color colour)
        {
            return sf::Color(colour.r, colour.g, colour.b, colour.a);
        }

        // How many concentric rings a glow is built from. More rings is a
        // smoother falloff; four is enough at this scale and stays cheap.
        constexpr int kGlowRings = 4;
    }

    Screen::Screen(int cellsWide, int cellsHigh, const std::wstring& title)
        : m_cellsWide(std::max(1, cellsWide))
        , m_cellsHigh(std::max(1, cellsHigh))
        , m_title(title)
        , m_pixelBatch(sf::PrimitiveType::Triangles)
        , m_glowBatch(sf::PrimitiveType::Triangles)
        , m_cellBatch(sf::PrimitiveType::Triangles)
        , m_overlayBatch(sf::PrimitiveType::Triangles)
        , m_overlayGlowBatch(sf::PrimitiveType::Triangles)
    {
        buildTargets(title, true);
    }

    Screen::Screen(int cellsWide, int cellsHigh, Offscreen)
        : m_cellsWide(std::max(1, cellsWide))
        , m_cellsHigh(std::max(1, cellsHigh))
        , m_offscreen(true)
        , m_pixelBatch(sf::PrimitiveType::Triangles)
        , m_glowBatch(sf::PrimitiveType::Triangles)
        , m_cellBatch(sf::PrimitiveType::Triangles)
        , m_overlayBatch(sf::PrimitiveType::Triangles)
        , m_overlayGlowBatch(sf::PrimitiveType::Triangles)
    {
        buildTargets(L"", false);
    }

    Screen::~Screen() = default;

    void Screen::buildTargets(const std::wstring& title, bool windowed)
    {
        m_solidRect = m_atlas.rect(glyph::Block);
        m_cellGlyphs.assign(static_cast<std::size_t>(m_cellsWide) * m_cellsHigh, glyph::Space);

        const auto canvasSize = sf::Vector2u{
            static_cast<unsigned>(canvasWidth()),
            static_cast<unsigned>(canvasHeight()) };

        m_target = std::make_unique<sf::RenderTexture>(canvasSize);
        m_target->setSmooth(true);

        if (!windowed)
            return;

        // Open at a comfortable windowed size; the canvas is letterboxed into
        // whatever the window ends up being, so any size stays correct.
        const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
        unsigned width = canvasSize.x;
        unsigned height = canvasSize.y;
        while (width > desktop.size.x - 100 || height > desktop.size.y - 100)
        {
            width = width * 3 / 4;
            height = height * 3 / 4;
        }

        m_window = std::make_unique<sf::RenderWindow>(
            sf::VideoMode({ width, height }),
            std::string(title.begin(), title.end()),
            sf::Style::Titlebar | sf::Style::Close);

        m_window->setVerticalSyncEnabled(true);
        m_window->setKeyRepeatEnabled(true);
    }

    bool Screen::isOpen() const
    {
        return m_window && m_window->isOpen();
    }

    void Screen::requestClose()
    {
        if (m_window)
            m_window->close();
    }

    void Screen::toggleFullscreen()
    {
        if (!m_window)
            return;

        m_fullscreen = !m_fullscreen;
        const std::string title(m_title.begin(), m_title.end());

        if (m_fullscreen)
        {
            m_window->create(sf::VideoMode::getDesktopMode(), title, sf::State::Fullscreen);
        }
        else
        {
            const sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
            unsigned width = static_cast<unsigned>(canvasWidth());
            unsigned height = static_cast<unsigned>(canvasHeight());
            while (width > desktop.size.x - 100 || height > desktop.size.y - 100)
            {
                width = width * 3 / 4;
                height = height * 3 / 4;
            }
            m_window->create(sf::VideoMode({ width, height }), title,
                sf::Style::Titlebar | sf::Style::Close);
        }

        m_window->setVerticalSyncEnabled(true);
        m_window->setKeyRepeatEnabled(true);
    }

    void Screen::pumpEvents(Input& input)
    {
        input.beginFrame();

        if (!m_window)
            return;

        while (const std::optional<sf::Event> event = m_window->pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                m_window->close();
                return;
            }

            // Put the pointer on the virtual canvas before the event is handed
            // on. mapPixelToCoords runs against the letterboxed view, so this is
            // correct at any window size, on any monitor and in fullscreen --
            // and it is the only place in the game that has to know that.
            if (const auto* moved = event->getIf<sf::Event::MouseMoved>())
                setMouseFromPixel(input, moved->position);
            else if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>())
                setMouseFromPixel(input, pressed->position);
            else if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>())
                setMouseFromPixel(input, released->position);

            input.handleEvent(*event);
        }

        if (input.pressed(Action::Fullscreen))
            toggleFullscreen();
    }

    void Screen::clear(Color background)
    {
        m_clearColour = background;
        m_pixelBatch.clear();
        m_glowBatch.clear();
        m_cellBatch.clear();
        m_overlayBatch.clear();
        m_overlayGlowBatch.clear();
        m_backgroundSprites.clear();
        m_worldSprites.clear();
        m_uiSprites.clear();
        std::fill(m_cellGlyphs.begin(), m_cellGlyphs.end(), glyph::Space);
    }

    void Screen::appendQuad(sf::VertexArray& batch, float x, float y, float w, float h,
        const sf::FloatRect& texRect, Color colour)
    {
        if (colour.a == 0 || w <= 0.0f || h <= 0.0f)
            return;

        const sf::Color tint = toSfml(colour);

        const sf::Vector2f topLeft{ x, y };
        const sf::Vector2f topRight{ x + w, y };
        const sf::Vector2f bottomRight{ x + w, y + h };
        const sf::Vector2f bottomLeft{ x, y + h };

        // Inset the sampled rect by half a texel so neighbouring atlas cells
        // cannot bleed in when the canvas is scaled up.
        constexpr float inset = 0.5f;
        const sf::Vector2f tl{ texRect.position.x + inset, texRect.position.y + inset };
        const sf::Vector2f tr{ texRect.position.x + texRect.size.x - inset, texRect.position.y + inset };
        const sf::Vector2f br{ texRect.position.x + texRect.size.x - inset, texRect.position.y + texRect.size.y - inset };
        const sf::Vector2f bl{ texRect.position.x + inset, texRect.position.y + texRect.size.y - inset };

        batch.append(sf::Vertex{ topLeft, tint, tl });
        batch.append(sf::Vertex{ topRight, tint, tr });
        batch.append(sf::Vertex{ bottomRight, tint, br });

        batch.append(sf::Vertex{ topLeft, tint, tl });
        batch.append(sf::Vertex{ bottomRight, tint, br });
        batch.append(sf::Vertex{ bottomLeft, tint, bl });
    }

    // ------------------------------------------------------------ cell layer --

    void Screen::put(int x, int y, wchar_t glyph, Color foreground, Color background)
    {
        if (x < 0 || x >= m_cellsWide || y < 0 || y >= m_cellsHigh)
            return;

        m_cellGlyphs[static_cast<std::size_t>(y) * m_cellsWide + x] = glyph;

        const float px = static_cast<float>(x * kCellWidth);
        const float py = static_cast<float>(y * kCellHeight);
        const float w = static_cast<float>(kCellWidth);
        const float h = static_cast<float>(kCellHeight);

        appendQuad(m_cellBatch, px, py, w, h, m_solidRect, background);

        if (glyph != glyph::Space)
            appendQuad(m_cellBatch, px, py, w, h, m_atlas.rect(glyph), foreground);
    }

    void Screen::text(int x, int y, std::wstring_view s, Color foreground, Color background)
    {
        for (std::size_t i = 0; i < s.size(); ++i)
            put(x + static_cast<int>(i), y, s[i], foreground, background);
    }

    void Screen::textCentered(int y, std::wstring_view s, Color foreground, Color background)
    {
        textCenteredIn(0, m_cellsWide, y, s, foreground, background);
    }

    void Screen::textCenteredIn(int x, int spanWidth, int y, std::wstring_view s, Color foreground, Color background)
    {
        text(x + (spanWidth - static_cast<int>(s.size())) / 2, y, s, foreground, background);
    }

    void Screen::fillRect(int x, int y, int w, int h, wchar_t glyph, Color foreground, Color background)
    {
        for (int row = 0; row < h; ++row)
            for (int column = 0; column < w; ++column)
                put(x + column, y + row, glyph, foreground, background);
    }

    void Screen::horizontalLine(int x, int y, int length, wchar_t glyph, Color foreground, Color background)
    {
        for (int i = 0; i < length; ++i)
            put(x + i, y, glyph, foreground, background);
    }

    void Screen::verticalLine(int x, int y, int length, wchar_t glyph, Color foreground, Color background)
    {
        for (int i = 0; i < length; ++i)
            put(x, y + i, glyph, foreground, background);
    }

    void Screen::panel(int x, int y, int w, int h, Color border, Color background, bool doubleLine)
    {
        if (w < 2 || h < 2)
            return;

        fillRect(x, y, w, h, glyph::Space, border, background);

        const wchar_t horizontal = doubleLine ? glyph::BoxH : glyph::ThinH;
        const wchar_t vertical = doubleLine ? glyph::BoxV : glyph::ThinV;
        const wchar_t topLeft = doubleLine ? glyph::BoxTopLeft : glyph::ThinTopLeft;
        const wchar_t topRight = doubleLine ? glyph::BoxTopRight : glyph::ThinTopRight;
        const wchar_t bottomLeft = doubleLine ? glyph::BoxBottomLeft : glyph::ThinBottomLeft;
        const wchar_t bottomRight = doubleLine ? glyph::BoxBottomRight : glyph::ThinBottomRight;

        horizontalLine(x + 1, y, w - 2, horizontal, border, background);
        horizontalLine(x + 1, y + h - 1, w - 2, horizontal, border, background);
        verticalLine(x, y + 1, h - 2, vertical, border, background);
        verticalLine(x + w - 1, y + 1, h - 2, vertical, border, background);

        put(x, y, topLeft, border, background);
        put(x + w - 1, y, topRight, border, background);
        put(x, y + h - 1, bottomLeft, border, background);
        put(x + w - 1, y + h - 1, bottomRight, border, background);
    }

    wchar_t Screen::glyphAt(int x, int y) const
    {
        if (x < 0 || x >= m_cellsWide || y < 0 || y >= m_cellsHigh)
            return glyph::Space;
        return m_cellGlyphs[static_cast<std::size_t>(y) * m_cellsWide + x];
    }

    // ----------------------------------------------------------- pixel layer --

    void Screen::rect(float x, float y, float w, float h, Color colour)
    {
        appendQuad(m_pixelBatch, x, y, w, h, m_solidRect, colour);
    }

    void Screen::drawGlyph(float x, float y, float w, float h, wchar_t glyph, Color colour)
    {
        appendQuad(m_pixelBatch, x, y, w, h, m_atlas.rect(glyph), colour);
    }

    void Screen::appendGlowRings(sf::VertexArray& batch, float x, float y, float w, float h,
        const sf::FloatRect& texRect, Color colour, float spread, float intensity)
    {
        // Stacked additive rings, biggest and faintest first. Cheap, needs no
        // shader, and reads as a proper neon bloom once several overlap.
        for (int ring = kGlowRings; ring >= 1; --ring)
        {
            const float t = static_cast<float>(ring) / static_cast<float>(kGlowRings);
            const float pad = spread * t;
            const float alpha = intensity * 42.0f * (1.0f - t) + 14.0f;

            appendQuad(batch, x - pad, y - pad, w + pad * 2.0f, h + pad * 2.0f, texRect,
                colour.withAlpha(static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f))));
        }
    }

    void Screen::glow(float centreX, float centreY, float radius, Color colour, float intensity)
    {
        appendGlowRings(m_glowBatch, centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f,
            m_atlas.rect(glyph::Circle), colour, radius * 0.9f, intensity);
    }

    void Screen::glowRect(float x, float y, float w, float h, Color colour, float spread, float intensity)
    {
        appendGlowRings(m_glowBatch, x, y, w, h, m_solidRect, colour, spread, intensity);
    }

    void Screen::overlayRect(float x, float y, float w, float h, Color colour)
    {
        appendQuad(m_overlayBatch, x, y, w, h, m_solidRect, colour);
    }

    void Screen::overlayGlowRect(float x, float y, float w, float h, Color colour, float spread, float intensity)
    {
        appendGlowRings(m_overlayGlowBatch, x, y, w, h, m_solidRect, colour, spread, intensity);
    }

    // ----------------------------------------------------------- image assets --

    void Screen::sprite(const sf::Texture& texture, float x, float y, float w, float h,
        SpriteLayer layer, Color tint, bool additive)
    {
        SpriteDraw draw;
        draw.texture = &texture;
        draw.x = x;
        draw.y = y;
        draw.w = w;
        draw.h = h;
        draw.tint = tint;
        draw.additive = additive;

        switch (layer)
        {
        case SpriteLayer::Background: m_backgroundSprites.push_back(draw); break;
        case SpriteLayer::World:      m_worldSprites.push_back(draw); break;
        case SpriteLayer::Ui:         m_uiSprites.push_back(draw); break;
        }
    }

    void Screen::spriteFitted(const sf::Texture& texture, float x, float y, float w, float h,
        SpriteLayer layer, Color tint, bool additive)
    {
        const sf::Vector2u size = texture.getSize();
        if (size.x == 0 || size.y == 0)
            return;

        const float scale = std::min(w / static_cast<float>(size.x), h / static_cast<float>(size.y));
        const float drawW = static_cast<float>(size.x) * scale;
        const float drawH = static_cast<float>(size.y) * scale;

        sprite(texture, x + (w - drawW) * 0.5f, y + (h - drawH) * 0.5f, drawW, drawH, layer, tint, additive);
    }

    void Screen::drawSprites(sf::RenderTarget& target, const std::vector<SpriteDraw>& sprites) const
    {
        for (const SpriteDraw& draw : sprites)
        {
            const sf::Vector2u size = draw.texture->getSize();
            if (size.x == 0 || size.y == 0)
                continue;

            sf::Sprite sprite(*draw.texture);
            sprite.setPosition({ draw.x, draw.y });
            sprite.setScale({ draw.w / static_cast<float>(size.x), draw.h / static_cast<float>(size.y) });
            sprite.setColor(toSfml(draw.tint));

            sf::RenderStates states;
            if (draw.additive)
                states.blendMode = sf::BlendAdd;

            target.draw(sprite, states);
        }
    }

    void Screen::setIcon(const sf::Texture& texture)
    {
        if (!m_window)
            return;

        const sf::Image image = texture.copyToImage();
        m_window->setIcon(image.getSize(), image.getPixelsPtr());
    }

    // ---------------------------------------------------------------- present --

    void Screen::flushBatches(sf::RenderTarget& target)
    {
        sf::RenderStates states;
        states.texture = &m_atlas.texture();

        drawSprites(target, m_backgroundSprites);

        if (m_pixelBatch.getVertexCount() > 0)
            target.draw(m_pixelBatch, states);

        drawSprites(target, m_worldSprites);

        if (m_glowBatch.getVertexCount() > 0)
        {
            sf::RenderStates additive = states;
            additive.blendMode = sf::BlendAdd;
            target.draw(m_glowBatch, additive);
        }

        if (m_cellBatch.getVertexCount() > 0)
            target.draw(m_cellBatch, states);

        drawSprites(target, m_uiSprites);

        if (m_overlayBatch.getVertexCount() > 0)
            target.draw(m_overlayBatch, states);

        if (m_overlayGlowBatch.getVertexCount() > 0)
        {
            sf::RenderStates additive = states;
            additive.blendMode = sf::BlendAdd;
            target.draw(m_overlayGlowBatch, additive);
        }
    }

    void Screen::setMouseFromPixel(Input& input, sf::Vector2i pixel)
    {
        if (!m_window)
            return;

        const sf::Vector2f canvas = m_window->mapPixelToCoords(pixel);

        // Outside the canvas the cell is deliberately out of range rather than
        // clamped, so a click in the letterbox bars hits nothing instead of
        // hitting the nearest edge control.
        const Vec2 cell{
            static_cast<int>(std::floor(canvas.x / static_cast<float>(kCellWidth))),
            static_cast<int>(std::floor(canvas.y / static_cast<float>(kCellHeight)))
        };

        input.setMousePosition(canvas.x, canvas.y, cell);
    }

    void Screen::applyLetterbox()
    {
        if (!m_window)
            return;

        const sf::Vector2u windowSize = m_window->getSize();
        if (windowSize.x == 0 || windowSize.y == 0)
            return;

        const float canvasAspect = canvasWidth() / canvasHeight();
        const float windowAspect = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);

        float viewWidth = 1.0f;
        float viewHeight = 1.0f;

        if (windowAspect > canvasAspect)
            viewWidth = canvasAspect / windowAspect;   // pillarbox
        else
            viewHeight = windowAspect / canvasAspect;  // letterbox

        sf::View view(sf::FloatRect({ 0.0f, 0.0f }, { canvasWidth(), canvasHeight() }));
        view.setViewport(sf::FloatRect(
            { (1.0f - viewWidth) * 0.5f, (1.0f - viewHeight) * 0.5f },
            { viewWidth, viewHeight }));

        m_window->setView(view);
    }

    void Screen::present()
    {
        m_target->clear(toSfml(m_clearColour));
        flushBatches(*m_target);
        m_target->display();

        if (!m_window)
            return;

        applyLetterbox();
        m_window->clear(sf::Color::Black);
        m_window->draw(sf::Sprite(m_target->getTexture()));
        m_window->display();
    }

    bool Screen::saveScreenshot(const std::string& path)
    {
        m_target->clear(toSfml(m_clearColour));
        flushBatches(*m_target);
        m_target->display();

        return m_target->getTexture().copyToImage().saveToFile(path);
    }
}
