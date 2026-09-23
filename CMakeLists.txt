cmake_minimum_required(VERSION 3.24)
project(crowd_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNINGS -Wall -Wextra -Wpedantic)

# OpenGL 4.6 core loader, generated at configure time (needs python-jinja2).
include(FetchContent)
FetchContent_Declare(glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v2.0.8
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl46 STATIC REPRODUCIBLE API gl:core=4.6)

find_package(glfw3 3.3 REQUIRED)
find_package(Threads REQUIRED)

# Single-file assets and headers, downloaded once into the build tree (pinned URLs).
set(THIRD_PARTY ${CMAKE_BINARY_DIR}/third_party)
function(fetch_file url dest)
  if(NOT EXISTS ${dest})
    file(DOWNLOAD ${url} ${dest}.part STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE ${dest}.part)
      message(FATAL_ERROR "download failed: ${url}: ${status}")
    endif()
    file(RENAME ${dest}.part ${dest})
  endif()
endfunction()
set(STB https://raw.githubusercontent.com/nothings/stb/5c205738c191bcb0abc65c4febfa9bd25ff35234)
fetch_file(${STB}/stb_truetype.h ${THIRD_PARTY}/stb_truetype.h)
fetch_file(${STB}/stb_image.h ${THIRD_PARTY}/stb_image.h)
fetch_file(https://raw.githubusercontent.com/JetBrains/JetBrainsMono/v2.304/fonts/ttf/JetBrainsMono-Regular.ttf
           ${THIRD_PARTY}/JetBrainsMono-Regular.ttf)
fetch_file(https://raw.githubusercontent.com/ETdoFresh/kenney.nl/master/kenney_rpgurbanpack/Tilemap/tilemap_packed.png
           ${THIRD_PARTY}/kenney_rpgurban.png)

# City generator and simulation tables: plain C++, no OpenGL.
add_library(citygen STATIC
  src/city/mapgen.cpp
  src/city/rail.cpp
  src/city/streets.cpp
  src/city/districts.cpp
  src/city/buildings.cpp
  src/city/sim_data.cpp
  src/city/traffic_data.cpp)
target_include_directories(citygen PUBLIC src shaders)
target_link_libraries(citygen PUBLIC Threads::Threads)
target_compile_options(citygen PRIVATE ${WARNINGS})

add_executable(crowd_sim
  src/main.cpp
  src/app/input.cpp
  src/app/hud.cpp
  src/app/runner.cpp
  src/sim/agents.cpp
  src/sim/traffic.cpp
  src/render/map_render.cpp
  src/render/roads.cpp
  src/render/building_render.cpp
  src/render/sprite_atlas.cpp
  src/render/heatmap.cpp
  src/ui/ui.cpp
  src/ui/selection.cpp
  src/ui/inspector.cpp)
target_include_directories(crowd_sim SYSTEM PRIVATE ${THIRD_PARTY})
target_link_libraries(crowd_sim PRIVATE citygen glad_gl46 glfw)
target_compile_definitions(crowd_sim PRIVATE
  SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  FONT_PATH="${THIRD_PARTY}/JetBrainsMono-Regular.ttf"
  SPRITE_ATLAS_PATH="${THIRD_PARTY}/kenney_rpgurban.png")
target_compile_options(crowd_sim PRIVATE ${WARNINGS})

# Headless SVG export of a generated city: ./build/map_preview <seed> city.svg
add_executable(map_preview tools/map_preview.cpp)
target_link_libraries(map_preview PRIVATE citygen)
target_compile_options(map_preview PRIVATE ${WARNINGS})

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
  enable_testing()
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  add_executable(citygen_tests tests/math_test.cpp tests/citygen_test.cpp)
  target_link_libraries(citygen_tests PRIVATE citygen Catch2::Catch2WithMain)
  target_compile_options(citygen_tests PRIVATE ${WARNINGS})
  catch_discover_tests(citygen_tests)
endif()
