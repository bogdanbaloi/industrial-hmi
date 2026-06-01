# Fuzz harnesses for the wire parsers.
#
# Each target compiles the parser sources directly into the fuzz binary
# (rather than linking the integration object libs) so the fuzz job can
# turn on libFuzzer + ASan + UBSan instrumentation without having to
# rebuild the entire stack with sanitizers on.
#
# Each harness is its own executable. libFuzzer keeps the corpus directory
# (passed as argv[1] at runtime) per binary, so isolating the targets
# also isolates their corpora -- a finding in one parser doesn't pollute
# the seeds for another.

include_directories(${CMAKE_SOURCE_DIR})

# Sanitizer + libFuzzer wiring. AddressSanitizer + UndefinedBehavior
# catch the lion's share of memory-corruption / UB bugs the fuzzer
# uncovers; the in-process libFuzzer driver provides coverage feedback.
# Applied per-target (rather than as a directory-wide -fsanitize) so
# the rest of the build tree stays untouched when BUILD_FUZZERS is the
# only opt-in.
set(FUZZ_FLAGS
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
    -O1
)

function(add_fuzz_target name)
    cmake_parse_arguments(FUZZ "" "" "SOURCES" ${ARGN})
    add_executable(${name} ${FUZZ_SOURCES})
    target_compile_options(${name} PRIVATE ${FUZZ_FLAGS})
    target_link_options(${name}    PRIVATE ${FUZZ_FLAGS})
endfunction()

# Modbus MBAP/PDU decoder -- the surface most likely to encounter
# adversarial bytes (untrusted PLC, network man-in-the-middle, or a
# poorly-tested third-party master). Compiles ModbusPdu.cpp directly
# (no socket, no async, no objectsModbus dep).
add_fuzz_target(fuzz_modbus_decode
    SOURCES
        fuzz_modbus_decode.cpp
        ${CMAKE_SOURCE_DIR}/src/integration/modbus/ModbusPdu.cpp
)

# MQTT PUBLISH parser -- inbound subscriber path. A broker (or peer
# masquerading as one) sending malformed PUBLISH packets must not
# crash the HMI; this proves the property under continuous coverage.
add_fuzz_target(fuzz_mqtt_publish
    SOURCES
        fuzz_mqtt_publish.cpp
        ${CMAKE_SOURCE_DIR}/src/integration/MqttPacket.cpp
)

# MQTT variable-byte length decoder -- the smallest-surface, most
# error-prone routine in the MQTT protocol (4-byte continuation chain,
# 7-bit-per-byte encoding). Worth its own harness because every
# packet parser hits it first.
add_fuzz_target(fuzz_mqtt_remaining_length
    SOURCES
        fuzz_mqtt_remaining_length.cpp
        ${CMAKE_SOURCE_DIR}/src/integration/MqttPacket.cpp
)
