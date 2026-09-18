# TLS test material (REQ-INTEGRATION-010)

Self-signed certificates used by `HttpTlsMaterialTest` and
`HttpBackendTlsTest` to exercise the HTTP backend's TLS mode (ADR-0030).

These are throwaway test keys. They are **not** secrets, they are **not**
used by any binary or deployment. Nothing outside `tests/` reads them.
They are committed so the TLS tests run with no generation step in CI.

| File              | What it is                                                 |
|-------------------|------------------------------------------------------------|
| `server.crt`      | Self-signed server certificate, `CN=localhost`, SAN `DNS:localhost` + `IP:127.0.0.1` |
| `server.key`      | Private key for `server.crt`                                |
| `client-ca.crt`   | Self-signed CA that issued `client.crt`. Also the `client_ca_path` for mutual TLS |
| `client.crt`      | Client certificate signed by `client-ca.crt`                |
| `client.key`      | Private key for `client.crt`                                |
| `malformed.pem`   | Deliberately not a PEM object, for the rejection test       |

`client.key` doubles as the "wrong key" in the mismatched-pair test: it is a
valid private key that does not belong to `server.crt`.

## Regenerating

Valid for 10 years from 2026-09-18, so CI does not rot. To regenerate the
whole set (the CA key is intentionally not committed, so client certificates
are re-issued from a fresh CA):

```bash
cd tests/fixtures/tls

# Server certificate + key.
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout server.key -out server.crt \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

# Client CA.
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout client-ca.key -out client-ca.crt \
    -subj "/CN=industrial-hmi-test-client-ca"

# Client certificate signed by that CA.
openssl req -newkey rsa:2048 -nodes \
    -keyout client.key -out client.csr \
    -subj "/CN=industrial-hmi-test-client"
openssl x509 -req -days 3650 -in client.csr \
    -CA client-ca.crt -CAkey client-ca.key -CAcreateserial \
    -out client.crt

rm -f client-ca.key client.csr client-ca.srl
```

`malformed.pem` is hand-written garbage. Any non-PEM bytes will do.
