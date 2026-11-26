#pragma once

#include <algorithm>
#include "../../quake/quakedef.h"

extern "C" {
    extern cvar_t scr_overscan_x;
    extern cvar_t scr_overscan_y;
}

namespace ps2::rs {

    inline void GetOverscanInset(const int width, const int height, int & ix, int & iy)
    {
        const float fx = std::clamp(scr_overscan_x.value, 0.0f, 0.25f);
        const float fy = std::clamp(scr_overscan_y.value, 0.0f, 0.25f);
        ix = static_cast<int>(static_cast<float>(width) * fx + 0.5f);
        iy = static_cast<int>(static_cast<float>(height) * fy + 0.5f);
    }
}
