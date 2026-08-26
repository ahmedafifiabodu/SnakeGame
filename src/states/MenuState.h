#pragma once

#include "GameState.h"

namespace neoncoil
{
    // Front end: name entry, colour choice and snake selection, with a live
    // preview of the snake you are about to play.
    class MenuState : public GameState
    {
    public:
        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

    private:
        enum class Field
        {
            Name,
            Colour,
            Type,
            Start,
            Count
        };

        void handleNameEntry(AppContext& context);
        void adjust(AppContext& context, int delta);

        void renderTitle(AppContext& context) const;
        void renderConfigPanel(AppContext& context) const;
        void renderPortrait(AppContext& context) const;
        void renderTypePanel(AppContext& context) const;

        Field m_field{ Field::Name };
        float m_elapsed{ 0.0f };
        bool m_nameTouched{ false };
    };
}
