cmake_minimum_required(VERSION 3.20)
project(KarnixLocalLLM VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

add_library(karnix_ollama_client
    src/OllamaClient.cpp
)
target_include_directories(karnix_ollama_client
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(karnix_ollama_client
    PUBLIC CURL::libcurl nlohmann_json::nlohmann_json
)
target_compile_options(karnix_ollama_client PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(karnix_llm
    src/main.cpp
)
target_link_libraries(karnix_llm PRIVATE karnix_ollama_client)
target_compile_options(karnix_llm PRIVATE -Wall -Wextra -Wpedantic)

add_executable(karnix_qemu_medgemma_bridge
    src/QemuMedGemmaBridge.cpp
)
target_link_libraries(karnix_qemu_medgemma_bridge PRIVATE karnix_ollama_client)
target_compile_options(karnix_qemu_medgemma_bridge
    PRIVATE -Wall -Wextra -Wpedantic
)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(karnix_ollama_tests
        tests/OllamaClientTests.cpp
    )
    target_link_libraries(karnix_ollama_tests
        PRIVATE karnix_ollama_client Threads::Threads
    )
    target_compile_options(karnix_ollama_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME karnix_ollama_tests COMMAND karnix_ollama_tests)
endif()
