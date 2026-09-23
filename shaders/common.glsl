// Shared by agents.comp and trains.vert (pasted in by #include).
// The including shader declares:
//   readonly buffer World { uint W[]; };
//   uniform uint uSec[12]; uniform uvec4 uCounts; uniform uint uDayOffset;

const uint kNone = 0xFFFFFFFFu;
const uint kMin  = 60000u;
const uint kDay  = 1440u * kMin;

const uint kFirstTrain       = 300u * kMin;  // 05:00
const uint kLastLoopBoarding = 1410u * kMin; // 23:30


float wf(uint i) { return uintBitsToFloat(W[i]); }

vec2  nodePos(uint n)     { uint o = uSec[0] + 2u * n; return vec2(wf(o), wf(o + 1u)); }
uint  adjOff(uint n)      { return W[uSec[1] + n]; }
uint  adjAt(uint k)       { return W[uSec[2] + k] & 0x7FFFFFFFu; }
bool  adjArterial(uint k) { return (W[uSec[2] + k] >> 31u) != 0u; }
const float kLocalHalfWidth = 3.5, kArterialHalfWidth = 8.0; // m, must match roadWidth() in mapgen.cpp
float roadHalfWidth(uint a, uint b) {
    for (uint k = adjOff(a); k < adjOff(a + 1u); ++k)
        if (adjAt(k) == b) return adjArterial(k) ? kArterialHalfWidth : kLocalHalfWidth;
    return kLocalHalfWidth;
}
uvec4 block(uint b)       { uint o = uSec[3] + 4u * b; return uvec4(W[o], W[o + 1u], W[o + 2u], W[o + 3u]); }
uint  pick(uint i)        { return W[uSec[4] + i]; }
vec4  railPt(uint i)      { uint o = uSec[5] + 4u * i; return vec4(wf(o), wf(o + 1u), wf(o + 2u), 0.0); }
uvec4 lineInfo(uint l)    { uint o = uSec[6] + 4u * l; return uvec4(W[o], W[o + 1u], W[o + 2u], W[o + 3u]); }
uint  stationNode(uint s) { return W[uSec[7] + 4u * s]; }
vec2  stationPos(uint s)  { uint o = uSec[7] + 4u * s; return vec2(wf(o + 1u), wf(o + 2u)); }
uvec2 railEntry(uint a, uint b) { uint o = uSec[8] + 2u * (a * uCounts.z + b); return uvec2(W[o], W[o + 1u]); }

// Line direction: A = (first profile entry, entries, loop, line)
//                 B = (headway, period / trip duration, fleet / trips per day, occupancy base), ms
uvec4 lineDirA(uint ld) { uint o = uSec[9] + 8u * ld; return uvec4(W[o], W[o + 1u], W[o + 2u], W[o + 3u]); }
uvec4 lineDirB(uint ld) { uint o = uSec[9] + 8u * ld + 4u; return uvec4(W[o], W[o + 1u], W[o + 2u], W[o + 3u]); }

uint  profStation(uint k) { return W[uSec[10] + 4u * k]; }
float profArc(uint k)     { return wf(uSec[10] + 4u * k + 1u); }
uint  profArr(uint k)     { return W[uSec[10] + 4u * k + 2u]; }
uint  profDep(uint k)     { return W[uSec[10] + 4u * k + 3u]; }
uint  profIndex(uint ld, uint s) { return W[uSec[11] + ld * uCounts.z + s]; }


uint pcg(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float rnd(inout uint s) {
    s = pcg(s);
    return float(s) * (1.0 / 4294967296.0);
}

// Simulation time t (ms since start) vs clock time T = t + uDayOffset (ms since day 0, 00:00).
uint clockOf(uint t) { return (t + uDayOffset) % kDay; }
uint dayOf(uint t)   { return (t + uDayOffset) / kDay; }


vec2 railPos(uint line, float s, out vec2 tangent) {
    uvec4 info  = lineInfo(line);
    float total = uintBitsToFloat(info.w);
    if (info.z != 0u) s = mod(s, total);
    s = clamp(s, 0.0, total);
    uint lo = 0u, hi = info.y - 1u;
    while (hi - lo > 1u) {
        uint mid = (lo + hi) >> 1u;
        if (railPt(info.x + mid).z <= s) lo = mid;
        else hi = mid;
    }
    vec4 a  = railPt(info.x + lo), b = railPt(info.x + hi);
    vec2 ab = b.xy - a.xy;
    tangent = length(ab) > 1e-3 ? normalize(ab) : vec2(1.0, 0.0);
    return mix(a.xy, b.xy, clamp((s - a.z) / max(b.z - a.z, 1e-3), 0.0, 1.0));
}

// Arc of a train `tau` ms after it left the first station of its profile.
float profileArc(uint first, uint count, uint tau) {
    uint last = first + count - 1u;
    if (tau >= profDep(last)) return profArc(last);
    uint lo = 0u, hi = count - 1u; // last entry with departure <= tau
    while (hi - lo > 1u) {
        uint mid = (lo + hi) >> 1u;
        if (profDep(first + mid) <= tau) lo = mid;
        else hi = mid;
    }
    uint a = profDep(first + lo), b = profArr(first + lo + 1u);
    if (tau >= b) return profArc(first + lo + 1u); // standing at the station
    float x = float(tau - a) / float(max(b - a, 1u));
    x = x * x * (3.0 - 2.0 * x); // accelerate, cruise, brake
    return mix(profArc(first + lo), profArc(first + lo + 1u), x);
}

// Time to ride from profile entry i to entry j (wrapping around on loops).
uint travelMs(uint ld, uint i, uint j) {
    uvec4 A = lineDirA(ld), B = lineDirB(ld);
    uint  dep = profDep(A.x + i);
    return j > i ? profArr(A.x + j) - dep : profArr(A.x + j) + B.y - dep;
}

// Which direction of `line` goes from `here` to `alight` (faster one on loops).
bool chooseDirection(uint line, uint here, uint alight, out uint ld, out uint i, out uint j) {
    uint best = kNone;
    ld = 0u; i = 0u; j = 0u;
    for (uint d = 0u; d < 2u; ++d) {
        uint l  = line * 2u + d;
        uint pi = profIndex(l, here), pj = profIndex(l, alight);
        if (pi == kNone || pj == kNone || pi == pj) continue;
        if (lineDirA(l).z == 0u && pj < pi) continue; // radial: wrong direction
        uint tt = travelMs(l, pi, pj);
        if (tt < best) {
            best = tt;
            ld   = l;
            i    = pi;
            j    = pj;
        }
    }
    return best != kNone;
}

// Next train of line direction `ld` leaving profile entry i at or after t.
// dep is in simulation time; trip indexes the occupancy counters.
bool nextDeparture(uint ld, uint i, uint t, out uint dep, out uint trip) {
    uvec4 A = lineDirA(ld), B = lineDirB(ld);
    uint  H = B.x, off = profDep(A.x + i);
    uint  T = t + uDayOffset;
    dep = kNone;
    trip = 0u;
    if (B.z == 0u) return false; // no service
    for (int attempt = 0; attempt < 2; ++attempt) {
        uint day = T - T % kDay;
        if (A.z != 0u) { // loop: the fleet passes entry i at off + m * H, train m % fleet
            uint m = T > off ? (T - off + H - 1u) / H : 0u;
            uint D = off + m * H;
            uint c = D % kDay;
            if (c >= kFirstTrain && c <= kLastLoopBoarding) {
                dep  = D - uDayOffset;
                trip = B.w + m % B.z;
                return true;
            }
            T = D - c + (c < kFirstTrain ? 0u : kDay) + kFirstTrain; // next morning
        } else {         // radial: trip m leaves the terminus at 05:00 + m * H
            uint first = day + kFirstTrain + off;
            uint m     = T > first ? (T - first + H - 1u) / H : 0u;
            if (m < B.z) {
                dep  = first + m * H - uDayOffset;
                trip = B.w + m;
                return true;
            }
            T = day + kDay + kFirstTrain; // tomorrow
        }
    }
    return false;
}

// Where train `slot` of line direction `ld` is at time t, if it is running.
bool trainAt(uint ld, uint slot, uint t, out float arc, out uint trip) {
    uvec4 A = lineDirA(ld), B = lineDirB(ld);
    uint  T = t + uDayOffset, c = T % kDay;
    arc = 0.0;
    trip = 0u;
    if (B.z == 0u || c < kFirstTrain) return false;
    if (A.z != 0u) { // loop: train `slot` left the first station at slot * H + k * period
        uint tau = (T % B.y + B.y - (slot * B.x) % B.y) % B.y;
        arc  = profileArc(A.x, A.y, tau);
        trip = B.w + slot;
        return true;
    }
    uint svc  = T - c + kFirstTrain;
    uint mNow = (T - svc) / B.x; // latest trip that has departed
    if (slot > mNow || mNow - slot >= B.z) return false;
    uint m   = mNow - slot;
    uint tau = T - svc - m * B.x;
    if (tau > B.y) return false; // already at the terminus
    arc  = profileArc(A.x, A.y, tau);
    trip = B.w + m;
    return true;
}
