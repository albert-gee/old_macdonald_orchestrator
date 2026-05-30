# WebSocket Protocol

The Dashboard connects to the Orchestrator over WSS at `/ws`. After the
WebSocket upgrade completes, the Orchestrator sends a `state_snapshot` to the
new client. A WebSocket send returning `ESP_OK` only means the message was
queued; command success is represented only by a matching `command_result`.

## TLS Trust

Generated device TLS certificates and private keys are local development assets,
not repository assets. Each physical Orchestrator must have its own TLS
identity. Private keys must never be committed.

Development builds may embed local files:

```bash
cd components/websocket_server/certs
./cert_generate.sh
```

This creates `servercert.pem`, `prvtkey.pem`, and local CA material. These files
are ignored by Git. The component CMake fails clearly if `servercert.pem` or
`prvtkey.pem` is missing.

Dashboard trust should use certificate fingerprint pinning,
trust-on-first-use during pairing, or an explicit development CA. Unexpected
fingerprint changes must be rejected unless the operator resets trust.

## Message Envelopes

### Command

```json
{
  "type": "command",
  "request_id": "req-1",
  "action": "thread.status_get",
  "payload": {}
}
```

`request_id` is required and is echoed in exactly one `command_result`.

### Command Result

```json
{
  "type": "command_result",
  "request_id": "req-1",
  "action": "thread.status_get",
  "ok": true,
  "payload": {
    "running": true
  }
}
```

Failures use `ok:false`:

```json
{
  "type": "command_result",
  "request_id": "req-1",
  "action": "unknown.action",
  "ok": false,
  "error": {
    "code": "UNKNOWN_ACTION",
    "message": "Unsupported command action: unknown.action"
  }
}
```

Payload construction failures are command failures. The firmware must not
return `ok:true` with an empty payload when the requested payload could not be
built.

### Protocol Error

Malformed envelopes are not command results:

```json
{
  "type": "error",
  "error": {
    "code": "INVALID_JSON",
    "message": "Message is not valid JSON"
  }
}
```

Other protocol errors include `MISSING_TYPE`, `MISSING_REQUEST_ID`,
`MISSING_ACTION`, `INVALID_PAYLOAD`, and `UNSUPPORTED_TYPE`.

### State Snapshot

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

Snapshots never expose Wi-Fi passwords, Thread keys, TLS private keys, or other
secrets.

### Event

```json
{
  "type": "event",
  "event": "thread.role_changed",
  "payload": {
    "role": "leader"
  }
}
```

Runtime changes emit events and state snapshots. Examples include
`wifi.ap_started`, `wifi.ap_stopped`, `wifi.sta_connected`,
`wifi.sta_disconnected`, `wifi.sta_got_ip`, `thread.enabled`,
`thread.disabled`, `thread.attached`, `thread.detached`,
`thread.role_changed`, `thread.dataset_changed`,
`matter.controller_initialized`, `websocket.client_connected`,
`websocket.client_disconnected`, and `device.registry_changed`.

Legacy `info` broadcasts may still exist for developer tools. Dashboard control
flow should use `state_snapshot`, `event`, and `command_result`.

## Registry Model

Normal Dashboard workflows use `device_id`, labels, and capabilities returned by
`device.list`. They should not require raw Matter node IDs, endpoint IDs,
cluster IDs, or attribute IDs except in developer/debug screens.

`device.list` returns:

```json
{
  "devices": [
    {
      "device_id": "bmp280-1",
      "node_id": "123456789",
      "label": "BMP280 Sensor",
      "reachable": true,
      "vendor_id": 0,
      "product_id": 0,
      "product_name": "BMP280",
      "location": "Root chamber",
      "capabilities": [
        {
          "capability_id": "bmp280-1-temperature",
          "semantic_type": "temperature",
          "endpoint_id": 1,
          "cluster_id": 1026,
          "attribute_id": 0,
          "label": "Temperature"
        },
        {
          "capability_id": "bmp280-1-pressure",
          "semantic_type": "pressure",
          "endpoint_id": 2,
          "cluster_id": 1027,
          "attribute_id": 0,
          "label": "Pressure"
        }
      ]
    }
  ]
}
```

Supported `semantic_type` values are `temperature`, `pressure`, `relay`,
`raw_attribute`, and `raw_command`.

After `matter.pair_ble_thread`, the Orchestrator creates or updates a reachable
placeholder device with no fake semantic capabilities:

```json
{
  "device_id": "node-123456789",
  "node_id": "123456789",
  "label": "Matter node 123456789",
  "reachable": true,
  "capabilities": []
}
```

Until automatic endpoint discovery is implemented, add capabilities manually:

```json
{
  "type": "command",
  "request_id": "req-cap-1",
  "action": "device.capability.add",
  "payload": {
    "device_id": "node-123456789",
    "capability_id": "node-123456789-temperature",
    "semantic_type": "temperature",
    "endpoint_id": 1,
    "cluster_id": 1026,
    "attribute_id": 0,
    "label": "Temperature"
  }
}
```

## Semantic Commands

`device.temperature.read` and `device.pressure.read` look up the matching
capability for the requested `device_id`. They do not assume endpoint `1`.
Matter reads are asynchronous in the current firmware, so a successful
`command_result` confirms that the read request was accepted:

```json
{
  "type": "command_result",
  "request_id": "req-temp-1",
  "action": "device.temperature.read",
  "ok": true,
  "payload": {
    "device_id": "bmp280-1",
    "accepted": true,
    "result_delivery": "matter.attribute_report"
  }
}
```

The value arrives later as an event:

```json
{
  "type": "event",
  "event": "matter.attribute_report",
  "payload": {
    "device_id": "bmp280-1",
    "semantic_type": "temperature",
    "temperature_celsius": 23.41,
    "raw_measured_value": 2341,
    "node_id": "123456789",
    "endpoint_id": 1,
    "cluster_id": 1026,
    "attribute_id": 0,
    "value": "2341"
  }
}
```

Pressure reports include `pressure_kpa` and `raw_measured_value` when a pressure
capability is registered.

`device.relay.set` requires a `relay` capability:

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

Missing devices or capabilities return `ok:false` with a clear error message.

## Command List

Operator commands:

- `chamber.status_get`
- `device.list`
- `device.get`
- `device.remove`
- `device.rename`
- `device.capability.add`
- `device.temperature.read`
- `device.pressure.read`
- `device.relay.set`
- `device.attribute.read`
- `device.attribute.subscribe`

Developer commands:

- `wifi.sta_connect`: `ssid`, `password`
- `thread.enable`
- `thread.disable`
- `thread.status_get`
- `thread.attached_get`
- `thread.role_get`
- `thread.active_dataset_get`
- `thread.unicast_addresses_get`
- `thread.multicast_addresses_get`
- `thread.br_init`
- `thread.br_deinit`
- `thread.dataset.init`
- `matter.controller_init`
- `matter.pair_ble_thread`
- `matter.cluster_command_invoke`
- `matter.attribute_read`
- `matter.attribute_subscribe`

Raw Matter commands use decimal string `node_id` or `destination_id`, numeric
`endpoint_id`, numeric `cluster_id`, numeric `attribute_id`, and numeric
`command_id` fields.

## Hardware Verification

Required local verification:

```bash
get_idf
get_matter
idf.py build
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
idf.py -p <detected-port> flash monitor
```

With a WSS client that accepts the local development certificate for this test,
verify invalid JSON, missing `request_id`, unknown action, `thread.status_get`,
`device.list`, `matter.controller_init`, connect snapshot delivery, and
disconnect/reconnect behavior.
