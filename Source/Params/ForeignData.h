#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace dlay::foreign
{

/** Limits for data DLAY did not write itself.

    There are two kinds: the session state a host hands back, which comes out of a project
    file, and preset files, which people pass around. Either can be damaged, or written by
    someone who means harm. Everything that comes in is therefore cut down to what DLAY
    itself could have written - a real session or preset is a few kilobytes - before any of
    it is used. (That the XML reader opens no other files and does not let text multiply
    itself is JUCE's doing; the test runner checks both.)
*/
constexpr int kMaxBytes             = 512 * 1024;   // one session, or one preset file
constexpr int kMaxNameLength        = 80;           // preset name
constexpr int kMaxCategoryLength    = 40;
constexpr int kMaxDescriptionLength = 600;
constexpr int kMaxPresetValues      = 512;          // parameter values in one preset file
constexpr int kMaxFavourites        = 2000;
constexpr int kMaxProperties        = 32;           // settings a session carries beside the parameters
constexpr int kMaxPropertyLength    = 256;

/** A number from a text file that fits a float: "nan", "inf" and 1e308 all parse, and none does. */
inline bool usableFloat (double v) noexcept
{
    return std::isfinite (v) && std::abs (v) <= (double) std::numeric_limits<float>::max();
}

/** Text as it may be shown and stored: no control characters, trimmed, not longer than asked. */
inline juce::String text (const juce::String& raw, int maxLength)
{
    // look no further than could ever be kept (a name may be megabytes long)
    const auto head = raw.substring (0, maxLength * 4 + 64);

    juce::String out;
    out.preallocateBytes ((size_t) head.getNumBytesAsUTF8() + 1);

    for (auto p = head.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        out << (c < 0x20 || c == 0x7f ? juce::juce_wchar (' ') : c);
    }

    return out.trim().substring (0, maxLength).trimEnd();
}

} // namespace dlay::foreign
