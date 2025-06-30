#pragma once
#include <cstdint>

// ikonok kitöltése a saját bitmap adataiddal
extern const uint8_t iconInstSpeed[];
extern const uint16_t iconInstSpeedWidth;
extern const uint16_t iconInstSpeedHeight;

extern const uint8_t iconSpeed[];
extern const uint16_t iconSpeedWidth;
extern const uint16_t iconSpeedHeight;

// napi és teljes távolság ikonjai (azonos bitmapp)
extern const uint8_t iconDistance[];
extern const uint16_t iconDistanceWidth;
extern const uint16_t iconDistanceHeight;

// aliasok
#define iconDaily     iconDistance
#define iconDailyWidth  iconDistanceWidth
#define iconDailyHeight iconDistanceHeight

#define iconTotal     iconDistance
#define iconTotalWidth  iconDistanceWidth
#define iconTotalHeight iconDistanceHeight
