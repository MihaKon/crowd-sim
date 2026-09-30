#pragma once
#include <cstdint>

// Tokyo Night based colours, 0xRRGGBB.
namespace palette {

constexpr uint32_t kBackground  = 0x1a1b26;
constexpr uint32_t kWater       = 0x16203a;
constexpr uint32_t kResidential = 0x283038;
constexpr uint32_t kCommercial  = 0x2e2c3a;
constexpr uint32_t kOffice      = 0x272c38;
constexpr uint32_t kIndustrial  = 0x2c2d31;
constexpr uint32_t kPark        = 0x1f2e2a;
constexpr uint32_t kResidentialPoor = 0x332f2c;
constexpr uint32_t kResidentialRich = 0x25352c;
constexpr uint32_t kLocalRoad   = 0x3b4261;
constexpr uint32_t kArterial    = 0x565f89;
constexpr uint32_t kStation     = 0xc0caf5;

constexpr uint32_t kBar      = 0x16161e;
constexpr uint32_t kPanel    = 0x1f2335;
constexpr uint32_t kFg       = 0xc0caf5;
constexpr uint32_t kFgDim    = 0xa9b1d6;
constexpr uint32_t kComment  = 0x565f89;
constexpr uint32_t kBlue     = 0x7aa2f7;
constexpr uint32_t kCyan     = 0x7dcfff;
constexpr uint32_t kGreen    = 0x9ece6a;
constexpr uint32_t kYellow   = 0xe0af68;
constexpr uint32_t kOrange   = 0xff9e64;
constexpr uint32_t kRed      = 0xf7768e;
constexpr uint32_t kMagenta  = 0xbb9af7;

// UI: dark translucent cards that read over the city by day and by night.
namespace ui {
constexpr uint32_t kCard     = 0x141821;
constexpr float    kCardA    = 0.84f;
constexpr uint32_t kKey      = 0x2b313d; // keycap
constexpr uint32_t kText     = 0xf3f5f8;
constexpr uint32_t kDim      = 0xaab2c0;
constexpr uint32_t kFaint    = 0x6e7787;
constexpr uint32_t kAccent   = 0xffc04d;
constexpr uint32_t kWalk     = 0x5b9cf5;
constexpr uint32_t kPlatform = 0x3cc6d8;
constexpr uint32_t kTrain    = 0x7bd389;
constexpr uint32_t kDrive    = 0xf29d4b;
constexpr uint32_t kHome     = 0xc5cad3;
constexpr uint32_t kWork     = 0x93a1b8;
constexpr uint32_t kShop     = 0xd58cf0;
constexpr uint32_t kStopped  = 0xef5b5b;
constexpr uint32_t kNightBar = 0x39425c;
constexpr uint32_t kDayBar   = 0xe8c872;
// Data layer ramps, must match heat.frag and congestion.vert.
constexpr uint32_t kDensityRamp[6] = {0x2f2f7a, 0x3a6fd8, 0x36c0d2, 0xf1d54c, 0xf28c38, 0xe8455a};
constexpr uint32_t kTrafficRamp[3] = {0x4cc38a, 0xf2b43c, 0xe5484d};
} // namespace ui

// Rail lines; index 0 is the ring line.
constexpr uint32_t kRail[] = {0x9ece6a, 0xff9e64, 0x7dcfff, 0xbb9af7, 0xf7768e,
                              0x73daca, 0x7aa2f7, 0xe0af68, 0x2ac3de};
constexpr int kRailCount = int(sizeof(kRail) / sizeof(kRail[0]));

}
