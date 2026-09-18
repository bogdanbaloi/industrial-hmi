# ADR-0030: TLS for the HTTP/REST backend via cpp-httplib's SSL variant

## Status
Accepted (2026-09-18).

## Context
Six integration backends speak over a network (TCP, MQTT, Modbus, OPC-UA,
HTTP, serial). None of them encrypts anything. The standing answer, recorded
in the README, has been "tunnel through stunnel": correct for a hand-rolled
protocol, but it means the binary itself has never demonstrated a TLS
boundary. It cannot demonstrate client-certificate authentication at all.

One of those six is different: the HTTP backend is built on cpp-httplib,
which already ships an OpenSSL path. `httplib::SSLServer` derives from the
`httplib::Server` that `HttpBackend` keeps behind its PIMPL, so TLS there is
a different concrete server, not a new abstraction.

The first question is where to terminate TLS. The second is how a bad
certificate should behave.

## Decision
Terminate TLS in-process for the HTTP backend only, using cpp-httplib's
built-in OpenSSL support, configured under `network.http.tls`
(`enabled`, `cert_path`, `key_path`, `verify_peer`, `client_ca_path`).

- **Native TLS, not a reverse proxy.** cpp-httplib's `SSLServer` gives both
  wire encryption and, with a client CA configured, client-certificate
  verification (mutual TLS). That is the part a proxy cannot demonstrate from
  inside this binary. It is also the part worth showing: the server decides who
  may talk to it, not just that the talk is encrypted.
- **Bad material is a fatal startup error, never a plaintext fallback.** The
  cert, key and client CA are loaded and verified by `HttpTlsMaterial` in the
  `HttpBackend` CONSTRUCTOR, which throws `core::TlsMaterialError` (a
  `CriticalStartupError`). Construction happens in `registerHttpBackend`,
  inside the same top-level `try` in `main()` that already handles a bad
  config or a dead database. It deliberately does NOT happen inside
  `IntegrationManager::startAll()`, which swallows a per-backend exception so
  one failed backend cannot keep the others down: that forgiving behaviour is
  right for a busy port and wrong for a certificate the operator asked for and
  cannot have.
- **The key is checked against the certificate.** `X509_check_private_key`
  catches the mismatched pair that would otherwise pass every file-exists
  check and then fail every handshake at run time.
- **Config validation stays shape-only.** `ConfigValidator` asks whether the
  operator supplied the paths their own settings require. It does not open
  files and does not link OpenSSL: it is compiled into every build, including
  ones without the HTTP backend.
- **Explicit non-goal: TCP, MQTT and Modbus stay stunnel-documented.** They
  are hand-rolled protocols over raw sockets with no TLS library boundary.
  Adding native TLS there is materially more work and a separate decision.

## Alternatives rejected
- **stunnel / reverse proxy only.** Zero new dependency and the standard
  production answer, but the binary can then never show mutual TLS. The
  demo also needs an external process. Kept as the documented answer for
  the other backends, not for HTTP.
- **TLS on every backend now.** Out of scope: four more protocols, each with
  its own hand-rolled socket loop, for the same demonstrated capability.
  Tracked as follow-on work.
- **Validate the certificate lazily, on the first request.** Simpler wiring,
  but a broken deployment would bind the port and answer with confusing
  per-request 500s. The operator would learn about it from a user rather
  than from startup.
- **Fall back to plaintext when the certificate cannot be loaded.** Rejected
  outright. Serving cleartext on a port the operator configured as HTTPS is
  worse than not starting.

## Consequences
- OpenSSL becomes a required dependency of the HTTP backend
  (`libssl-dev` on the Linux CI jobs, `mingw-w64-clang-x86_64-openssl` on
  the MSYS2 job). It is pulled in only where cpp-httplib already was.
- `CPPHTTPLIB_OPENSSL_SUPPORT` is defined on the `cpp_httplib_headers`
  interface target rather than on `objectsHttp`. The macro changes the layout
  of `httplib::Server`, so every translation unit that includes the header
  must agree on it. Hanging it off the header target makes a mismatch
  impossible to write by accident.
- A second Windows-only link appears. It is not Winsock: with OpenSSL
  support on, cpp-httplib's client reads the Windows system certificate store,
  so `crypt32` joins the link line.
- Self-signed test material is committed under `tests/fixtures/tls/` with the
  exact `openssl` commands that produced it, so the TLS tests need no
  generation step in CI.
- Honesty rail: this is TLS termination for one read-only backend, with
  certificates an operator supplies. It is not a PKI, there is no certificate
  rotation or revocation story (a restart picks up new material). The
  other five backends are unchanged.
