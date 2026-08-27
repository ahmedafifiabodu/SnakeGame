#include "MatchSnapshot.h"

namespace neoncoil
{
    const SnakeSnapshot* MatchSnapshot::find(PlayerSlot slot) const
    {
        for (const SnakeSnapshot& snake : snakes)
            if (snake.slot == slot)
                return &snake;
        return nullptr;
    }
}
