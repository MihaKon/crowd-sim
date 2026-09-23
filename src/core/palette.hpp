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

// Rail lines; index 0 is the ring line.
constexpr uint32_t kRail[] = {0x9ece6a, 0xff9e64, 0x7dcfff, 0xbb9af7, 0xf7768e,
                              0x73daca, 0x7aa2f7, 0xe0af68, 0x2ac3de};
constexpr int kRailCount = int(sizeof(kRail) / sizeof(kRail[0]));

}
