# ADR-0031: OPC-UA Sign&Encrypt via open62541's OpenSSL plugin

## Status
Accepted (2026-09-19).

## Context
ADR-0030 put a real TLS boundary inside the binary, but only for the HTTP
backend. The OPC-UA endpoints stayed on `SecurityPolicy#None`: every browse,
every subscription and every method call crossed the wire in clear text, with
no proof of who was on the other end. On a plant network that is the single
most visible gap in this project, because OPC-UA is the one protocol here that
ships a security model in its own specification rather than borrowing TLS.

OPC-UA does not reuse TLS. It defines its own SecureChannel layer with three
message security modes (`None`, `Sign`, `SignAndEncrypt`) and a set of named
security policies that fix the signature and encryption algorithms. The
identity on both ends is an application certificate whose `applicationUri`
extension must match the `applicationUri` the application advertises, which is
a stricter coupling than an HTTPS certificate's hostname check.

open62541 already ships that machinery behind `UA_ENABLE_ENCRYPTION`. The
question was which crypto backend to compile it against, and how a bad
certificate should behave.

## Decision
Enable `UA_ENABLE_ENCRYPTION=OPENSSL` on the vendored open62541 build and add
an opt-in `SignAndEncrypt` mode with the `Basic256Sha256` policy to both the
server and the client, configured under `network.opcua.server.security` and
`network.opcua.client.security` (`enabled`, `cert_path`, `private_key_path`,
`trust_list_dir`).

- **OpenSSL, not mbedTLS.** OpenSSL is already a required dependency of this
  build (ADR-0030 pulled it in for cpp-httplib, and every CI job plus the
  MSYS2 toolchain already installs it). `UA_ENABLE_ENCRYPTION` is an
  open62541 build option, not a new third-party package, so choosing the
  backend that is already present adds a compile-time cost and no supply-chain
  cost. mbedTLS would have meant a second crypto library in one binary, with
  two sets of CVE advisories to track for one capability.
- **Exactly one endpoint: `Basic256Sha256` with `SignAndEncrypt`.** It is the
  policy the OPC Foundation profile marks as the current baseline, and it is
  the one every contemporary client offers. Shipping a policy matrix would
  have multiplied the test surface without demonstrating anything the single
  policy does not.

  Getting to exactly one takes a step that is worth recording, because the
  obvious call is the wrong one.
  `UA_ServerConfig_setDefaultWithSecurityPolicies` sounds like the function
  for this and is not: it adds `SecurityPolicy#None` alongside the secure
  policies, so an endpoint configured as encrypted would still accept a
  plaintext client. `UA_ServerConfig_setDefaultWithSecureSecurityPolicies`
  excludes `None`, but still offers every remaining policy in BOTH `Sign` and
  `SignAndEncrypt`, which leaves a sign-only downgrade on the same port. So
  the server calls the secure variant for the PKI, trust list and access
  control it wires, then clears the endpoint array and adds back the single
  entry. Clearing and re-adding is exactly how open62541's own
  `UA_ServerConfig_addAllSecureEndpoints` builds that list, so this walks a
  supported path rather than a private one.

  The client pins the same policy and mode on its config instead of taking
  whatever the peer offers first. A client that negotiated downward would
  hide the very misconfiguration this feature exists to surface.
- **DER on disk, not PEM.** This is the one place the project deliberately
  diverges from ADR-0030, which loads PEM. open62541's `UA_ByteString`
  material is handed to the stack as raw bytes and its own PKI plugin expects
  DER, so a PEM path would mean converting on every load and carrying a
  second failure mode. The OPC-UA tooling ecosystem (UaExpert, the `certs`
  directories of most vendor stacks) is DER-first for the same reason.
- **Bad material is a fatal startup error, never a silent drop to `None`.**
  `Open62541SecurityMaterial` loads and proves the files in the SERVER and
  CLIENT constructors and throws `core::TlsMaterialError`, the same
  `CriticalStartupError` ADR-0030 introduced. The error is reused rather than
  a new code invented: from the operator's seat the pathology is identical, a
  configured certificate that cannot be used, and the recovery action is the
  same. Construction happens in `IntegrationBootstrap`, inside the top-level
  `try` in `main()`, and deliberately NOT inside
  `IntegrationManager::startAll()`, which swallows a per-backend exception so
  one failed backend cannot keep the others down.
- **Structural certificate validity is open62541's answer, not ours.**
  `UA_ServerConfig_setDefaultWithSecureSecurityPolicies` and
  `UA_ClientConfig_setDefaultEncryption` parse the certificate and key
  themselves and return a status code. We check that code and refuse on it.
  Hand-rolling a second X.509 parse with OpenSSL would mean two parsers that
  can disagree, and the one that matters is the one the stack actually uses.
  `Open62541SecurityMaterial` answers only the question open62541 cannot:
  which configured path is missing, unreadable or empty. That is the answer
  an operator can act on, because a `BadCertificateInvalid` status code names
  no file.
- **`Open62541SecurityMaterial` does not hide open62541.** Every other type
  under `src/integration/opcua/` keeps the C stack behind a PIMPL. This one
  cannot: its whole purpose is to hand `UA_ByteString` values to
  `UA_ServerConfig`, so the type appears in its public signature. The
  trade-off is recorded in the class comment rather than papered over with a
  wrapper that would only re-expose the same type one layer down.
- **Config validation stays shape-only.** `ConfigValidator::checkOpcUaSecurity`
  asks whether the operator supplied the paths their own settings require. It
  opens no files and links no OpenSSL, because the validator compiles into
  every build including ones without the OPC-UA backend.

## Alternatives rejected
- **mbedTLS as the crypto backend.** open62541 supports it and it is the
  smaller library, but it would be the second TLS implementation linked into
  one binary purely because a build flag defaulted that way. Rejected on
  dependency hygiene, not on technical merit.
- **A policy matrix (`Basic128Rsa15`, `Aes128Sha256RsaOaep`,
  `Aes256Sha256RsaPss`, plus `Sign` without encryption).** More configuration
  surface, more combinations to test, no additional capability demonstrated.
  `Basic128Rsa15` is deprecated by the OPC Foundation and would be an actively
  bad default to offer.
- **PEM material, for symmetry with ADR-0030.** Rejected above: it would add a
  conversion step on a path whose failure mode is already the interesting
  part.
- **A full PKI: issuer lists, revocation lists, certificate rotation.** The
  trust list here is a directory of peer certificates, one per file, read once
  at startup. A revocation story needs a CRL fetch policy and a refresh
  schedule, which is a product decision rather than a demonstration.
- **`UA_ServerConfig_setBasics_withPort` plus a hand-added
  `Basic256Sha256` policy.** This reaches the single endpoint without
  clearing anything, and was the first shape tried. Rejected because
  `setBasics` wires no PKI: the trust list and the access-control plugin
  would both have to be assembled by hand, including a
  `UA_CertificateGroup_Memorystore` call and its parameter map. That is
  materially more of open62541's internals to own, for the same endpoint
  list, and a mistake in it looks like a server that trusts everybody.
- **Trusting `UA_ServerConfig_setDefaultWithSecurityPolicies` on its name.**
  Recorded here because it is the trap: that function offers a `None`
  endpoint, so it would have shipped a port the operator believes is
  encrypted and a plaintext client can still use.
  `Open62541ServerSecurityTest.RejectsPlaintextClientWhenSignAndEncryptRequired`
  is the test that would have caught it, and it is the reason that test
  exists.

## Consequences
- `UA_ENABLE_ENCRYPTION=OPENSSL` is set before `FetchContent_MakeAvailable`,
  and `find_package(OpenSSL REQUIRED)` is repeated inside the
  `BUILD_OPCUA_BACKEND` block. The earlier call lives inside the
  `BUILD_HTTP_BACKEND OR BUILD_TESTS` block, so a build with OPC-UA on and
  HTTP off would otherwise reach the encryption path with no OpenSSL targets
  defined. `find_package` is idempotent, so the repetition is free.
- open62541 compiles its PKI and SecurityPolicy plugins on top of the existing
  core, so both build time and the static library grow. That is the price of
  the capability and it is paid only when `BUILD_OPCUA_BACKEND=ON`.
- `open62541::open62541` carries `OpenSSL::SSL` and `OpenSSL::Crypto` as
  interface link dependencies, so `objectsOpcUa` needs no extra link line.
  This was verified on the MSYS2 CLANG64 build rather than assumed: the
  equivalent gap for cpp-httplib (`crypt32`) surfaced only at link time in a
  test target.
- `OpcUaServer` gains no new method. Reporting the negotiated mode is an
  orthogonal capability (`OpcUaSecurityReport`), which is what that
  interface's own comment already prescribed for encryption. `OpcUaBackend`
  queries it and omits the field for a server that does not offer it, so
  `MockOpcUaServer` is untouched.
- The `Basic256Sha256` policy URI is spelled as a named constant in our code.
  open62541 v1.5.4 exports a `UA_String` for the `None` policy URI only, so
  there is no library symbol to use for this one. It is safe to spell because
  nothing trusts it blind: `UA_ServerConfig_addEndpoint` looks it up in the
  policy list the library itself built and returns `BadInvalidArgument` when
  it does not match, so a typo produces a server that refuses to start rather
  than one that quietly offers no secure endpoint.
- The secure config still LOADS every secure policy open62541 supports; only
  the endpoint list is reduced. A policy with no endpoint is not reachable, so
  the wire behaviour is the single endpoint, but `GetEndpoints` output and the
  memory footprint both reflect the full policy set.
- Self-signed DER test material is committed under
  `tests/fixtures/opcua-security/` with the exact `openssl` commands that
  produced it, so the tests need no generation step in CI.
- Honesty rail: this is one policy, one mode, opt-in, with certificates an
  operator supplies and a trust list read once at startup. It is not a PKI.
  There is no rotation and no revocation (a restart picks up new material),
  and no user-identity token beyond the application certificate. The other
  backends are unchanged and remain stunnel-documented.
