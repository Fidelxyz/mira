#pragma once

#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "rime.h"

struct lua_State;

class TestRunner {
public:
    explicit TestRunner(const YAML::Node& document);
    ~TestRunner();

    bool ready() const;
    bool run_deployment(Rime& rime,
                        const std::string& schema_id,
                        const std::string& deployment_name,
                        const YAML::Node& deployment);
    int finish() const;

private:
    lua_State* lua = nullptr;
    bool initialized = false;
    std::vector<std::string> passlist;
    std::vector<std::string> faillist;
};
