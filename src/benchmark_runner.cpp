#include "benchmark_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <string_view>
#include <vector>

#include "deployment.h"
#include "output_collector.h"

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct Statistics {
    double mean;
    double p50;
    double p95;
    double p99;
    double maximum;
};

static std::optional<std::vector<std::string>> split_key_sequence(
    const std::string& keys);
static bool run_iteration(Rime& rime,
                          const std::string& schema_id,
                          const YAML::Node& options,
                          const std::vector<std::string>& keys,
                          std::vector<double>* key_samples,
                          double* iteration_total);
static Statistics calculate_statistics(const std::vector<double>& samples);
static double nearest_rank(const std::vector<double>& sorted, double percentile);
static void print_statistics(std::string_view label, const Statistics& stats);

BenchmarkRunner::BenchmarkRunner(const YAML::Node& document)
{
    if (!document["warmup"] || !document["iterations"]) {
        std::cerr << "warmup and iterations must be explicitly configured\n";
        return;
    }
    const auto configured_warmup = document["warmup"].as<long long>();
    const auto configured_iterations = document["iterations"].as<long long>();
    if (configured_warmup < 0 || configured_iterations <= 0) {
        std::cerr << "warmup must be at least 0 and iterations must be greater than 0\n";
        return;
    }
    warmup = static_cast<std::size_t>(configured_warmup);
    iterations = static_cast<std::size_t>(configured_iterations);
    if (!document["deploy"] || !document["deploy"].IsMap() ||
        document["deploy"].size() == 0) {
        std::cerr << "benchmark deploy must be a non-empty map\n";
        return;
    }
    initialized = true;
}

bool
BenchmarkRunner::ready() const
{
    return initialized;
}

bool
BenchmarkRunner::run_deployment(Rime& rime,
                                const std::string& schema_id,
                                const std::string& deployment_name,
                                const YAML::Node& deployment)
{
    for (const auto& benchmark : deployment["benchmarks"]) {
        const auto name = benchmark["name"].as<std::string>();
        const auto send = benchmark["send"].as<std::string>();
        const auto keys = split_key_sequence(send);
        const auto benchmark_id = schema_id + "::" + deployment_name + "::" + name;
        if (!keys) {
            std::cerr << "benchmark '" << benchmark_id
                      << "' has an invalid key sequence\n";
            return false;
        }
        if (keys->empty()) {
            std::cerr << "benchmark '" << benchmark_id
                      << "' has an empty key sequence\n";
            return false;
        }

        std::vector<double> key_samples;
        std::vector<double> iteration_totals;
        key_samples.reserve(iterations * keys->size());
        iteration_totals.reserve(iterations);
        std::string stdout_text, stderr_text;
        bool succeeded = true;
        {
            OutputCollector collector(stdout_text, stderr_text);
            for (std::size_t i = 0; i < warmup; ++i) {
                if (!run_iteration(rime, schema_id, deployment["options"], *keys,
                                   nullptr, nullptr)) {
                    succeeded = false;
                    break;
                }
            }
            for (std::size_t i = 0; succeeded && i < iterations; ++i) {
                double total = 0.0;
                if (!run_iteration(rime, schema_id, deployment["options"], *keys,
                                   &key_samples, &total)) {
                    succeeded = false;
                    break;
                }
                iteration_totals.push_back(total);
            }
        }
        if (!succeeded) {
            std::cerr << "benchmark '" << benchmark_id << "' failed\n";
            if (!stdout_text.empty()) {
                std::cerr << "\n========= STDOUT =========\n" << stdout_text << "\n";
            }
            if (!stderr_text.empty()) {
                std::cerr << "\n========= STDERR =========\n" << stderr_text << "\n";
            }
            return false;
        }

        std::cout << "- " << benchmark_id
                  << "... warmup=" << warmup
                  << " iterations=" << iterations
                  << " keys=" << keys->size() << "\n";
        print_statistics("per-key latency", calculate_statistics(key_samples));
        print_statistics("iteration key-time total",
                         calculate_statistics(iteration_totals));
        ++completed;
    }
    return true;
}

int
BenchmarkRunner::finish() const
{
    if (completed == 0) {
        std::cerr << "no deployments matched the requested regex\n";
        return 1;
    }
    std::cout << completed << " benchmarks completed.\n";
    return 0;
}

static std::optional<std::vector<std::string>>
split_key_sequence(const std::string& keys)
{
    std::vector<std::string> result;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == '{' && i + 1 < keys.size()) {
            const auto end = keys.find('}', i + 1);
            if (end == std::string::npos) {
                return std::nullopt;
            }
            result.push_back(keys.substr(i, end - i + 1));
            i = end;
            continue;
        }
        result.push_back(keys.substr(i, 1));
    }
    return result;
}

static bool
run_iteration(Rime& rime,
              const std::string& schema_id,
              const YAML::Node& options,
              const std::vector<std::string>& keys,
              std::vector<double>* key_samples,
              double* iteration_total)
{
    auto session = prepare_session(rime, schema_id, options);
    if (!session) {
        return false;
    }

    double total = 0.0;
    for (const auto& key : keys) {
        const auto start = Clock::now();
        if (!session->send_key_sequence(key)) {
            return false;
        }
        session->read_result(/* include_candidates */ false);
        const auto elapsed = Duration(Clock::now() - start).count();
        total += elapsed;
        if (key_samples) {
            key_samples->push_back(elapsed);
        }
    }
    if (iteration_total) {
        *iteration_total = total;
    }
    return true;
}

static Statistics
calculate_statistics(const std::vector<double>& samples)
{
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    return {
        std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size(),
        nearest_rank(sorted, 0.50),
        nearest_rank(sorted, 0.95),
        nearest_rank(sorted, 0.99),
        sorted.back(),
    };
}

static double
nearest_rank(const std::vector<double>& sorted, double percentile)
{
    const auto rank = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(sorted.size())));
    return sorted[rank - 1];
}

static void
print_statistics(std::string_view label, const Statistics& stats)
{
    std::cout << std::fixed << std::setprecision(3)
              << "  " << label << " (ms):"
              << " mean=" << stats.mean
              << " p50=" << stats.p50
              << " p95=" << stats.p95
              << " p99=" << stats.p99
              << " max=" << stats.maximum << "\n";
}
