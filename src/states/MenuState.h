#pragma once

#include "GameState.h"
#include "../ui/Hit.h"

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
            Start,        // offline, no connection and no account needed
            Multiplayer,
            Options,
            Count
        };

        // Ids above the Field range, so one map covers both "focus this field"
        // and the controls that act directly.
        enum Hit : int
        {
            HitColourSwatch = 100,   // + swatch index
            HitTypePrev = 200,
            HitTypeNext = 201
        };

        void handleNameEntry(AppContext& context);
        void adjust(AppContext& context, int delta);
        Transition handleMouse(AppContext& context);
        Transition confirmField(AppContext& context);

        void renderTitle(AppContext& context) const;
        void renderConfigPanel(AppContext& context) const;
        void renderPortrait(AppContext& context) const;
        void renderTypePanel(AppContext& context) const;

        Field m_field{ Field::Name };
        float m_elapsed{ 0.0f };
        bool m_nameTouched{ false };

        // Filled while rendering, read on the next update.
        mutable ui::HitMap m_hits;
    };
}
