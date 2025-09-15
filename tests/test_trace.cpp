#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "gcache/ghost_kv_cache.h"
#include "gcache/node.h"
#include "util.h"

using namespace gcache;

class CSVParser {
 public:
  static std::vector<std::vector<std::string>> parse_csv(
      const std::string& filename) {
    std::vector<std::vector<std::string>> data;
    std::ifstream file(filename);

    if (!file.is_open()) {
      throw std::runtime_error("Cannot open file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
      std::vector<std::string> row;
      std::stringstream ss(line);
      std::string cell;

      while (std::getline(ss, cell, ',')) {
        row.push_back(cell);
      }

      if (!row.empty()) {
        data.push_back(row);
      }
    }

    return data;
  }
};

void load_initial_cache(const std::string& cache_image_file,
                        SampledGhostKvCache<0>& ghost_cache) {
  std::cout << "Loading initial cache state from: " << cache_image_file
            << std::endl;

  auto data = CSVParser::parse_csv(cache_image_file);
  std::cout << "Parsed " << data.size() << " rows from cache image file"
            << std::endl;

  // Skip header row
  for (size_t i = 1; i < data.size(); ++i) {
    if (data[i].size() >= 2) {
      const std::string_view key = data[i][0];
      ghost_cache.access(key, 0, AccessMode::NOOP);
    }
  }

  std::cout << "Loaded " << (data.size() - 1) << " initial cache entries"
            << std::endl;
}

void simulate_trace(const std::string& req_trace_file,
                    SampledGhostKvCache<0>& ghost_cache) {
  std::cout << "Simulating request trace from: " << req_trace_file << std::endl;

  auto data = CSVParser::parse_csv(req_trace_file);

  uint64_t processed_requests = 0;

  // Skip header row
  for (size_t i = 1; i < data.size(); ++i) {
    if (data[i].size() != 4) {
      std::cout << "Skipping row " << i << " with " << data[i].size()
                << " columns" << std::endl;
      continue;
    }
    // timestamp and val_size are not used in ghost cache simulation
    std::string op = data[i][1];
    std::string key = data[i][2];

    ghost_cache.access(key, 0,
                       op == "get" ? AccessMode::DEFAULT : AccessMode::NOOP);
    processed_requests++;

    // Print progress every 1000 requests
    if (processed_requests % 1000 == 0) {
      std::cout << "Processed " << processed_requests << " requests..."
                << std::endl;
    }
  }

  std::cout << "Processed " << processed_requests << " requests" << std::endl;
}

void print_results(SampledGhostKvCache<0>& ghost_cache, size_t cache_size) {
  std::cout << "\n=== Ghost Cache Simulation Results ===" << std::endl;
  std::cout << "Cache Size: " << cache_size << " entries" << std::endl;

  // Get statistics for the specified cache size
  auto stat = ghost_cache.get_stat(cache_size);

  size_t total_requests = stat.hit_cnt + stat.miss_cnt;
  std::cout << "Total Requests: " << total_requests << std::endl;
  std::cout << "Cache Hits: " << stat.hit_cnt << std::endl;
  std::cout << "Cache Misses: " << stat.miss_cnt << std::endl;

  if (total_requests > 0) {
    double hit_rate = stat.get_hit_rate();
    double miss_rate = stat.get_miss_rate();

    std::cout << "Hit Rate: " << std::fixed << std::setprecision(4)
              << hit_rate * 100 << "%" << std::endl;
    std::cout << "Miss Rate: " << std::fixed << std::setprecision(4)
              << miss_rate * 100 << "%" << std::endl;
  } else {
    std::cout << "Hit Rate: 0.0000%" << std::endl;
    std::cout << "Miss Rate: 0.0000%" << std::endl;
  }

  std::cout << "=====================================" << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0]
              << " <cache_image.csv> <req_trace.csv> <cache_size_entries>"
              << std::endl;
    std::cerr << "Example: " << argv[0] << " cache_image.csv req_trace.csv 1000"
              << std::endl;
    return 1;
  }

  std::string cache_image_file = argv[1];
  std::string req_trace_file = argv[2];
  size_t cache_size = std::stoul(argv[3]);

  try {
    // Create ghost cache with appropriate parameters
    // tick, min_size, max_size - we'll use cache_size as max_size
    // Ensure minimum values to avoid issues
    uint32_t tick = 100'000;
    uint32_t min_size = 100'000;
    uint32_t max_size = std::max(1U, static_cast<uint32_t>(cache_size));

    std::cout << "Creating ghost cache with tick=" << tick
              << ", min_size=" << min_size << ", max_size=" << max_size
              << std::endl;

    SampledGhostKvCache<0> ghost_cache(tick, min_size, max_size);

    // Load initial cache state
    load_initial_cache(cache_image_file, ghost_cache);

    // Simulate request trace
    simulate_trace(req_trace_file, ghost_cache);

    // Print results
    print_results(ghost_cache, cache_size);

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
