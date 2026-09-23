#version 460
// One building = 78 vertices pulled from its record:
//   0-23  the four walls (6 each); only the ones facing the viewer (south)
//         are kept, the others collapse (they are behind the roof)
//   24-77 the roof: a 9-slice lifted up the screen by height * kLean.
// Corners of the roof are exactly one tile; edges and centre repeat their
// tile (see buildings.frag).

struct Building {
    vec2  origin;    // street-facing corner
    vec2  axisAlong; // unit, along the street
    float sizeAlong; // metres, a whole number of tiles
    float sizeIn;
    uint  info;      // bits 0-2 roof set, bit 3: axisIn = -perp(axisAlong), bits 4-6 facade,
                     // bits 7-8 window pattern, bits 9-31 seed
    float height;    // metres
};
layout(std430, binding = 19) readonly buffer Buildings { Building buildings[]; };

layout(location = 0) uniform vec2 uCenter;
layout(location = 1) uniform vec2 uScale;

flat out uint  vKind;      // 0 wall, 1 roof
out vec2       vTiles;     // roof: position within the cell, in tiles
flat out vec2  vCellTiles; // roof: cell size in tiles
flat out ivec2 vTile;      // roof: atlas tile of this cell
out vec2       vWall;      // wall: metres along, metres up
flat out vec4  vWallInfo;  // wall: colour (rgb), wall length
flat out uvec2 vSeedSet;   // random seed, window pattern
flat out float vHeight;

const float kTile = 3.5; // metres per roof tile, must match kBuildingTileMetres
const float kLean = 0.5; // roof lift per metre of height, must match building_render.cpp
// Roof sets in the Kenney RPG Urban Pack atlas: top-left tile, and which three
// atlas rows make the street edge / middle / back edge (the tile roofs are 4
// rows tall: skip their ridge band).
const ivec2 kRoofSet[6]  = ivec2[](ivec2(17, 0), ivec2(17, 4), ivec2(8, 0), ivec2(8, 3), ivec2(0, 3), ivec2(0, 0));
const ivec3 kRoofRows[6] = ivec3[](ivec3(0, 2, 3), ivec3(0, 2, 3), ivec3(0, 1, 2), ivec3(0, 1, 2),
                                   ivec3(0, 1, 2), ivec3(0, 1, 2));
// Facades (mapgen.cpp pickStyle): warm plaster, light plaster, concrete, glass
// tower, old dark walls, corrugated metal, white villa.
const vec3 kFacade[7] = vec3[](vec3(0.72, 0.64, 0.55), vec3(0.78, 0.76, 0.83), vec3(0.56, 0.58, 0.68),
                               vec3(0.36, 0.48, 0.66), vec3(0.46, 0.42, 0.40), vec3(0.52, 0.55, 0.58),
                               vec3(0.90, 0.88, 0.84));
const vec2 kQuad[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    Building b = buildings[gl_VertexID / 78];
    int      k = gl_VertexID % 78;
    vec2     A = b.axisAlong;
    vec2     I = vec2(-A.y, A.x) * ((b.info & 8u) != 0u ? -1.0 : 1.0);
    vec2     lift = vec2(0.0, b.height * kLean);
    uint     set  = b.info & 7u;

    vSeedSet   = uvec2(b.info >> 9u, (b.info >> 7u) & 3u);
    vHeight    = b.height;
    vTiles     = vec2(0.0);
    vCellTiles = vec2(1.0);
    vTile      = ivec2(0);
    vWall      = vec2(0.0);
    vWallInfo  = vec4(0.0);

    vec2 world;
    if (k < 24) { // ---- wall
        vKind = 0u;
        int  e = k / 6;
        vec2 q = kQuad[k % 6];
        vec2 c[4] = vec2[](b.origin, b.origin + A * b.sizeAlong, b.origin + A * b.sizeAlong + I * b.sizeIn,
                           b.origin + I * b.sizeIn);
        vec2 p0 = c[e], p1 = c[(e + 1) % 4];
        vec2 centre = b.origin + A * (0.5 * b.sizeAlong) + I * (0.5 * b.sizeIn);
        vec2 edge = p1 - p0;
        vec2 n = normalize(vec2(edge.y, -edge.x));
        if (dot(n, 0.5 * (p0 + p1) - centre) < 0.0) n = -n; // outward
        if (n.y > -0.02) { // faces away from the viewer: hidden behind the roof
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        world     = mix(p0, p1, q.x) + lift * q.y;
        vWall     = vec2(q.x, q.y) * vec2(length(edge), b.height);
        vWallInfo = vec4(kFacade[min((b.info >> 4u) & 7u, 6u)] * (0.80 - 0.16 * n.x), length(edge)); // sun from the west
    } else { // ---- roof
        vKind   = 1u;
        int   r = k - 24, cell = r / 6;
        ivec2 cr = ivec2(cell % 3, cell / 3); // column along, row inward
        vec2  q  = kQuad[r % 6];
        float along[4] = float[](0.0, kTile, b.sizeAlong - kTile, b.sizeAlong);
        float inw[4]   = float[](0.0, kTile, b.sizeIn - kTile, b.sizeIn);
        vec2  size     = vec2(along[cr.x + 1] - along[cr.x], inw[cr.y + 1] - inw[cr.y]);
        vec2  local    = q * size;
        world      = b.origin + A * (along[cr.x] + local.x) + I * (inw[cr.y] + local.y) + lift;
        vTile      = kRoofSet[set] + ivec2(cr.x, kRoofRows[set][cr.y]);
        vTiles     = local / kTile;
        vCellTiles = size / kTile;
    }
    gl_Position = vec4((world - uCenter) * uScale, 0.0, 1.0);
}
