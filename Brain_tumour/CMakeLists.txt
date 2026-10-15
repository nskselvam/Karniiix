cmake_minimum_required(VERSION 3.10)
project(KarnixBrainTumour CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_SOURCE_DIR}/../neural_network
    ${CMAKE_SOURCE_DIR}/..
)

add_executable(brain_tumour_cli brain_tumour_cli.cpp)
target_compile_options(brain_tumour_cli PRIVATE -Wall -Wextra -O2)
target_compile_options(brain_tumour_cli PRIVATE -Wno-sign-compare -Wno-unused-variable)
if(APPLE)
    target_link_libraries(brain_tumour_cli PRIVATE
        "-framework CoreGraphics"
        "-framework ImageIO"
        "-framework CoreFoundation"
    )

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
        add_executable(brain_tumour_visualizer brain_tumour_visualizer.cpp)
        target_include_directories(brain_tumour_visualizer PRIVATE
            "${SDL2_ROOT}/include/SDL2"
            "${SDL2_TTF_ROOT}/include/SDL2"
        )
        target_link_libraries(brain_tumour_visualizer PRIVATE
            "${SDL2_ROOT}/lib/libSDL2.dylib"
            "${SDL2_TTF_ROOT}/lib/libSDL2_ttf.dylib"
            "-framework CoreGraphics"
            "-framework ImageIO"
            "-framework CoreFoundation"
        )
        target_compile_options(brain_tumour_visualizer PRIVATE -Wall -Wextra -O2 -Wno-sign-compare -Wno-unused-variable)
    else()
        message(WARNING "SDL2/SDL2_ttf not found; brain_tumour_visualizer target disabled")
    endif()
endif()

message(STATUS "Karnix Brain Tumour C++ engine")
message(STATUS "No Python runtime is required")