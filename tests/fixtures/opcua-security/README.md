# OPC-UA Sign&Encrypt test material (REQ-INTEGRATION-011)

Self-signed application certificates used by `Open62541SecurityMaterialTest`
and `Open62541ServerSecurityTest` to exercise the OPC-UA `SignAndEncrypt`
mode (ADR-0031).

These are throwaway test keys. They are **not** secrets, they are **not**
used by any binary or deployment. Nothing outside `tests/` reads them.
They are committed so the security tests run with no generation step in CI.

Everything here is **DER**, not PEM. open62541 hands its PKI plugin raw bytes
and expects DER there, which is why the loader under test reads DER and why
these files do too. ADR-0031 records the divergence from the PEM material
`tests/fixtures/tls/` holds for the HTTP backend.

| File                    | What it is                                                        |
|-------------------------|-------------------------------------------------------------------|
| `server.der`            | Self-signed server application certificate, SAN `URI:urn:industrial-hmi:server` |
| `server-key.der`        | PKCS#8 private key for `server.der`                               |
| `client.der`            | Self-signed client application certificate, SAN `URI:urn:industrial-hmi:client` |
| `client-key.der`        | PKCS#8 private key for `client.der`                               |
| `trustlist/client.der`  | One-file trust list: the peer certificate a server trusts         |

The `URI:` entry in the subject alternative name is load-bearing, not
decoration. OPC-UA requires the `applicationUri` an application advertises to
match the URI inside the certificate it presents, and open62541 enforces it:
a mismatch is `BadCertificateUriInvalid` at connect time. The two URIs here
are exactly the `application_uri` defaults the server and the client ship
with.

The rejection cases (missing file, unset path, empty file, trust list that is
not a directory) are built in a temporary directory by the tests themselves
rather than committed. A committed empty directory cannot survive git, and a
committed empty file would be indistinguishable from a truncated one.

## Regenerating

Valid for 10 years from 2026-09-19, so CI does not rot.

```bash
cd tests/fixtures/opcua-security

cat > server.cnf <<'CNF'
[req]
distinguished_name = dn
x509_extensions    = ext
prompt             = no

[dn]
CN = Industrial HMI OPC-UA Server
O  = industrial-hmi test material

[ext]
basicConstraints     = critical,CA:FALSE
keyUsage             = critical,digitalSignature,nonRepudiation,keyEncipherment,dataEncipherment,keyCertSign
extendedKeyUsage     = serverAuth,clientAuth
subjectKeyIdentifier = hash
subjectAltName       = URI:urn:industrial-hmi:server,DNS:localhost,IP:127.0.0.1
CNF

# The client config is the same file with "Server" and ":server" swapped
# for "Client" and ":client".
sed -e 's/OPC-UA Server/OPC-UA Client/' \
    -e 's/urn:industrial-hmi:server/urn:industrial-hmi:client/' \
    server.cnf > client.cnf

for role in server client; do
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -sha256 \
        -config "${role}.cnf" \
        -keyout "${role}-key.pem" -outform DER -out "${role}.der"
    openssl pkcs8 -topk8 -nocrypt -in "${role}-key.pem" \
        -outform DER -out "${role}-key.der"
done

mkdir -p trustlist
cp client.der trustlist/client.der

rm -f server.cnf client.cnf server-key.pem client-key.pem
```

The `keyUsage` and `extendedKeyUsage` extensions are spelled out rather than
left to openssl's defaults. An OPC-UA application certificate carries a
different usage set from a plain TLS server certificate, and open62541's PKI
plugin inspects it, so a certificate that would serve HTTPS happily is not
automatically usable here.
