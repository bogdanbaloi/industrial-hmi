#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <string>
#include <vector>

namespace app::config {

class ConfigManager;

/**
 * ConfigValidator -- semantic validation of a parsed ConfigManager.
 *
 * Responsibility:
 *   - Runs after ConfigManager::initialize() has populated the in-memory
 *     map from nlohmann/json. By that point the file is syntactically
 *     valid JSON; the validator is concerned only with VALUES (types,
 *     ranges, enums, cross-field invariants).
 *
 * Why a C++ validator instead of a runtime JSON-Schema library:
 *   - We already ship a typed accessor API on ConfigManager (getInt /
 *     getFloat / getValue). The validator hits exactly the same paths
 *     as production code, so a passing validation guarantees the
 *     accessors will see well-formed inputs.
 *   - Avoids pulling in a second heavy header-only dep (e.g.
 *     valijson, json-schema-validator) just to enforce ~20 rules.
 *   - The audit-grade SPEC lives alongside as `schemas/app-config.schema.json`
 *     (draft-07). The schema is the human-readable / tool-readable
 *     contract; the C++ validator is the runtime enforcement. They are
 *     kept in sync by code-review discipline and the validator's own
 *     unit tests. See ADR-0015.
 *
 * Contract:
 *   - Returns Result{ok=true} when every rule passes.
 *   - Returns Result{ok=false, errors=[...]} listing every failed
 *     rule (not just the first), so the operator sees all issues at
 *     once instead of "fix, re-run, see next error" loops.
 *
 * Threading: stateless free function; safe to call from any thread.
 */
class ConfigValidator {
public:
    struct Result {
        bool ok = true;
        std::vector<std::string> errors;
    };

    /// Validate the populated ConfigManager. The manager must already
    /// have returned true from initialize() -- the validator does not
    /// re-read the file.
    [[nodiscard]] static Result validate(const ConfigManager& cfg);
};

}  // namespace app::config

#endif  // CONFIG_VALIDATOR_H
