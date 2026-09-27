#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <yaml-cpp/yaml.h>

#include "rime.h"

std::unique_ptr<Rime> prepare_deployment(
    const std::filesystem::path& source_dir,
    const std::string& schema_id,
    const std::optional<std::string>& cache_dir,
    const YAML::Node& deployment);

std::unique_ptr<Session> prepare_session(
    Rime& rime,
    const std::string& schema_id,
    const YAML::Node& options);
