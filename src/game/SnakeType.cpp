#include "SnakeType.h"

#include "../core/Glyphs.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        // The roster. Speeds are multipliers on the level's base tick rate, so
        // difficulty scaling and snake choice compose instead of fighting.
        const std::vector<SnakeType> kSnakeTypes = {
            SnakeType{
                L"VIPER",
                L"Fast, fragile, unforgiving",
                { L"Speed  x1.25", L"Growth +1 per food", L"No safety net" },
                glyph::Block, glyph::Block, 0,
                Color::Green,
                1.25f, 4, 1, 1.0f,
                AbilityDef{
                    AbilityKind::Dash,
                    L"DASH",
                    L"2s of double speed. Eating mid-dash extends your combo.",
                    8.0f, 2.0f
                }
            },
            SnakeType{
                L"BULWARK",
                L"Slow, armoured, forgiving",
                { L"Speed  x0.85", L"Starts 5 segments long", L"Survives one mistake" },
                glyph::Block, glyph::ShadeDark, 0,
                Color::Blue,
                0.85f, 5, 1, 1.0f,
                AbilityDef{
                    AbilityKind::IronScales,
                    L"IRON SCALES",
                    L"Absorbs the next lethal hit. Shatters the wall it hits.",
                    15.0f, 0.0f
                }
            },
            SnakeType{
                L"WRAITH",
                L"Walks through the level, not around it",
                { L"Speed  x1.00", L"Ignores walls while phased", L"Solidifying inside a wall kills" },
                glyph::ShadeMedium, glyph::ShadeLight, 0,
                Color::Aqua,
                1.0f, 4, 1, 1.0f,
                AbilityDef{
                    AbilityKind::Phase,
                    L"PHASE",
                    L"2.5s passing through walls and your own body.",
                    12.0f, 2.5f
                }
            },
            SnakeType{
                L"MIDAS",
                L"Clears targets fast, pays for it in length",
                { L"Speed  x1.00", L"Growth +2 per food", L"Base score x1.2" },
                glyph::Block, glyph::ShadeMedium, 0,
                Color::Gold,
                1.0f, 4, 2, 1.2f,
                AbilityDef{
                    AbilityKind::GoldRush,
                    L"GOLD RUSH",
                    L"5s of triple-value food. Every bite drops a bonus fruit.",
                    18.0f, 5.0f
                }
            },
            SnakeType{
                L"OUROBOROS",
                L"Length is a resource, not a burden",
                { L"Speed  x1.10", L"Growth +1 per food", L"Trades tail for points" },
                glyph::Block, glyph::Block, glyph::ShadeDark,
                Color::Magenta,
                1.10f, 4, 1, 1.0f,
                AbilityDef{
                    AbilityKind::Shed,
                    L"SHED",
                    L"Drop half your tail instantly. Banks 5 points a segment.",
                    10.0f, 0.0f
                }
            }
        };
    }

    const std::vector<SnakeType>& snakeTypes()
    {
        return kSnakeTypes;
    }

    int snakeTypeCount()
    {
        return static_cast<int>(kSnakeTypes.size());
    }

    const SnakeType& snakeTypeAt(int index)
    {
        const int clamped = std::clamp(index, 0, snakeTypeCount() - 1);
        return kSnakeTypes[static_cast<std::size_t>(clamped)];
    }
}
