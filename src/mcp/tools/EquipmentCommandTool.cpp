#include "src/mcp/tools/EquipmentCommandTool.h"

#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/presenter/DashboardPresenter.h"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>

namespace app::mcp {

namespace {

using app::core::Result;
using app::auth::Role;

// Wire-level key + value strings for the acknowledgement payload and the input
// schema, named so they are not spelled inline (clang-tidy does not police
// magic strings, so the discipline is enforced by hand).
inline constexpr const char* kArgCommand       = "command";
inline constexpr const char* kResultCommandKey = "command";
inline constexpr const char* kResultStatusKey  = "status";
inline constexpr const char* kStatusAccepted   = "accepted";

// A pointer to one of the presenter's parameterless operator handlers -- the
// exact entry point a human button calls.
using PresenterAction = void (DashboardPresenter::*)();

// A role predicate (auth::canStartStop / canResetSystem). Typed noexcept so the
// constexpr table binds the noexcept auth helpers directly.
using RolePredicate = bool (*)(Role) noexcept;

// Single source of truth for the EquipmentCommand <-> wire-name mapping, the
// permission each command requires, and the presenter handler it routes to.
// The descriptor's `enum`, argument parsing and execution all derive from this
// one table so the command vocabulary is never spelled out in more than one
// place (same discipline as HistorianQueryTool's kFieldMappings).
struct CommandMapping {
    EquipmentCommand command;
    const char*      name;
    RolePredicate    allowed;
    PresenterAction  invoke;
};
constexpr auto kCommandMappings = std::to_array<CommandMapping>({
    {.command = EquipmentCommand::Start,
     .name    = "start",
     .allowed = &app::auth::canStartStop,
     .invoke  = &DashboardPresenter::onStartClicked},
    {.command = EquipmentCommand::Stop,
     .name    = "stop",
     .allowed = &app::auth::canStartStop,
     .invoke  = &DashboardPresenter::onStopClicked},
    {.command = EquipmentCommand::ResetRestart,
     .name    = "reset",
     .allowed = &app::auth::canResetSystem,
     .invoke  = &DashboardPresenter::onResetRestartClicked},
});

const CommandMapping& mappingFor(EquipmentCommand command) {
    for (const auto& mapping : kCommandMappings) {
        if (mapping.command == command) {
            return mapping;
        }
    }
    return kCommandMappings.front();
}

bool commandFromName(const std::string& name, EquipmentCommand& out) {
    for (const auto& mapping : kCommandMappings) {
        if (name == mapping.name) {
            out = mapping.command;
            return true;
        }
    }
    return false;
}

nlohmann::json commandNameEnum() {
    nlohmann::json names = nlohmann::json::array();
    for (const auto& mapping : kCommandMappings) {
        names.push_back(mapping.name);
    }
    return names;
}

}  // namespace

nlohmann::json equipmentCommandDescriptor() {
    return {
        {"name", kEquipmentCommandTool},
        {"description",
         "Change the production line state (start / stop / reset). "
         "State-changing and audited; the agent role must permit the action "
         "(start/stop need Operator, reset needs Maintenance)."},
        {"inputSchema",
         {{"type", "object"},
          {"properties",
           {{kArgCommand,
             {{"type", "string"},
              {"enum", commandNameEnum()},
              {"description",
               "Line command: start or stop production, or reset/restart "
               "the system."}}}}},
          {"required", {kArgCommand}}}},
    };
}

Result<EquipmentCommand, McpErrorCode>
parseEquipmentCommandArgs(const nlohmann::json& args) {
    using Res = Result<EquipmentCommand, McpErrorCode>;
    if (!args.is_object()) {
        return Res{app::core::Err, McpErrorCode::InvalidParams};
    }

    const std::string name = args.value(kArgCommand, std::string{});
    EquipmentCommand  command{EquipmentCommand::Start};
    if (!commandFromName(name, command)) {
        return Res{app::core::Err, McpErrorCode::InvalidParams};
    }
    return Res{app::core::Ok, command};
}

Result<nlohmann::json, McpErrorCode>
runEquipmentCommand(DashboardPresenter& presenter, const auth::Session& session,
                    EquipmentCommand command) {
    using Res = Result<nlohmann::json, McpErrorCode>;
    const CommandMapping& mapping = mappingFor(command);

    // Explicit internal authorization gate (ADR-0024, the interesting bit).
    // The presenter role-gates every operator action through one checkRole(),
    // but that gate returns TRUE for a null session by design (auth-disabled
    // dev builds) and refuses a wired-but-unauthorised action with a *void*
    // early return. A write tool must not lean on the presenter alone.
    const std::optional<app::auth::User> agent = session.currentUser();
    if (!agent.has_value()) {
        // No authenticated agent behind this session. The server only enables
        // writes with a synthetic agent user set on the session (ADR-0024), so
        // an empty session here is an internal wiring fault -- refuse BEFORE
        // touching the presenter, or its null-session pass-through would run an
        // ungated write.
        return Res{app::core::Err, McpErrorCode::Unauthorized};
    }

    // Route through the same handler a human button calls. With an authenticated
    // agent session guaranteed above, the presenter's own checkRole audits the
    // attempt (SUCCESS when permitted, FAILURE otherwise) and performs or no-ops
    // accordingly, so the audit trail is identical to a human click and is not
    // re-implemented here.
    (presenter.*mapping.invoke)();

    // The presenter's gate is void, so re-evaluate the same permission to shape
    // the JSON-RPC reply: a structured Unauthorized error for the (now audited)
    // refused no-op, instead of a misleading success.
    if (!mapping.allowed(agent->role)) {
        return Res{app::core::Err, McpErrorCode::Unauthorized};
    }

    return Res{app::core::Ok,
               nlohmann::json{{kResultCommandKey, mapping.name},
                              {kResultStatusKey, kStatusAccepted}}};
}

}  // namespace app::mcp
