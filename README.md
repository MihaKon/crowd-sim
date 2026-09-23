# crowd-sim

A top-down simulation of a whole Tokyo-like city: millions of people living their day, trains running on timetables, cars stuck in traffic, and a city that is generated from scratch every time you press `N`.

I wanted to see how far one ordinary laptop GPU can go if the simulation is designed for it from the start. On my Intel UHD 630 it runs 5 million people and ~50 000 cars at about 70 fps.

Everything is written in C++20 and OpenGL 4.6 compute shaders. No game engine.

## What it does

**A generated city.** Every seed gives a different 20 × 20 km city: a bay, an imperial palace in the middle, a ring rail line with radial lines, arterial roads, around 50 000 streets, 19 000 city blocks and over half a million buildings.

**Districts.** The city is split into about 125 districts. The centre and the main rail hub are business districts full of glass towers. Industry sits along the bay. The rest is residential: ordinary housing, wealthy villa quarters with gardens (mostly west of the centre), and cramped poor districts that end up next to the factories.

**People with a daily routine.** Every person has a home and, if they work, a job. They leave in the morning, walk, take the train or drive, spend the day at work, sometimes go shopping, and come back in the evening. Where you live decides a lot: people from poor districts are more often unemployed and work low-paid jobs in shops and factories, while people from wealthy districts mostly work well-paid jobs in the centre.

**Trains.** Eight lines with real timetables (a ring line and cross-town line every 3 minutes, radial lines every 4 minutes, 05:00 to 23:30). Trains have limited capacity. At rush hour people get left behind on full platforms, and after a few missed trains they give up and walk.

**Traffic.** Car owners drive when the trip is long, and vans and taxis drive around all day. Streets have lanes, arterials have traffic lights, and cars queue behind each other. At rush hour the main roads really do jam.

**Inspecting anyone.** Click on any person or car to see who they are: name, age, job, social class, monthly income, where they live and work, how they commute and what they are doing right now ("on the platform at Shin-Umeda St., the Ring Line leaves 07:42"). Press `F` to follow them around the city.

**Layers for the big picture.** Zoomed out, individual people are just noise, so they fade out and a layer takes over: a density map of where people are, or a traffic map where every lane is coloured from free-flowing green to jammed red.

**Day and night.** Buildings are drawn in 2.5D with walls, windows and shadows. After dusk the windows light up.

## Controls

| Input | Action |
|---|---|
| drag / wheel | pan / zoom |
| click | inspect a person or car |
| `F` | follow the inspected one |
| `P` | follow a random person |
| `Esc` | close the inspector, then quit |
| `Space` | pause |
| `+` / `-` | simulation speed (×0.25 to ×3840) |
| `D` | layer: people outdoors → everyone → traffic → off |
| `N` | generate a new city |
| `R` | reset the camera |
| `H` | hide the UI |
| `M` / `A` | toggle map / people and cars |
| `L` / `V` | toggle level of detail / vsync |
| `[` / `]` | point size |

Run it as `./build/crowd_sim [people] [seed]`, for example `./build/crowd_sim 5e6 42`. The defaults are 1 million people and seed 1.

## How it works

### Generating the city

The generator is plain C++ and runs on the CPU in about two seconds.

1. **Rail first.** A ring of hubs around the centre gets a ring line, a cross-town line through the middle and radial lines towards the edges. Stations are placed along them.
2. **Density.** A density field is built from the hubs, the stations and the city centre, plus some noise. It decides how dense the streets get.
3. **Streets.** Street junctions are scattered with a variable-radius Poisson disk (dense near stations, sparse in the outskirts), and streets are the edges of their Gabriel graph. Arterial roads are inserted first, so they end up as continuous roads. Then triangles are merged into quads, sharp angles are removed and only the largest connected part is kept.
4. **Blocks.** Blocks are the faces of this planar street graph.
5. **Districts.** District seeds sit on a jittered grid, and each block joins the nearest one. Business and industry come from geography. For the rest I compute a "prestige" score and rank the districts by it: the top becomes wealthy, the bottom poor.
6. **Buildings.** Lots are placed along the inside of every block, ring after ring. Each lot is checked against its neighbours (separating axis test) and the real streets, so nothing overlaps.

### Simulating people on the GPU

This is the part that makes millions of people possible:

- **Event-driven.** A person only does real work when something happens to them, like reaching a junction, a train arriving or the end of the work day. The rest of the time the GPU thread just checks a timestamp and exits.
- **No stored positions.** Where someone is follows from the current leg of their trip and the time it started, so a position is only computed for people actually on screen. Each person takes 36 bytes of GPU memory.
- **Precomputed routing.** Walking is greedy (next street towards the goal, with a bit of noise). For trains, the CPU precomputes the best connection between every pair of stations with Dijkstra over (station, line) pairs, so a transfer costs time. Trains follow timetables, so a train's position is a formula of time and nothing needs to be simulated.
- **Atomic boarding.** Boarding uses one atomic counter per train trip: if the train turns out to be full, the person steps back and waits for the next one.

### Traffic

- **Lanes are queues.** Every lane is a queue of cars, stepped every 0.5 s of simulated time.
- **Lock-free moves.** Cars that want to move onto another lane "bid" for it with an atomic minimum, so each lane takes at most one new car per step. Thousands of lanes update in parallel without locks.
- **Region routing.** Long trips use routing tables: the city is split into about 700 regions, and for each one the CPU precomputes (on all cores) which way to turn at every junction to get there.

### Drawing

- **Level of detail.** Only people and cars that are on screen, outdoors and within the level-of-detail budget are collected into a list, which is drawn with an indirect draw call. Zooming in only ever adds people, so nothing flickers.
- **Procedural streets.** Streets are drawn by a shader that knows each pixel's position along and across the street. That's how it draws sidewalks, lane markings (Japanese style, driving on the left), zebra crossings and stop lines at any angle without a single texture.
- **Buildings.** Each building is one 32-byte record, and the shader turns it into walls and a roof. Roofs are tiles from the Kenney pixel-art pack, walls get procedural windows, and shadows use the stencil buffer so overlapping shadows don't get darker.
- **People sprites.** People are Kenney sprites that face the direction they walk. Cars are small top-down pixel-art sprites generated in code.

### The inspector

- **Identity from the id.** A person's name, age, job and income are not stored anywhere. They are computed from the person's id and the city seed, so they cost no memory.
- **No stalls.** The marker that follows the selected person is positioned by the simulation shader itself and read back through persistently mapped buffers with fences, so following someone never makes the CPU wait for the GPU.

## Project structure

```
src/
  main.cpp      window and startup
  app/          app state, input, HUD, the frame loop
  city/         city generator (rail, streets, districts, buildings) and the tables for the GPU
  sim/          GPU simulation of people and traffic
  render/       map, streets, buildings, sprites, density map
  ui/           text rendering, picking, inspector
  core/         math, camera, colours, OpenGL helpers
shaders/        GLSL compute and render shaders
tools/          map_preview: exports a generated city as SVG, no GPU needed
```

## Credits

- Pixel art: [Kenney](https://kenney.nl) "RPG Urban Pack" (CC0)
- Font: [JetBrains Mono](https://www.jetbrains.com/lp/mono/) (OFL)
- [stb_image and stb_truetype](https://github.com/nothings/stb) by Sean Barrett (public domain)
- [GLFW](https://www.glfw.org) and [glad](https://github.com/Dav1dde/glad)
- Colours: [Tokyo Night](https://github.com/enkia/tokyo-night-vscode-theme)

---

## For developers

### Requirements

- A GPU and driver with **OpenGL 4.6** (Intel, AMD and NVIDIA all work, Mesa included)
- **CMake 3.24+** and a **C++20** compiler (GCC 11+ or Clang 14+)
- **GLFW 3.3+**
- **Python 3 with Jinja2** (glad generates the OpenGL loader at configure time)
- **Git** and an internet connection for the first configure

Everything else is downloaded automatically into `build/third_party/` on the first configure, from pinned URLs: glad, the stb headers, the JetBrains Mono font and the Kenney sprite sheet.

**Arch Linux**
```sh
sudo pacman -S --needed cmake gcc glfw python-jinja git
```

**Ubuntu / Debian**
```sh
sudo apt install cmake g++ libglfw3-dev python3-jinja2 git
```

**Fedora**
```sh
sudo dnf install cmake gcc-c++ glfw-devel python3-jinja2 git
```

### Build and run

```sh
cmake -B build
cmake --build build -j
./build/crowd_sim            # 1M people, seed 1
./build/crowd_sim 5e6 42     # 5M people, seed 42
```

The default build type is Release. A Debug build (`-DCMAKE_BUILD_TYPE=Debug`) turns on the OpenGL debug output.

Shaders are loaded from the source tree at runtime, so after changing a shader you only need to restart the app.

### Useful to know

- `./build/map_preview <seed> city.svg` writes a generated city as an SVG. It's the fastest way to work on the generator.
- The simulation prints one status line per second with GPU timings of each pass (sim, cull, cars, map, draw), which is handy for profiling.
- The number of people is limited by the largest shader storage buffer your driver allows. If you ask for too many, the app tells you and uses the maximum instead.
- The generator is deterministic: the same seed always gives the same city, down to every building.
