// [utest->req~perf-001~1]
// Covers REQ-PERF-001 (reproducible microbenchmarks on hot paths).
//
// Benchmark: ConfigManager cold-start parse.
//
// Why this matters: the parser swap in ADR-0015 (hand-rolled flat parser
// -> nlohmann/json) traded a small steady-state hit for correctness on
// two known-buggy edge cases. This benchmark gives the swap a number
// rather than a hand-wave -- "the parser change costs O(X us) on a
// realistic config" is a defensible claim, "it's fast enough" is not.
//
// This is a single-shot path (one parse per process), so we run it
// inside Google Benchmark to get statistical bounds rather than a
// single timing point. The work-unit is one full re-parse of a
// realistic-shape config (~30 leaf keys, three nesting levels).

#include <benchmark/benchmark.h>

#include "src/config/ConfigManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using app::config::ConfigManager;

namespace {

/// Path to a realistic-shape config file the benchmark parses every
/// iteration. Written once in main() to a unique temp path so parallel
/// benchmark invocations don't race on disk.
std::string g_configPath;

/// A representative config covering the main subsystems (application,
/// window, logging, i18n, network with three sub-backends, historian).
/// Sized to match what a deployed app actually loads -- benchmarking on
/// a 3-key toy file would be unrepresentative.
std::string makeRealisticConfig() {
    std::ostringstream os;
    os << R"({
  "application": { "name": "Industrial HMI", "version": "0.13.0" },
  "window": { "title": "Industrial HMI", "default_width": 1280, "default_height": 800 },
  "theme":  { "default": "industrial" },
  "ui":     { "palette": "industrial", "multistation_enabled": false },
  "i18n":   { "language": "auto" },
  "logging": {
    "level": "INFO",
    "file": "logs/industrial-hmi.log",
    "max_file_size_mb": 10,
    "max_files": 5,
    "console": true
  },
  "auth": { "enabled": false, "db_path": "data/users.db" },
  "historian": {
    "enabled": true,
    "db_path": "data/historian.db",
    "batch_size": 200,
    "batch_age_ms": 1000,
    "sweep_interval_ms": 60000,
    "raw_retention_ms":    86400000,
    "minute_retention_ms": 604800000
  },
  "network": {
    "tcp":  { "enabled": false, "port": 5555 },
    "mqtt": {
      "enabled": false,
      "broker_host": "127.0.0.1",
      "broker_port": 1883,
      "client_id": "industrial-hmi",
      "topic_prefix": "factory/",
      "emit_plain_text": true,
      "emit_json": false,
      "subscriber": { "enabled": false, "topic_prefix": "factory/in/" }
    },
    "modbus": {
      "enabled": false,
      "host": "127.0.0.1",
      "port": 5020,
      "poll_interval_ms": 250,
      "connect_timeout_ms": 1000,
      "request_timeout_ms": 500,
      "slave_id": 1,
      "equipment_count": 3,
      "quality_count": 3
    },
    "opcua": {
      "enabled": false,
      "port": 4840,
      "application_uri": "urn:industrial-hmi",
      "application_name": "Industrial HMI",
      "server": { "commands_enabled": true },
      "client": {
        "enabled": false,
        "endpoint": "opc.tcp://127.0.0.1:4840",
        "application_uri": "urn:industrial-hmi.client",
        "application_name": "Industrial HMI Client",
        "ingest_bridge": { "enabled": false, "topic_prefix": "opcua/" }
      }
    }
  }
})";
    return os.str();
}

}  // namespace

static void BM_ConfigManager_ParseRealistic(benchmark::State& state) {
    for (auto _ : state) {
        ConfigManager::instance().clear();
        // Mutable local so DoNotOptimize sees a non-const lvalue --
        // google/benchmark deprecated the const-ref overload because the
        // optimiser was free to elide it. The cast back to void silences
        // -Wunused-but-set-variable on the trailing assignment.
        bool ok = ConfigManager::instance().initialize(g_configPath);
        benchmark::DoNotOptimize(ok);
        (void)ok;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ConfigManager_ParseRealistic)
    ->Unit(benchmark::kMicrosecond)
    ->Repetitions(10);

int main(int argc, char** argv) {
    const auto path = fs::temp_directory_path() /
                      "industrial-hmi-bench-config.json";
    g_configPath = path.string();
    std::ofstream out(path, std::ios::trunc);
    out << makeRealisticConfig();
    out.close();

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    std::error_code ec;
    fs::remove(path, ec);
    return 0;
}
