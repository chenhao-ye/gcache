cmake_minimum_required(VERSION 3.9)
project(gcache)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -Werror -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -Werror")

set(SOURCE_FILES
	include/gcache/handle.h
	include/gcache/handle_table.h
	include/gcache/lru_cache.h)

include_directories(include)
add_executable(gcache_bench ${SOURCE_FILES} benchmarks/bench_lru.cpp)
