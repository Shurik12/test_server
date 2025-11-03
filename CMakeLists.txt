cmake_minimum_required(VERSION 3.20)
project(CppService VERSION 1.0.0 LANGUAGES CXX)

# Set compilers to Clang 20 (can be overridden from command line)
# cmake -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_C_COMPILER=clang-20 ..
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "clang++-20")
endif()
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "clang-20")
endif()

# Enable testing
include(CTest)
enable_testing()
option(BUILD_TESTING "Build tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_GENERATOR STREQUAL "Ninja")
    message(STATUS "Using Ninja generator")
endif()

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

set(SRC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
set(src_headers)
set(src_sources)
foreach(DIR ${SRC_DIRS})
    file(GLOB HEADERS "${DIR}/*.h" "${DIR}/*.hpp")
    file(GLOB SOURCES "${DIR}/*.cpp")
    list(APPEND src_headers ${HEADERS})
    list(APPEND src_sources ${SOURCES})
endforeach()
add_executable(server server.cpp)
target_sources(server PRIVATE ${src_sources})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(server PRIVATE
    spdlog::spdlog
    fmt::fmt
)

# Client executable
add_executable(client client.cpp)
target_sources(client PRIVATE ${src_sources})
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(client PRIVATE
    spdlog::spdlog
    fmt::fmt
)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    # Unified test executable
    add_executable(tests
        tests/test_main.cpp
        tests/service_tests.cpp
        tests/load_integration_tests.cpp
        ${src_sources}
    )

    target_include_directories(tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(tests PRIVATE
        GTest::gtest
        GTest::gtest_main
        spdlog::spdlog
        fmt::fmt
        pthread
    )

    target_compile_options(tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -O2
        -Wno-array-bounds
        -Wno-stringop-overflow
    )

    # Add test targets with labels
    add_test(NAME UnitTests COMMAND tests --gtest_filter="RequestHandlerTest*")
    add_test(NAME PerformanceTests COMMAND tests --gtest_filter="*PerformanceTest*")
    add_test(NAME IntegrationTests COMMAND tests --gtest_filter="LoadIntegrationTest*")

    # Set test properties
    set_tests_properties(UnitTests PROPERTIES LABELS "unit")
    set_tests_properties(PerformanceTests PROPERTIES LABELS "performance")
    set_tests_properties(IntegrationTests PROPERTIES LABELS "integration")

    # Google Benchmark
    find_package(benchmark QUIET)
    if(benchmark_FOUND)
        message(STATUS "Google Benchmark found, building load benchmarks")

        # Load benchmark executable
        add_executable(load_benchmark
            tests/load_benchmark.cpp
            ${src_sources}
        )

        target_include_directories(load_benchmark PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )

        target_link_libraries(load_benchmark PRIVATE
            benchmark::benchmark
            benchmark::benchmark_main
            spdlog::spdlog
            fmt::fmt
            pthread
        )

        target_compile_options(load_benchmark PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -O2
            -Wno-array-bounds
            -Wno-stringop-overflow
        )

        # Add benchmark test
        add_test(NAME LoadBenchmarks COMMAND load_benchmark
            --benchmark_min_time=5s
            --benchmark_repetitions=3
            --benchmark_report_aggregates_only=true
        )
        set_tests_properties(LoadBenchmarks PROPERTIES LABELS "benchmark")

    else()
        message(WARNING "Google Benchmark not found, load benchmarks will not be built")
    endif()
endif()

target_compile_options(server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
    -Wno-array-bounds
    -Wno-stringop-overflow
)

target_compile_options(client PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
    -Wno-array-bounds
    -Wno-stringop-overflow
)
