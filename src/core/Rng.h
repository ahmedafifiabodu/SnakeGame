#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace neoncoil
{
    // Seeded random source. Every gameplay-relevant random decision goes through
    // an Rng so that a run (and any individual level) can be reproduced from its
    // seed alone -- see App::parseArguments and LevelGenerator::levelSeed.
    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed) : m_seed(seed), m_engine(static_cast<std::mt19937::result_type>(seed)) {}

        std::uint64_t seed() const { return m_seed; }

        void reseed(std::uint64_t seed)
        {
            m_seed = seed;
            m_engine.seed(static_cast<std::mt19937::result_type>(seed));
        }

        // Inclusive on both ends. Returns lo when the range is degenerate.
        int range(int lo, int hi)
        {
            if (hi <= lo)
                return lo;
            return std::uniform_int_distribution<int>(lo, hi)(m_engine);
        }

        float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_engine); }

        bool chance(float probability) { return unit() < probability; }

        template <typename T>
        const T& pick(const std::vector<T>& items)
        {
            return items[static_cast<std::size_t>(range(0, static_cast<int>(items.size()) - 1))];
        }

        // Non-cryptographic mix used to derive independent, reproducible
        // sub-seeds (run seed + level index -> that level's seed).
        static std::uint64_t mix(std::uint64_t a, std::uint64_t b)
        {
            std::uint64_t h = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33;
            h *= 0xc4ceb9fe1a85ec53ull;
            h ^= h >> 33;
            return h;
        }

        static std::uint64_t seedFromClock();

    private:
        std::uint64_t m_seed;
        std::mt19937 m_engine;
    };
}
