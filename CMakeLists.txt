cmake_minimum_required(VERSION 3.9)
project(gcache)
set(CMAKE_CXX_STANDARD 20)

enable_testing()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Werror -msse4.2")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Werror -msse4.2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -Wall -Werror -msse4.2")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Wall -Werror -msse4.2")

message("CMAKE_CXX_FLAGS_DEBUG is ${CMAKE_CXX_FLAGS_DEBUG}")
message("CMAKE_CXX_FLAGS_RELEASE is ${CMAKE_CXX_FLAGS_RELEASE}")
message("CMAKE_CXX_FLAGS_RELWITHDEBINFO is ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message("CMAKE_CXX_FLAGS_MINSIZEREL is ${CMAKE_CXX_FLAGS_MINSIZEREL}")

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(SOURCE_FILES
	include/gcache/hash.h
	include/gcache/node.h
	include/gcache/table.h
	include/gcache/lru_cache.h
	include/gcache/stat.h
	include/gcache/ghost_cache.h
	include/gcache/shared_cache.h)

include_directories(include)
add_executable(gcache_test_lru ${SOURCE_FILES} tests/test_lru.cpp)
add_executable(gcache_test_shared ${SOURCE_FILES} tests/test_shared.cpp)
add_executable(gcache_test_ghost ${SOURCE_FILES} tests/test_ghost.cpp)
add_executable(gcache_bench_ghost ${SOURCE_FILES} benchmarks/bench_ghost.cpp)

if(SAMPLE_SHIFT)
	target_compile_definitions(gcache_bench_ghost PRIVATE SAMPLE_SHIFT=${SAMPLE_SHIFT})
endif()

add_test(NAME test_lru COMMAND gcache_test_lru)
add_test(NAME test_shared COMMAND gcache_test_shared)
add_test(NAME test_ghost COMMAND gcache_test_ghost)
add_test(NAME bench_ghost COMMAND gcache_bench_ghost)
