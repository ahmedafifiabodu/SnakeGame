#pragma once

#include "Colors.h"
#include "Settings.h"
#include "GlyphAtlas.h"
#include "Textures.h"

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace neoncoil
{
    class Input;

    // The render surface and the window that shows it.
    //
    // Two coordinate spaces, both on the same fixed virtual canvas that is
    // letterboxed to whatever the window size is:
    //
    //   * cells  - the character grid the UI, HUD and menus are laid out on.
    //   * pixels - free-form virtual pixels, used by the board and the effects
    //              so the snake can move smoothly and glow properly.
    //
    // Everything is batched: the pixel layer, the additive glow layer and the
    // cell layer are three draw calls total.
    class Screen
    {
    public:
        static constexpr int kCellWidth = 16;
        static constexpr int kCellHeight = 24;

        // Renders into a texture with no window. Used by --screenshot and the
        // --uidump layout check.
        struct Offscreen {};

        Screen(int cellsWide, int cellsHigh, const std::wstring& title);
        Screen(int cellsWide, int cellsHigh, Offscreen);
        ~Screen();

        Screen(const Screen&) = delete;
        Screen& operator=(const Screen&) = delete;

        int width() const { return m_cellsWide; }
        int height() const { return m_cellsHigh; }

        float canvasWidth() const { return static_cast<float>(m_cellsWide * kCellWidth); }
        float canvasHeight() const { return static_cast<float>(m_cellsHigh * kCellHeight); }

        // --- window ----------------------------------------------------------
        bool isOpen() const;
        void pumpEvents(Input& input);
        void requestClose();

        // Windowed, borderless or exclusive fullscreen. Recreates the window,
        // which is why it takes the mode rather than toggling: a toggle can only
        // ever describe two of the three, and F11 has to land somewhere sensible
        // whichever one the player is currently in.
        void setDisplayMode(DisplayMode mode);
        DisplayMode displayMode() const { return m_displayMode; }

        // F11. Fullscreen goes back to windowed; anything else goes fullscreen.
        void toggleFullscreen();

        void setVerticalSync(bool enabled);
        bool verticalSync() const { return m_vsync; }

        // --- frame -----------------------------------------------------------
        void clear(Color background = Color::Black);
        void present();

        bool saveScreenshot(const std::string& path);

        // --- cell layer ------------------------------------------------------
        void put(int x, int y, wchar_t glyph, Color foreground, Color background = Color::Transparent);
        void text(int x, int y, std::wstring_view s, Color foreground, Color background = Color::Transparent);
        void textCentered(int y, std::wstring_view s, Color foreground, Color background = Color::Transparent);
        void textCenteredIn(int x, int spanWidth, int y, std::wstring_view s, Color foreground, Color background = Color::Transparent);

        void fillRect(int x, int y, int w, int h, wchar_t glyph, Color foreground, Color background);
        void horizontalLine(int x, int y, int length, wchar_t glyph, Color foreground, Color background = Color::Transparent);
        void verticalLine(int x, int y, int length, wchar_t glyph, Color foreground, Color background = Color::Transparent);
        void panel(int x, int y, int w, int h, Color border, Color background, bool doubleLine = true);

        // Records what the cell layer holds, so --uidump can still print the
        // laid-out screen as ASCII without a GPU.
        wchar_t glyphAt(int x, int y) const;

        // --- pixel layer, drawn UNDER the cell layer -------------------------
        // This is the world: the board, the snake, food, hazards and effects.
        // Cell-layer panels and overlays therefore composite on top of it,
        // which is exactly what pause and game-over want.
        void rect(float x, float y, float w, float h, Color colour);
        void drawGlyph(float x, float y, float w, float h, wchar_t glyph, Color colour);

        // Additive. `spread` is how far the halo reaches, in virtual pixels.
        void glow(float centreX, float centreY, float radius, Color colour, float intensity = 1.0f);
        void glowRect(float x, float y, float w, float h, Color colour, float spread, float intensity = 1.0f);

        // --- pixel layer, drawn OVER the cell layer --------------------------
        // For pixel-precise widgets that sit inside a cell-drawn panel, such as
        // the HUD meters. Without this they would be buried by the panel's own
        // cell background.
        void overlayRect(float x, float y, float w, float h, Color colour);
        void overlayGlowRect(float x, float y, float w, float h, Color colour, float spread, float intensity = 1.0f);

        // --- image assets ----------------------------------------------------
        // Each sprite is its own draw call (a different texture cannot join the
        // atlas batch), so these are for a handful of large images -- the logo,
        // the roster portraits, the background plate, the board objects -- not
        // for anything drawn per tile in bulk.
        enum class SpriteLayer
        {
            Background, // behind everything
            World,      // with the board, under the cell layer
            Ui          // over the cell layer
        };

        void sprite(const sf::Texture& texture, float x, float y, float w, float h,
            SpriteLayer layer = SpriteLayer::Ui, Color tint = Color::White, bool additive = false);

        // Scales to fit inside the box while preserving aspect, then centres it.
        void spriteFitted(const sf::Texture& texture, float x, float y, float w, float h,
            SpriteLayer layer = SpriteLayer::Ui, Color tint = Color::White, bool additive = false);

        Textures& textures() { return m_textures; }

        // Sets the window icon. Ignored when there is no window.
        void setIcon(const sf::Texture& texture);

    private:
        // Maps a window pixel onto the virtual canvas and hands it to Input.
        void setMouseFromPixel(Input& input, sf::Vector2i pixel);

        void buildTargets(const std::wstring& title, bool windowed);
        void appendQuad(sf::VertexArray& batch, float x, float y, float w, float h,
            const sf::FloatRect& texRect, Color colour);
        void flushBatches(sf::RenderTarget& target);
        void applyLetterbox();

        int m_cellsWide{ 0 };
        int m_cellsHigh{ 0 };

        GlyphAtlas m_atlas;
        sf::FloatRect m_solidRect;   // fully opaque atlas slot, used for fills

        std::unique_ptr<sf::RenderWindow> m_window;
        DisplayMode m_displayMode{ DisplayMode::Windowed };
        bool m_vsync{ true };
        std::unique_ptr<sf::RenderTexture> m_target;   // always present; the canvas
        bool m_offscreen{ false };
        std::wstring m_title;

        sf::VertexArray m_pixelBatch;
        sf::VertexArray m_glowBatch;
        sf::VertexArray m_cellBatch;
        sf::VertexArray m_overlayBatch;
        sf::VertexArray m_overlayGlowBatch;

        // Shared by every glow: stacked additive rings, no shader needed.
        void appendGlowRings(sf::VertexArray& batch, float x, float y, float w, float h,
            const sf::FloatRect& texRect, Color colour, float spread, float intensity);

        struct SpriteDraw
        {
            const sf::Texture* texture{ nullptr };
            float x{ 0.0f };
            float y{ 0.0f };
            float w{ 0.0f };
            float h{ 0.0f };
            Color tint{ Color::White };
            bool additive{ false };
        };

        std::vector<SpriteDraw> m_backgroundSprites;
        std::vector<SpriteDraw> m_worldSprites;
        std::vector<SpriteDraw> m_uiSprites;

        void drawSprites(sf::RenderTarget& target, const std::vector<SpriteDraw>& sprites) const;

        Textures m_textures;

        Color m_clearColour{ Color::Black };

        // Mirror of the cell layer for --uidump.
        std::vector<wchar_t> m_cellGlyphs;
    };
}
