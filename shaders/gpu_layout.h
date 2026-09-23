// GPU contract shared by C++ (#include "gpu_layout.h") and GLSL (#include "gpu_layout.h").
// Only #define and comments here: the file must stay valid in both languages.
// Locations after an array are computed from its size, so resizing it moves them on both sides.
#ifndef CROWD_SIM_GPU_LAYOUT_H
#define CROWD_SIM_GPU_LAYOUT_H

// Tables in SimWorld::data (Section in sim_data.hpp), indices into uSec[].
#define SEC_NODE_POS      0
#define SEC_ADJ_OFF       1
#define SEC_ADJ           2
#define SEC_BLOCKS        3
#define SEC_PICK          4
#define SEC_RAIL_PTS      5
#define SEC_LINE_INFO     6
#define SEC_STATION_INFO  7
#define SEC_RAIL_TABLE    8
#define SEC_LINE_DIR      9
#define SEC_PROFILE       10
#define SEC_PROFILE_INDEX 11
#define SECTION_COUNT     12

// Tables in TrafficWorld::data (TrafficSection in traffic_data.hpp), indices into uTSec[].
#define TSEC_LANES          0
#define TSEC_DIR_EDGES      1
#define TSEC_ADJ_DIR_EDGE   2
#define TSEC_NODE_REGION    3
#define TSEC_NEXT_SLOT      4
#define TRAFFIC_SECTION_COUNT 5

// agents.comp: locals 0-8
#define AGENTS_LOC_MODE         0
#define AGENTS_LOC_COUNT        1
#define AGENTS_LOC_NOW          2
#define AGENTS_LOC_DAY_OFFSET   3
#define AGENTS_LOC_SEED         4
#define AGENTS_LOC_COLLECT_STATS 5
#define AGENTS_LOC_VIEW         6
#define AGENTS_LOC_STRIDE       7
#define AGENTS_LOC_CAPACITY     8

// traffic.comp: locals 0-8
#define TRAFFIC_LOC_NOW         0
#define TRAFFIC_LOC_DT          1
#define TRAFFIC_LOC_LANE_COUNT  2
#define TRAFFIC_LOC_DAY_OFFSET  3
#define TRAFFIC_LOC_CAR_COUNT   4
#define TRAFFIC_LOC_OWNER_COUNT 5
#define TRAFFIC_LOC_VIEW        6
#define TRAFFIC_LOC_STRIDE      7
#define TRAFFIC_LOC_CAPACITY    8

// trains.vert: locals 0-5
#define TRAINS_LOC_CENTER       0
#define TRAINS_LOC_SCALE        1
#define TRAINS_LOC_PPM          2
#define TRAINS_LOC_DAY_OFFSET   3
#define TRAINS_LOC_NOW          4
#define TRAINS_LOC_TRAIN_CAP    5

// Uniform locations shared by agents.comp, traffic.comp and trains.vert.
#define LOC_COUNTS    9  // uvec4 uCounts
#define LOC_SEC       10 // uint  uSec[SECTION_COUNT]
#define LOC_AFTER_SEC (LOC_SEC + SECTION_COUNT)

// Per-shader uniforms (locations 0..LOC_COUNTS-1) must stay below the shared block.
#if AGENTS_LOC_CAPACITY >= LOC_COUNTS || TRAFFIC_LOC_CAPACITY >= LOC_COUNTS || TRAINS_LOC_TRAIN_CAP >= LOC_COUNTS
#error per-shader uniform locations overlap LOC_COUNTS
#endif

// Shared constants.
#define LOCAL_SIZE        256                       // work group size of agents.comp, traffic.comp
#define CULL_PER_THREAD   4                         // agents per thread in the agents.comp cull pass
#define DAY_START_MS      (6 * 3600 * 1000)         // simulation starts at 06:00 (uDayOffset)
#define TRAIN_LINE_COLORS 9                         // vec3 uLineColor[], one per rail line
#define LANE_STRIDE       8                         // words per lane in kTSecLanes

// agents.comp: shader-specific uniforms after LOC_AFTER_SEC
#define AGENTS_LOC_TRAIN_CAP   (LOC_AFTER_SEC + 0)
#define AGENTS_LOC_OWNER_EVERY (LOC_AFTER_SEC + 1)
#define AGENTS_LOC_SELECTED    (LOC_AFTER_SEC + 2)
#define AGENTS_LOC_HEAT_MODE   (LOC_AFTER_SEC + 3)
#define AGENTS_LOC_HEAT_DIM    (LOC_AFTER_SEC + 4)
#define AGENTS_LOC_HEAT_SCALE  (LOC_AFTER_SEC + 5)
#define AGENTS_LOC_WORK_BY_PAY (LOC_AFTER_SEC + 6)

// traffic.comp: shader-specific uniforms after LOC_AFTER_SEC
#define TRAFFIC_LOC_ADVANCE      (LOC_AFTER_SEC + 0)
#define TRAFFIC_LOC_EMIT         (LOC_AFTER_SEC + 1)
#define TRAFFIC_LOC_COLLECT      (LOC_AFTER_SEC + 2)
#define TRAFFIC_LOC_TSEC         (LOC_AFTER_SEC + 3) // uint uTSec[TRAFFIC_SECTION_COUNT]
#define TRAFFIC_LOC_SEED         (TRAFFIC_LOC_TSEC + TRAFFIC_SECTION_COUNT)
#define TRAFFIC_LOC_NODE_COUNT   (TRAFFIC_LOC_SEED + 1)
#define TRAFFIC_LOC_SELECTED_CAR (TRAFFIC_LOC_SEED + 2)
#define TRAFFIC_LOC_LANE_STATS   (TRAFFIC_LOC_SEED + 3)

// trains.vert: shader-specific uniforms after LOC_AFTER_SEC
#define TRAINS_LOC_LINE_COLOR (LOC_AFTER_SEC + 1) // vec3 uLineColor[TRAIN_LINE_COLORS]

#endif
