#pragma once
#include "dittosr/constants.hpp"

namespace nr::ditto {
// Both colour grids inherit the game's jitter in normalized screen units. Their relative jitter is zero:
// reconstruct onto that same phase, so the downstream SR still receives the phase it was told to expect.
// History reprojects between those jittered grids. NGX's jittered vectors already contain that difference.
inline void keep_output_phase(DittoConstants& c, float x, float y, uint32_t render_w, uint32_t render_h,
                              bool jittered_motion, bool reset, float previous[2]) {
    const float phase[2] = {x / render_w, y / render_h};
    c.jit[0] = c.jit[1] = 0.f;
    for (unsigned i = 0; i < 2; ++i) {
        c.jitCancel[i] = jittered_motion || reset ? 0.f : phase[i] - previous[i];
        previous[i] = phase[i];
    }
}
} // namespace nr::ditto
