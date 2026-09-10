#include "src/core/Bootstrap.h"
#include "src/mcp/McpInitRoot.h"

#include <exception>
#include <iostream>

// Entry point for the opt-in MCP server binary. Kept separate from the shared
// src/main.cpp (GTK / console / Qt) rather than adding a fourth #ifdef branch:
// this consumer has no View toolkit and its own composition root.
//
// stdout is the JSON-RPC channel, so every diagnostic goes to stderr.
int main() {
    // Capture the real stdout, then point std::cout at stderr so every log line
    // the app writes to std::cout lands on stderr instead of corrupting the
    // JSON-RPC stream. The MCP server is handed the captured real stdout.
    std::ostream protocolOut(std::cout.rdbuf());
    std::cout.rdbuf(std::cerr.rdbuf());

    try {
        app::core::Bootstrap bootstrap;
        bootstrap.run();
        app::mcp::McpInitRoot root(bootstrap);
        return root.run(protocolOut);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Fatal: unknown error\n";
        return 3;
    }
}
