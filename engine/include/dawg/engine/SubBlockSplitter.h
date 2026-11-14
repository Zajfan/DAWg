#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace dawg::engine {

class SubBlockSplitter final
{
public:
    static constexpr std::size_t kMaxSplitPoints = 64;

    // Splits [0, totalFrames) at every valid split point in (0, totalFrames),
    // sorting and deduplicating on a fixed stack array with zero heap allocation.
    template <typename Callback>
    static void split(const std::size_t totalFrames,
                      const std::span<const std::size_t> splitOffsets,
                      Callback&& callback) noexcept
    {
        if (totalFrames == 0)
            return;

        std::array<std::size_t, kMaxSplitPoints> points {};
        std::size_t count = 0;

        for (const std::size_t offset : splitOffsets)
        {
            if (offset == 0 || offset >= totalFrames)
                continue;
            if (count < kMaxSplitPoints)
                points[count++] = offset;
        }

        if (count == 0)
        {
            callback(std::size_t {0}, totalFrames);
            return;
        }

        std::sort(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(count));
        const auto uniqueEnd = std::unique(
            points.begin(), points.begin() + static_cast<std::ptrdiff_t>(count));
        const auto uniqueCount = static_cast<std::size_t>(uniqueEnd - points.begin());

        std::size_t current = 0;
        for (std::size_t i = 0; i < uniqueCount; ++i)
        {
            const std::size_t boundary = points[i];
            if (boundary > current)
            {
                callback(current, boundary - current);
                current = boundary;
            }
        }

        if (current < totalFrames)
            callback(current, totalFrames - current);
    }
};

} // namespace dawg::engine
