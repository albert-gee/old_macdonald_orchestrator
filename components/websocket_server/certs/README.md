# Orchestrator WebSocket TLS Certificates

Generated device TLS certificates and private keys are not repository assets.
Each physical Orchestrator must have its own TLS identity. Private keys must
never be committed.

Current development builds still embed local certificate files:

```bash
cd components/websocket_server/certs
./cert_generate.sh
```

This generates local `servercert.pem` and `prvtkey.pem` files for the developer
workstation. They are ignored by Git and must remain local.

If either embedded development file is missing, the WebSocket component CMake
configuration fails with a message telling the developer to run
`cert_generate.sh`. Do not work around that by committing generated keys or
certificates.

Production direction: generate a unique certificate/private key on first boot
or during provisioning, store it in device-local persistent storage such as NVS,
encrypted NVS, LittleFS/SPIFFS, or a provisioning partition, and load it when
starting the secure WebSocket server.

The Dashboard must deliberately trust an Orchestrator certificate. Supported
trust models are certificate fingerprint pinning, trust-on-first-use during
pairing, or a known development CA. Unexpected certificate fingerprint changes
must be rejected unless the operator explicitly resets trust.
