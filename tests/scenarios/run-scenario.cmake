# Scenario runner invoked by ctest.
#
# Pipes SCENARIO_INPUT into CONSOLE_BIN, captures stdout, filters out
# logger timestamp lines (they change every run), normalises line
# endings, and compares against SCENARIO_EXPECTED. Exits 0 on match,
# 1 on mismatch (with a diff printed so the CI log is actionable).
#
# Invoked like:
#   cmake
#     -DCONSOLE_BIN=$<TARGET_FILE:industrial-hmi-console>
#     -DSCENARIO_INPUT=<path>/start-stop.txt
#     -DSCENARIO_EXPECTED=<path>/start-stop.expected
#     -DSCENARIO_NAME=start-stop
#     -P run-scenario.cmake

if(NOT DEFINED CONSOLE_BIN OR NOT EXISTS "${CONSOLE_BIN}")
    message(FATAL_ERROR "CONSOLE_BIN not set or binary missing: ${CONSOLE_BIN}")
endif()
if(NOT DEFINED SCENARIO_INPUT OR NOT EXISTS "${SCENARIO_INPUT}")
    message(FATAL_ERROR "SCENARIO_INPUT missing: ${SCENARIO_INPUT}")
endif()
if(NOT DEFINED SCENARIO_EXPECTED OR NOT EXISTS "${SCENARIO_EXPECTED}")
    message(FATAL_ERROR "SCENARIO_EXPECTED missing: ${SCENARIO_EXPECTED}")
endif()
if(NOT DEFINED SCENARIO_NAME)
    set(SCENARIO_NAME "unnamed")
endif()

# Force a predictable locale so gettext returns source English (we
# aren't testing translations here) and number/date formatting is
# consistent across Linux / Windows CI runners.
set(ENV{LANG}     "en_US.UTF-8")
set(ENV{LANGUAGE} "en")
set(ENV{LC_ALL}   "en_US.UTF-8")

execute_process(
    COMMAND           "${CONSOLE_BIN}"
    INPUT_FILE        "${SCENARIO_INPUT}"
    OUTPUT_VARIABLE   raw_stdout
    ERROR_QUIET                   # logger stderr is diagnostic only
    RESULT_VARIABLE   exit_code
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR
        "[${SCENARIO_NAME}] Binary exited with code ${exit_code}")
endif()

# Normalise line endings first (Windows runners give \r\n).
string(REPLACE "\r\n" "\n" filtered_stdout "${raw_stdout}")

# The logger shares stdout (per LoggerImpl ConsoleLogger). Its lines
# look like  "HH:MM:SS.mmm [LEVEL] file.h:NN  message"  -- the leading
# timestamp makes them non-deterministic. Strip them out so the
# scenario diff only sees structural event lines ([WORK_UNIT], ...)
# and our command output (--- STATUS ---, banner, etc.).
set(LOGGER_PATTERN "(^|\n)[0-9]+:[0-9]+:[0-9]+\\.[0-9]+ \\[[A-Z ]+\\] [^\n]*")
string(REGEX REPLACE "${LOGGER_PATTERN}" "" filtered_stdout "${filtered_stdout}")

# Alerts render "<title> at HH:MM:SS" from AlertCenter::formatNow.
# Mask the time so the alerts scenario matches regardless of clock.
string(REGEX REPLACE " at [0-9]+:[0-9]+:[0-9]+"
                     " at <TIME>"
                     filtered_stdout "${filtered_stdout}")

# Product detail renders "created:  YYYY-MM-DD HH:MM:SS" from the DB
# row's createdAt column. Same timestamp story -- mask it.
string(REGEX REPLACE "created: +[0-9-]+ [0-9:]+"
                     "created:  <TIMESTAMP>"
                     filtered_stdout "${filtered_stdout}")

# Collapse leading blank line that may appear after a stripped logger
# line, so the diff isn't sensitive to where the log happened to fire.
string(REGEX REPLACE "^\n+" "" filtered_stdout "${filtered_stdout}")

# Load expected (already in LF, see .gitattributes).
file(READ "${SCENARIO_EXPECTED}" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")

if(NOT "${filtered_stdout}" STREQUAL "${expected}")
    # Write the actual output next to the expected file so a developer
    # can `diff` it locally to see what changed. CI artifacts pick it
    # up automatically if the tests job uploads build dir.
    set(actual_path "${CMAKE_CURRENT_BINARY_DIR}/${SCENARIO_NAME}.actual")
    file(WRITE "${actual_path}" "${filtered_stdout}")

    message("----- expected -----")
    message("${expected}")
    message("----- actual -------")
    message("${filtered_stdout}")
    message("----- end ----------")
    message(FATAL_ERROR
        "[${SCENARIO_NAME}] output mismatch. "
        "Actual written to ${actual_path}")
endif()

message(STATUS "[${SCENARIO_NAME}] OK")
