#pragma once

#include "GameState.h"
#include "../ui/Hit.h"

namespace neoncoil
{
    // Display, audio and comfort settings.
    //
    // Every change is applied the moment it is made and written to disk in the
    // same breath -- there is no APPLY button and no CANCEL. A player who turns
    // on borderless and then alt-tabs away has already had their choice saved,
    // and one who picks a mode that looks wrong can see that it looks wrong
    // immediately rather than after confirming it.
    class OptionsState : public GameState
    {
    public:
        void onEnter(AppContext& context) override;
        Transition update(AppContext& context, float deltaSeconds) override;
        void render(AppContext& context) override;

    private:
        enum class Row
        {
            DisplayMode,
            VerticalSync,

            MasterVolume,
            MusicVolume,
            EffectsVolume,

            ScreenShake,
            Bloom,
            ShowPing,

            Back,
            Count
        };

        enum Hit : int
        {
            HitPrev = 200,   // + row index
            HitNext = 300
        };

        // Left/right on the focused row. `delta` is -1 or +1; rows that are
        // toggles ignore its size and rows that are volumes step by five.
        void adjust(AppContext& context, int delta);
        Transition activate(AppContext& context);
        Transition handleMouse(AppContext& context);

        // True for the rows this build cannot honour. They are shown greyed
        // with a reason rather than hidden: a player looking for the volume
        // should find out that there is no sound yet, not conclude the game has
        // no options screen.
        static bool rowEnabled(Row row);
        static const wchar_t* rowLabel(Row row);
        std::wstring rowValue(const AppContext& context, Row row) const;
        static const wchar_t* rowHint(Row row);

        Row m_row{ Row::DisplayMode };
        float m_elapsed{ 0.0f };
        std::wstring m_message;

        mutable ui::HitMap m_hits;
    };
}
