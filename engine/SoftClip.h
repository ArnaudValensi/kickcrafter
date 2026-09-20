// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace kcf
{

// The Daisy reference applies DaisySP's SoftLimit (a rational approximation of
// a soft saturator, extracted from pichenettes/stmlib). It is NOT bounded:
// for |x| > 3 it grows again (SoftLimit(64) ~= 7.15), which matters once
// several maximum-gain voices are summed. DaisySP's companion SoftClip clamps
// to +-1 beyond |x| >= 3, exactly where SoftLimit equals +-1, so the curve is
// continuous and identical to the reference within the whole musical range.
inline float softLimit (float x) noexcept
{
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float softClip (float x) noexcept
{
    if (x <= -3.0f) return -1.0f;
    if (x >= 3.0f)  return 1.0f;
    const float y = softLimit (x);   // monotonic, reaches exactly +-1 at +-3
    return y < -1.0f ? -1.0f : (y > 1.0f ? 1.0f : y);   // absorb float rounding near the bound
}

} // namespace kcf
