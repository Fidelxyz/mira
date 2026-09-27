#pragma once

#include <cstddef>
#include <string>

#include <yaml-cpp/yaml.h>

#include "rime.h"

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(const YAML::Node& document);

    bool ready() const;
    bool run_deployment(Rime& rime,
                        const std::string& schema_id,
                        const std::string& deployment_name,
                        const YAML::Node& deployment);
    int finish() const;

private:
    bool initialized = false;
    std::size_t warmup = 0;
    std::size_t iterations = 0;
    std::size_t completed = 0;
};
