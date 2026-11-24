// Goal: Share the touchscreen menu's color palette.
// Responsibilities: Define RGB565 colors for navigation, actions, and danger.
// How it works:
// 1. Menu code selects a color for a button's role.
// 2. Display drawing uses the corresponding numeric color.
// 3. The shared constants keep menu roles visually consistent.
#pragma once

#include <stdint.h>

namespace menu_colors {
inline constexpr uint16_t navigation = 0x0253;
inline constexpr uint16_t action = 0x0326;
inline constexpr uint16_t danger = 0xB004;
}  // namespace menu_colors
