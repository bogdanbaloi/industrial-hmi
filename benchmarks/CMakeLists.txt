# Benchmark binaries.
#
# Each benchmark is its own executable so a regression in one hot path
# doesn't block a clean run on the others, and so CI can produce a
# per-target line in the summary without parsing multi-suite JSON.
#
# Google Benchmark provides BENCHMARK_MAIN() -- we use it everywhere
# rather than rolling a custom driver, because we want the standard
# CLI flags (--benchmark_min_time, --benchmark_filter,
# --benchmark_format=json for CI ingestion) for free.

include_directories(${CMAKE_SOURCE_DIR})

# AlertCenter -- ISA-18.2 alarm lifecycle. Exercised under realistic
# storm conditions (10 / 100 / 1000 concurrent active alarms) to
# capture how raise/ack/snapshot scale with active-set size.
# Header-only; needs sigc++ (re-exported transitively by GTKMM, same
# pattern as test_alert_center) but not objectsPresenter -- linking
# the full presenter object lib would drag in DashboardPresenter +
# ProductsPresenter and force us to satisfy their model deps too.
add_executable(bench_alert_center bench_alert_center.cpp)
target_link_libraries(bench_alert_center PRIVATE
    ${GTKMM_LIBRARIES}
    benchmark::benchmark
)
target_include_directories(bench_alert_center PRIVATE ${GTKMM_INCLUDE_DIRS})

# Modbus PDU codec -- pure encode / decode, no I/O. Captures the
# steady-state cost of the wire-format functions the Modbus master
# polls in a tight loop.
#
# We compile ModbusPdu.cpp directly into the benchmark binary rather
# than linking objectsModbus -- same defensive pattern as test_modbus_pdu
# (tests/CMakeLists.txt). Keeps the benchmark runnable even when
# BUILD_MODBUS_BACKEND is off, since the codec has no socket / async
# dependency.
add_executable(bench_modbus_pdu
    bench_modbus_pdu.cpp
    ${CMAKE_SOURCE_DIR}/src/integration/modbus/ModbusPdu.cpp
)
target_link_libraries(bench_modbus_pdu PRIVATE
    benchmark::benchmark
)

# ConfigManager parse -- cold-start latency. One-shot per process, so
# only the bootstrap path cares; included for symmetry with the
# parser-swap narrative in ADR-0015.
add_executable(bench_config_parse bench_config_parse.cpp)
target_link_libraries(bench_config_parse PRIVATE
    objectsConfig
    benchmark::benchmark
)
