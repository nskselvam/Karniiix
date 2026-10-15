cmake_minimum_required(VERSION 3.10)
project(KarnixSpineEdge CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Include paths ─────────────────────────────────────────────────────────────
# Karnix core lives one level up in neural_network/ and medical/
include_directories(
    ${CMAKE_SOURCE_DIR}/..          # exposes neural_network/, medical/, etc.
    ${CMAKE_SOURCE_DIR}/medical     # local SpineCNN / SpineEvaluationUtils
)

# ── Target ────────────────────────────────────────────────────────────────────
add_executable(spine_cli
    spine_cli.c++
    medical/SpineEvaluationUtils.cpp
)

add_executable(mura_pipeline
    mura_pipeline.c++
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(spine_cli PRIVATE
    -Wall
    -Wextra
    -O2
)

target_compile_options(mura_pipeline PRIVATE
    -Wall
    -Wextra
    -O2
)

if(APPLE)
    file(GLOB SDL2_CELLAR_DIRS
        "/opt/homebrew/Cellar/sdl2/*"
        "/opt/homebrew/Cellar/sdl2-compat/*"
        "/usr/local/Cellar/sdl2/*"
        "/usr/local/Cellar/sdl2-compat/*"
        "/opt/homebrew/opt/sdl2"
        "/usr/local/opt/sdl2"
    )
    file(GLOB SDL2_TTF_CELLAR_DIRS
        "/opt/homebrew/Cellar/sdl2_ttf/*"
        "/usr/local/Cellar/sdl2_ttf/*"
        "/opt/homebrew/opt/sdl2_ttf"
        "/usr/local/opt/sdl2_ttf"
    )
    list(SORT SDL2_CELLAR_DIRS)
    list(SORT SDL2_TTF_CELLAR_DIRS)
    list(REVERSE SDL2_CELLAR_DIRS)
    list(REVERSE SDL2_TTF_CELLAR_DIRS)
    if(SDL2_CELLAR_DIRS AND SDL2_TTF_CELLAR_DIRS)
        list(GET SDL2_CELLAR_DIRS 0 SDL2_ROOT)
        list(GET SDL2_TTF_CELLAR_DIRS 0 SDL2_TTF_ROOT)

        add_executable(cnn_visualizer cnn_visualizer.c++)
        target_include_directories(cnn_visualizer PRIVATE
            "${SDL2_ROOT}/include/SDL2"
            "${SDL2_TTF_ROOT}/include/SDL2"
        )
        target_link_libraries(cnn_visualizer PRIVATE
            "${SDL2_ROOT}/lib/libSDL2.dylib"
            "${SDL2_TTF_ROOT}/lib/libSDL2_ttf.dylib"
        )
        target_compile_options(cnn_visualizer PRIVATE -Wall -Wextra -O2)
    else()
        message(WARNING "SDL2/SDL2_ttf not found; cnn_visualizer target disabled")
    endif()
endif()

# Print offline reminder
message(STATUS "==================================================")
message(STATUS " Karnix Spine Edge")
message(STATUS " Single-file compile:")
message(STATUS "   clang++ -std=gnu++14 spine_cli.c++ -o spine_cli")
message(STATUS "==================================================")
