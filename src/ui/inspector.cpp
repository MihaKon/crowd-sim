#include "ui/inspector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

constexpr uint32_t kNone     = 0xFFFFFFFFu;
constexpr uint32_t kSpawn    = 0xFFFFFFFEu;
constexpr uint32_t kMin      = 60000u;
constexpr uint32_t kDay      = 1440u * kMin;
constexpr uint32_t kPatience = 4u; // must match agents.comp

// Mirrors of agents.comp: the panel recomputes what the GPU decided, so these must stay bit-identical.
uint32_t pcg(uint32_t v) {
    const uint32_t s = v * 747796405u + 2891336453u;
    const uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float rnd(uint32_t& s) {
    s = pcg(s);
    return float(s) * (1.0f / 4294967296.0f);
}
uint32_t traits(const InspectContext& c, uint32_t id) { return pcg(id * 0x9E3779B9u ^ c.seed); }
float    walkSpeed(const InspectContext& c, uint32_t id) {
    return 1.1f + 0.6f * float(traits(c, id) & 0xFFFFu) / 65535.0f;
}
uint32_t dayOf(const InspectContext& c, uint32_t t) { return (t + c.dayOffsetMs) / kDay; }
uint32_t dayRng(const InspectContext& c, uint32_t id, uint32_t t, uint32_t salt) {
    return pcg(traits(c, id) ^ (dayOf(c, t) * 0x85EBCA6Bu) ^ salt);
}

struct World {
    const InspectContext& c;
    uint32_t w(Section s, size_t i) const { return c.sim.data[c.sim.sec[size_t(s)] + i]; }
    float    f(Section s, size_t i) const {
        float v;
        uint32_t u = w(s, i);
        std::memcpy(&v, &u, 4);
        return v;
    }
    Vec2     node(uint32_t n) const { return {f(kSecNodePos, 2 * n), f(kSecNodePos, 2 * n + 1)}; }
    uint32_t block(uint32_t b, int k) const { return w(kSecBlocks, 4 * size_t(b) + size_t(k)); }
    uint32_t lineDir(uint32_t ld, int k) const { return w(kSecLineDir, 8 * size_t(ld) + size_t(k)); }
    uint32_t profStation(uint32_t k) const { return w(kSecProfile, 4 * size_t(k)); }
    float    railTime(uint32_t a, uint32_t b) const { return f(kSecRailTable, 2 * (size_t(a) * c.sim.stationCount + b)); }
    uint32_t railLeg(uint32_t a, uint32_t b) const { return w(kSecRailTable, 2 * (size_t(a) * c.sim.stationCount + b) + 1); }
};

std::string clock(const InspectContext& c, uint32_t t) {
    const uint32_t m = (t + c.dayOffsetMs) / kMin % 1440;
    char           b[8];
    std::snprintf(b, sizeof b, "%02u:%02u", m / 60, m % 60);
    return b;
}

std::string km(float metres) {
    char b[16];
    if (metres < 1000.0f) std::snprintf(b, sizeof b, "%.0f m", double(metres));
    else std::snprintf(b, sizeof b, "%.1f km", double(metres) / 1000.0);
    return b;
}

uint32_t nearestStation(const InspectContext& c, Vec2 p) {
    uint32_t best = kNone;
    float    bd   = 1e30f;
    for (uint32_t s = 0; s < c.map.stations.size(); ++s) {
        const float d = length2(c.map.stations[s].pos - p);
        if (d < bd) {
            bd   = d;
            best = s;
        }
    }
    return best;
}

std::string near(const InspectContext& c, Vec2 p) {
    const uint32_t s = nearestStation(c, p);
    if (s == kNone) return "somewhere in the city";
    return km(length(c.map.stations[s].pos - p)) + " from " + c.map.stations[s].name + " St.";
}

std::string stationName(const InspectContext& c, uint32_t s) {
    return s < c.map.stations.size() ? c.map.stations[s].name : std::string("?");
}

const char* blockKind(uint32_t type) {
    switch (BlockType(type)) {
    case BlockType::Residential: return "residential block";
    case BlockType::Commercial:  return "shops";
    case BlockType::Office:      return "office block";
    case BlockType::Industrial:  return "industrial area";
    case BlockType::Park:        return "park";
    case BlockType::ResidentialPoor: return "tenement block";
    case BlockType::ResidentialRich: return "villa";
    }
    return "block";
}

std::string rideName(const InspectContext& c, const World& w, uint32_t ld) {
    const uint32_t line = ld / 2;
    if (line >= c.map.lines.size()) return "train";
    const RailLine& l = c.map.lines[line];
    if (l.loop) return l.name + ((ld & 1u) ? " (clockwise)" : " (counter-clockwise)");
    const uint32_t last = w.lineDir(ld, 0) + w.lineDir(ld, 1) - 1;
    return l.name + " towards " + stationName(c, w.profStation(last));
}

const char* kFamily[] = {"Sato",     "Suzuki",   "Takahashi", "Tanaka",    "Watanabe", "Ito",      "Yamamoto",
                         "Nakamura", "Kobayashi", "Kato",     "Yoshida",   "Yamada",   "Sasaki",   "Yamaguchi",
                         "Matsumoto", "Inoue",   "Kimura",    "Hayashi",   "Shimizu",  "Yamazaki", "Mori",
                         "Abe",      "Ikeda",    "Hashimoto", "Ishikawa",  "Ogawa",    "Fujita",   "Okada",
                         "Goto",     "Hasegawa", "Murakami",  "Kondo",     "Ishii",    "Saito",    "Sakamoto"};
const char* kGiven[]  = {"Haruto", "Yuto",  "Sota",   "Riku",   "Hinata", "Minato", "Yui",    "Hina",  "Aoi",
                         "Rin",    "Sakura", "Mei",   "Kenji",  "Takeshi", "Hiroshi", "Akiko", "Yuko",  "Naoki",
                         "Emi",    "Daiki", "Kaito",  "Mio",    "Ren",    "Sora",   "Yuna",   "Koharu", "Kazuki",
                         "Ryo",    "Ayaka", "Mai",    "Shota",  "Nanami", "Taro",   "Hanako", "Kenta",  "Misaki"};
// By pay level and kind of workplace.
const char* kLowOffice[]  = {"security guard", "janitor", "receptionist", "call centre agent"};
const char* kLowShop[]    = {"shop clerk", "cashier", "waiter", "cook", "cleaner", "delivery rider"};
const char* kLowFactory[] = {"factory worker", "warehouse staff", "forklift driver", "dock worker", "packer"};
const char* kMidOffice[]  = {"office worker", "engineer", "accountant", "designer", "civil servant", "sales manager"};
const char* kMidShop[]    = {"store manager", "pharmacist", "restaurant owner", "real estate agent"};
const char* kMidFactory[] = {"foreman", "technician", "logistics planner", "quality inspector"};
const char* kHighOffice[] = {"executive", "investment banker", "corporate lawyer", "consultant",
                             "software architect", "fund manager"};

std::string yen(uint32_t v) {
    std::string d = std::to_string(v), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return "JPY " + out;
}

template <size_t N> const char* choose(const char* (&list)[N], uint32_t h) { return list[h % N]; }

} // namespace

Panel describeAgent(const InspectContext& c, const AgentRaw& a, const CarRaw* car) {
    const World    w{c};
    const uint32_t h      = traits(c, a.id);
    // plan[1]: workplace block, pay level in the top 2 bits.
    const bool     worker = a.plan[1] != kNone;
    const uint32_t home = a.plan[0], work = worker ? (a.plan[1] & 0x3FFFFFFFu) : kNone;
    const uint32_t tier = worker ? a.plan[1] >> 30 : 0;
    const uint32_t homeType = w.block(home, 3);
    const int      cls = homeType == uint32_t(BlockType::ResidentialPoor)   ? 0
                       : homeType == uint32_t(BlockType::ResidentialRich) ? 2
                                                                           : 1;

    Panel p;
    p.title = std::string(choose(kGiven, pcg(h ^ 0x1111u))) + " " + choose(kFamily, pcg(h ^ 0x2222u));

    uint32_t    age = 0, income = 0;
    std::string job;
    const uint32_t r = pcg(h ^ 0x4444u), money = pcg(h ^ 0x6666u) % 1000;
    if (worker) {
        age = 22 + pcg(h ^ 0x3333u) % 43;
        const uint32_t type    = w.block(work, 3);
        const bool     factory = type == uint32_t(BlockType::Industrial);
        const bool     shop    = type == uint32_t(BlockType::Commercial);
        if (tier == 2) {
            job    = factory ? "factory owner" : shop ? "department store owner" : choose(kHighOffice, r);
            income = factory || shop ? 3'000'000 + money * 6'000 : 700'000 + money * 1'100;
        } else if (tier == 1) {
            job    = factory ? choose(kMidFactory, r) : shop ? choose(kMidShop, r) : choose(kMidOffice, r);
            income = 300'000 + money * 250;
        } else {
            job    = factory ? choose(kLowFactory, r) : shop ? choose(kLowShop, r) : choose(kLowOffice, r);
            income = 180'000 + money * 80;
        }
    } else {
        const uint32_t k = pcg(h ^ 0x5555u) % 100;
        if (k < (cls == 0 ? 25u : 30u)) {
            age = 15 + pcg(h ^ 0x3333u) % 8;
            job = "student";
        } else if (k < (cls == 0 ? 70u : cls == 1 ? 45u : 40u)) {
            age = 22 + pcg(h ^ 0x3333u) % 40;
            job = cls == 2 ? "lives off investments" : "unemployed";
            income = cls == 2 ? 900'000 + money * 3'000 : 0;
        } else if (k < (cls == 0 ? 85u : cls == 1 ? 70u : 65u)) {
            age = 28 + pcg(h ^ 0x3333u) % 33;
            job = "homemaker";
        } else {
            age    = 65 + pcg(h ^ 0x3333u) % 25;
            job    = "retired";
            income = cls == 0 ? 90'000 + money * 40 : cls == 1 ? 160'000 + money * 80 : 350'000 + money * 400; // pension
        }
    }
    char sub[96];
    std::snprintf(sub, sizeof sub, "%u, %s  #%u", age, job.c_str(), a.id);
    p.subtitle = sub;

    const uint32_t act = a.state & 15u, kind = (a.state >> 4) & 7u, hops = (a.state >> 8) & 255u;
    const uint32_t stn = a.state >> 16;
    const char*    purpose = act == 1 ? "to work" : act == 3 ? "to the shops" : "home";
    std::string    now, next;
    switch (kind) {
    case 0: // indoors
        if (act == 0) {
            now  = "At home";
            next = "leaves at " + clock(c, a.ev);
        } else if (act == 2) {
            uint32_t s = dayRng(c, a.id, a.ev, 3u);
            now  = "At work";
            next = "until " + clock(c, a.ev) + (rnd(s) < 0.35f ? ", then shopping" : ", then home");
        } else {
            now  = "Shopping";
            next = "until " + clock(c, a.ev) + ", then home";
        }
        break;
    case 1: // walking
        if ((a.state & 128u) != 0) now = "Walking to " + stationName(c, stn) + " St. (" + purpose + ")";
        else now = std::string("Walking ") + purpose;
        next = km(length(w.node(a.leg[3]) - w.node(a.leg[1]))) + " to go (straight line)";
        break;
    case 2: { // platform
        const uint32_t ld = a.leg[0] & 0xFFFu;
        now  = "On the platform at " + stationName(c, stn) + " St., " + purpose;
        next = rideName(c, w, ld) + " leaves " + clock(c, a.ev);
        if (hops > 0) next += ", left behind by " + std::to_string(hops) + " full train" + (hops > 1 ? "s" : "") +
                              " (gives up after " + std::to_string(kPatience) + ")";
        break;
    }
    case 3: { // train
        const uint32_t ld = a.leg[0] & 0xFFFu;
        now  = "On the " + rideName(c, w, ld) + ", " + purpose;
        next = "gets off at " + stationName(c, stn) + " St. at " + clock(c, a.ev);
        break;
    }
    case 4: // car
        now = std::string("Driving ") + purpose;
        if (car) {
            char b[64];
            if (car->lane == kSpawn) next = "waiting to get onto the road";
            else if (car->kin[1] < 0.5f) {
                std::snprintf(b, sizeof b, "standing for %.0f s", double(car->kin[2]));
                next = b;
            } else {
                std::snprintf(b, sizeof b, "%.0f km/h", double(car->kin[1]) * 3.6);
                next = b;
            }
            next += ", " + km(length(w.node(car->route[0]) - w.node(car->route[1] == kNone ? car->route[0] : car->route[1]))) +
                    " trip";
        }
        break;
    default: now = "?";
    }
    p.rows.push_back({"Now", now});
    if (!next.empty()) p.rows.push_back({"", next});

    const Vec2 hp = w.node(w.block(home, 0));
    auto district = [&](uint32_t blk) {
        const uint16_t d = c.map.blocks[blk].district;
        return d < c.map.districts.size() ? c.map.districts[d].name : std::string("?");
    };
    p.rows.push_back({"Home", std::string(blockKind(w.block(home, 3))) + " in " + district(home) + ", " + near(c, hp)});
    if (worker) {
        const Vec2 wp = w.node(w.block(work, 0));
        p.rows.push_back({"Work", std::string(blockKind(w.block(work, 3))) + " in " + district(work) + ", " + near(c, wp)});
    }
    const uint32_t shop = w.block(home, 2);
    p.rows.push_back({"Shops", near(c, w.node(w.block(shop, 0)))});
    static const char* kClass[] = {"working class", "middle class", "wealthy"};
    p.rows.push_back({"Class", kClass[cls]});
    p.rows.push_back({"Income", income ? yen(income) + " / month" : std::string("none")});

    // Usual commute: same rule as startTrip in agents.comp.
    const uint32_t target = worker ? work : shop;
    const Vec2     tp     = w.node(w.block(target, 0));
    const float    d      = length(tp - hp);
    const float    v      = walkSpeed(c, a.id);
    std::string    mode;
    if (a.id % c.ownerEvery == 0 && d > 1200.0f) {
        mode = "by car";
    } else {
        const uint32_t sa = w.block(home, 1), sb = w.block(target, 1);
        bool           train = false;
        if (c.sim.stationCount > 0 && d > 1500.0f && sa != sb) {
            const float access = (length(c.map.stations[sa].pos - hp) + length(tp - c.map.stations[sb].pos)) * 1.3f / v;
            train = access + w.railTime(sa, sb) + 150.0f < d * 1.3f / v;
        }
        if (train) {
            const uint32_t leg  = w.railLeg(sa, sb);
            const uint32_t line = leg >> 16;
            mode = "by train from " + stationName(c, sa) + " St." +
                   (line < c.map.lines.size() ? " (" + c.map.lines[line].name + ")" : "");
        } else {
            mode = "on foot";
        }
    }
    p.rows.push_back({worker ? "Commute" : "Errands", km(d) + " " + mode});
    char walk[32];
    std::snprintf(walk, sizeof walk, "%.1f km/h", double(v) * 3.6);
    p.rows.push_back({"Walks", walk});
    return p;
}

Panel describeCar(const InspectContext& c, const CarRaw& r) {
    const World    w{c};
    const uint32_t fleetIndex = r.car - c.ownerCount;
    const bool     van        = r.car % 10u < 7u; // traffic.comp: 70% vans
    Panel          p;
    p.title    = std::string(van ? "Delivery van" : "Taxi") + " #" + std::to_string(fleetIndex + 1);
    p.subtitle = van ? "on the road 07:00-19:00" : "on the road day and night";

    char b[64];
    if (r.lane == kNone) {
        p.rows.push_back({"Now", "Parked, " + near(c, w.node(r.route[1]))});
        p.rows.push_back({"", "leaves at " + clock(c, r.route[3])});
    } else {
        if (r.lane == kSpawn) std::snprintf(b, sizeof b, "Waiting to get onto the road");
        else if (r.kin[1] < 0.5f) std::snprintf(b, sizeof b, "Standing for %.0f s", double(r.kin[2]));
        else std::snprintf(b, sizeof b, "Driving at %.0f km/h", double(r.kin[1]) * 3.6);
        p.rows.push_back({"Now", b});
        p.rows.push_back({"To", near(c, w.node(r.route[0]))});
        std::snprintf(b, sizeof b, "%.0f streets so far", double(r.kin[3]));
        p.rows.push_back({"Trip", b});
    }
    return p;
}
