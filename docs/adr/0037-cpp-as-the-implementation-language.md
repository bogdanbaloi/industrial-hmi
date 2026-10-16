# ADR-0037: C++20 as the implementation language

## Status
Accepted (2026-10-16). Records, after the fact, a choice made at the start of
the project; every earlier ADR assumes it.

## Context
An industrial HMI sits between machines and people. It talks to PLCs and field
devices over industrial protocols, must react to device data without
unpredictable pauses, runs on anything from an industrial PC to an ARM board,
and is sold into automotive and manufacturing customers who audit how the code
is written. The language choice shapes all of that, so it is worth stating
why C++ was picked, not only that it was.

## Decision
Write the HMI in C++20 (CMake, `CMAKE_CXX_STANDARD 20`), for these reasons:

- **The industrial ecosystem is C/C++.** open62541 (OPC UA), Modbus stacks,
  vendor SDKs, Boost.Asio, Qt and embedded toolchains are native C/C++. No
  binding layer between the HMI and the libraries it depends on.
- **Determinism.** No garbage collector, so no unpredictable pauses on the path
  from a device value to the screen.
- **Explicit control of concurrency.** Threads, `std::jthread`, lock-free
  structures and explicit memory ordering (ADR-0018) are first-class.
- **Portability with a small footprint.** One codebase from industrial PC to
  ARM/embedded targets (serial and OTA to STM32, ADR-0029, ADR-0034).
- **Native UI toolkits.** GTK and Qt, the latter the de facto standard for
  industrial HMIs, with no browser in between (ADR-0020).
- **Standards the customers ask for.** MISRA (ADR-0028) and ASPICE-style
  traceability (ADR-0012) target C/C++.
- **The author's background** is automotive and embedded C++, so the language
  also shows the skills the project is meant to demonstrate.

## Alternatives rejected
- **Rust.** Stronger memory safety, and used here for one component
  (ADR-0026, ADR-0027). Rejected as the main language because the industrial
  library and UI ecosystem is still thinner, and customers rarely ask for it yet.
- **C# / .NET (WPF).** Common for Windows HMIs. Rejected: a garbage collector on
  the data path, and strong ties to Windows.
- **Python.** Fast to write; fits the audit and AI side. Rejected for the HMI:
  no determinism, slower, harder to ship to embedded targets.
- **Web stack (JS/TS UI).** Easy UI, but the protocol backend would still need
  another language, and a browser adds weight on the operator panel.

## Consequences
- C++ needs discipline on memory and threads. The project pays for it with
  sanitizers, ThreadSanitizer and fuzzers in CI, and a strict clang-tidy profile.
- Build times and dependency management are heavier than in managed languages.
- Components where memory safety matters more than ecosystem can still be
  written in Rust behind a C ABI (ADR-0027).
