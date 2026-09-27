#include "config.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <regex>
#include <string>

#include <yaml-cpp/yaml.h>
#include <argparse/argparse.hpp>

#include "benchmark_runner.h"
#include "deployment.h"
#include "test_runner.h"

namespace fs = std::filesystem;

enum class SpecType {
    test,
    benchmark,
};

static std::optional<SpecType> spec_type(const std::string& path);
static bool has_suffix(const std::string& value, const std::string& suffix);

int
main(int argc, char* argv[])
{
    argparse::ArgumentParser program(PROJECT_NAME, PROJECT_VERSION);
    program.add_argument("FILE")
        .help("test or benchmark spec file");
    program.add_argument("-C", "--cache-dir")
        .help("cache built artifacts");
    program.add_argument("-R", "--regex")
        .help("regex to match desired deployments")
        .default_value(".*");
    bool arguments_parsed = false;
    try {
        program.parse_args(argc, argv);
        arguments_parsed = true;
        const std::string path = program.get("FILE");
        const auto type = spec_type(path);
        if (!type) {
            std::cerr << "unsupported spec filename '" << path
                      << "'; expected *.test.yaml or *.benchmark.yaml\n";
            return 1;
        }

        const std::regex deployment_pattern(program.get("-R"));
        std::optional<std::string> cache_dir;
        if (program.is_used("-C")) {
            cache_dir = program.get("-C");
        }

        const auto document = YAML::LoadFile(path);
        const auto schema_id = document["schema"].as<std::string>();
        const auto source_dir = fs::path(path).remove_filename() /
                                document["source_dir"].as<std::string>();
        std::cout << "SELECT " << schema_id << " from " << source_dir << "\n";

        std::optional<TestRunner> test_runner;
        std::optional<BenchmarkRunner> benchmark_runner;
        if (*type == SpecType::test) {
            test_runner.emplace(document);
            if (!test_runner->ready()) {
                return 1;
            }
        } else {
            benchmark_runner.emplace(document);
            if (!benchmark_runner->ready()) {
                return 1;
            }
        }

        bool completed = true;
        for (const auto& pair : document["deploy"]) {
            const auto name = pair.first.as<std::string>();
            const auto deployment = pair.second;
            if (!std::regex_match(name, deployment_pattern)) {
                continue;
            }
            if (*type == SpecType::benchmark &&
                (!deployment["benchmarks"] ||
                 !deployment["benchmarks"].IsSequence() ||
                 deployment["benchmarks"].size() == 0)) {
                std::cerr << "deployment '" << name
                          << "' benchmarks must be a non-empty sequence\n";
                return 1;
            }
            std::cout << "DEPLOY " << name << "\n";
            auto rime = prepare_deployment(source_dir, schema_id, cache_dir,
                                           deployment);
            if (*type == SpecType::test) {
                completed = test_runner->run_deployment(
                    *rime, schema_id, name, deployment) && completed;
            } else {
                if (!benchmark_runner->run_deployment(
                        *rime, schema_id, name, deployment)) {
                    return 1;
                }
            }
            std::cout << "\n";
        }

        if (*type == SpecType::test) {
            const int result = test_runner->finish();
            return completed && result == 0 ? 0 : (result == 0 ? 1 : result);
        }
        return benchmark_runner->finish();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        if (!arguments_parsed) {
            std::cerr << program;
        }
        return 1;
    }
}

static std::optional<SpecType>
spec_type(const std::string& path)
{
    if (has_suffix(path, ".benchmark.yaml")) {
        return SpecType::benchmark;
    }
    if (has_suffix(path, ".test.yaml")) {
        return SpecType::test;
    }
    return std::nullopt;
}

static bool
has_suffix(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}
