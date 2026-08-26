#include "Snake.h"

#include <algorithm>

namespace neoncoil
{
    namespace
    {
        constexpr std::size_t kMaxQueuedTurns = 2;
    }

    void Snake::reset(Vec2 boardSize, Vec2 head, Direction direction, int length)
    {
        m_boardSize = boardSize;
        m_occupancy.assign(static_cast<std::size_t>(std::max(0, boardSize.x)) *
                           static_cast<std::size_t>(std::max(0, boardSize.y)), 0);

        m_body.clear();
        m_queuedTurns.clear();
        m_direction = direction;
        m_pendingGrowth = 0;

        const Vec2 back = toDelta(direction) * -1;
        Vec2 segment = head;

        for (int i = 0; i < std::max(1, length); ++i)
        {
            // Stop early rather than wrap: the level generator guarantees a
            // clear spawn corridor, but a hand-authored board might not.
            if (segment.x < 0 || segment.x >= boardSize.x || segment.y < 0 || segment.y >= boardSize.y)
                break;

            m_body.push_back(segment);
            addOccupancy(segment);
            segment += back;
        }

        if (m_body.empty())
        {
            m_body.push_back(head);
            addOccupancy(head);
        }
    }

    Direction Snake::nextDirection() const
    {
        return m_queuedTurns.empty() ? m_direction : m_queuedTurns.front();
    }

    void Snake::queueDirection(Direction direction)
    {
        // Validate against the last thing the snake will actually be doing, not
        // against its current heading, or a buffered pair could reverse it.
        const Direction reference = m_queuedTurns.empty() ? m_direction : m_queuedTurns.back();

        if (direction == reference || isOpposite(direction, reference))
            return;

        if (m_queuedTurns.size() >= kMaxQueuedTurns)
            return;

        m_queuedTurns.push_back(direction);
    }

    void Snake::commitStep()
    {
        if (!m_queuedTurns.empty())
        {
            m_direction = m_queuedTurns.front();
            m_queuedTurns.pop_front();
        }

        const Vec2 newHead = head() + toDelta(m_direction);

        m_body.push_front(newHead);
        addOccupancy(newHead);

        if (m_pendingGrowth > 0)
        {
            --m_pendingGrowth;
        }
        else
        {
            removeOccupancy(m_body.back());
            m_body.pop_back();
        }
    }

    bool Snake::occupies(Vec2 position) const
    {
        const int index = occupancyIndex(position);
        return index >= 0 && m_occupancy[static_cast<std::size_t>(index)] > 0;
    }

    bool Snake::occupiesAfterStep(Vec2 position) const
    {
        if (!occupies(position))
            return false;

        if (m_pendingGrowth > 0)
            return true;

        // The tail vacates this tick. It is only safe to move into if no other
        // segment also sits there (possible after phasing through yourself).
        const int index = occupancyIndex(position);
        const bool onlyTailOccupies = position == m_body.back() &&
            m_occupancy[static_cast<std::size_t>(index)] == 1;

        return !onlyTailOccupies;
    }

    std::vector<Vec2> Snake::shedTo(int keepLength)
    {
        std::vector<Vec2> removed;
        const int target = std::max(1, keepLength);

        while (length() > target)
        {
            removed.push_back(m_body.back());
            removeOccupancy(m_body.back());
            m_body.pop_back();
        }

        return removed;
    }

    int Snake::occupancyIndex(Vec2 position) const
    {
        if (position.x < 0 || position.x >= m_boardSize.x || position.y < 0 || position.y >= m_boardSize.y)
            return -1;
        return position.y * m_boardSize.x + position.x;
    }

    void Snake::addOccupancy(Vec2 position)
    {
        const int index = occupancyIndex(position);
        if (index >= 0)
            ++m_occupancy[static_cast<std::size_t>(index)];
    }

    void Snake::removeOccupancy(Vec2 position)
    {
        const int index = occupancyIndex(position);
        if (index >= 0 && m_occupancy[static_cast<std::size_t>(index)] > 0)
            --m_occupancy[static_cast<std::size_t>(index)];
    }
}
