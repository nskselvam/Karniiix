cmake_minimum_required(VERSION 3.16)
project(KarnixLungXrayML CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(lung_xray_ml lung_xray_ml.cpp)
target_compile_options(lung_xray_ml PRIVATE -Wall -Wextra -Wpedantic -O2)

add_executable(osteoarthritis_ml osteoarthritis_ml.cpp)
target_include_directories(osteoarthritis_ml PRIVATE ${CMAKE_SOURCE_DIR}/..)
target_compile_options(osteoarthritis_ml PRIVATE -Wall -Wextra -Wpedantic -O2)

add_executable(brain_tumor_ml brain_tumor_ml.cpp)
target_include_directories(brain_tumor_ml PRIVATE ${CMAKE_SOURCE_DIR}/..)
target_compile_options(brain_tumor_ml PRIVATE -Wall -Wextra -Wpedantic -O2)

add_executable(lumbar_stenosis_ml lumbar_stenosis_ml.cpp)
target_include_directories(lumbar_stenosis_ml PRIVATE ${CMAKE_SOURCE_DIR}/..)
target_compile_options(lumbar_stenosis_ml PRIVATE -Wall -Wextra -Wpedantic -O2)

if(APPLE)
  target_link_libraries(lung_xray_ml PRIVATE
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework CoreFoundation"
  )
  target_link_libraries(osteoarthritis_ml PRIVATE
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework CoreFoundation"
  )
  target_link_libraries(brain_tumor_ml PRIVATE
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework CoreFoundation"
  )
  target_link_libraries(lumbar_stenosis_ml PRIVATE
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework CoreFoundation"
  )
endif()
