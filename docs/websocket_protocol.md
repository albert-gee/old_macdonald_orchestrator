# WebSocket Protocol

The Dashboard connects to the Orchestrator over WSS at `/ws`. After the
WebSocket upgrade completes, the Orchestrator immediately sends a
`state_snapshot`. The Dashboard must not treat WebSocket send success as command
success; every command is complete only after a matching `command_result`.

## TLS Trust

Generated device TLS certificates and private keys are not repository assets.
Each physical Orchestrator must have its own TLS identity. Private keys must
never be committed. Dashboard trust is established by certificate fingerprint
pinning, trust-on-first-use during pairing, or a known development CA. Unexpected
certificate changes must be rejected unless the operator explicitly resets trust.

Development firmware may embed local `components/websocket_server/certs/servercert.pem`
and `components/websocket_server/certs/prvtkey.pem` files generated with:

```bash
components/websocket_server/certs/cert_generate.sh
```

Those files are ignored by Git.

## Command

```json
{
  "type": "command",
  "request_id": "client-generated-id",
  "action": "thread.status_get",
  "payload": {}
}
```

`request_id` is required and must be unique among pending Dashboard commands.

## Command Result

```json
{
  "type": "command_result",
  "request_id": "client-generated-id",
  "action": "thread.status_get",
  "ok": true,
  "payload": {
    "running": true
  }
}
```

Errors use the same envelope:

```json
{
  "type": "command_result",
  "request_id": "client-generated-id",
  "action": "matter.attribute_read",
  "ok": false,
  "error": {
    "code": "ESP_ERR_INVALID_ARG",
    "message": "ESP_ERR_INVALID_ARG"
  }
}
```

Unknown actions return `UNKNOWN_ACTION`.

## Protocol Error

```json
{
  "type": "error",
  "error": {
    "code": "INVALID_JSON",
    "message": "Message is not valid JSON"
  }
}
```

Other protocol error codes include `MISSING_TYPE`, `MISSING_REQUEST_ID`,
`MISSING_ACTION`, `INVALID_PAYLOAD`, and `UNSUPPORTED_TYPE`.

## Event

```json
{
  "type": "event",
  "event": "wifi.sta_connected",
  "payload": {
    "ip": "192.168.1.123"
  }
}
```

Legacy informational broadcasts may still be emitted for developer tooling, but
Dashboard control flow should use `state_snapshot`, `event`, and
`command_result`.

## State Snapshot

```json
{
  "type": "state_snapshot",
  "payload": {
    "wifi": {
      "mode": "apsta",
      "ap_running": true,
      "sta_configured": false,
      "sta_connected": false,
      "sta_ip": null,
      "rssi": null
    },
    "thread": {
      "enabled": false,
      "attached": false,
      "role": "disabled",
      "dataset_present": false
    },
    "matter": {
      "controller_initialized": false,
      "commissioned_nodes": []
    },
    "websocket": {
      "clients": 1
    }
  }
}
```

Snapshots never expose secrets.

## Operator Commands

- `chamber.status_get`
- `device.list`
- `device.get`
- `device.remove`
- `device.rename`
- `device.temperature.read`
- `device.pressure.read`
- `device.relay.set`
- `device.attribute.read`
- `device.attribute.subscribe`

Normal Dashboard controls should use device IDs and labels from `device.list`.
Raw Matter node IDs, endpoints, cluster IDs, and attribute IDs belong in a
developer/debug panel.

Temperature read:

```json
{
  "type": "command",
  "request_id": "req-temp-1",
  "action": "device.temperature.read",
  "payload": {
    "device_id": "root-temp-1"
  }
}
```

Relay set:

```json
{
  "type": "command",
  "request_id": "req-relay-1",
  "action": "device.relay.set",
  "payload": {
    "device_id": "mist-relay-1",
    "on": true
  }
}
```

## Developer Commands

### Wi-Fi

### `wifi.sta_connect`

- `ssid`: string
- `password`: string

### Thread

### `thread.enable`

No fields.

### `thread.disable`

No fields.

### `thread.status_get`

No fields.

### `thread.attached_get`

No fields.

### `thread.role_get`

No fields.

### `thread.active_dataset_get`

No fields.

### `thread.unicast_addresses_get`

No fields.

### `thread.multicast_addresses_get`

No fields.

### `thread.br_init`

No fields.

### `thread.br_deinit`

No fields.

### `thread.dataset.init`

- `channel`: number
- `pan_id`: number
- `network_name`: string
- `extended_pan_id`: string
- `mesh_local_prefix`: string
- `master_key`: string
- `pskc`: string

### Matter

### `matter.controller_init`

- `node_id`: string unsigned decimal uint64
- `fabric_id`: number
- `listen_port`: number

### `matter.pair_ble_thread`

- `node_id`: string unsigned decimal uint64
- `setup_code`: string unsigned decimal uint32
- `discriminator`: string unsigned decimal uint16

### `matter.cluster_command_invoke`

- `destination_id`: string unsigned decimal uint64
- `endpoint_id`: number
- `cluster_id`: number
- `command_id`: number
- `command_data`: string

### `matter.attribute_read`

- `node_id`: string unsigned decimal uint64
- `endpoint_id`: number
- `cluster_id`: number
- `attribute_id`: number

### `matter.attribute_subscribe`

- `node_id`: string unsigned decimal uint64
- `endpoint_id`: number
- `cluster_id`: number
- `attribute_id`: number
- `min_interval`: number
- `max_interval`: number
