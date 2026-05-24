# WebSocket Command Protocol

Dashboard clients send commands to the orchestrator with this JSON envelope:

```json
{
  "type": "command",
  "action": "...",
  "payload": {}
}
```

Every command must include a JSON object `payload`. Commands with no fields use `{}`.

## Wi-Fi Commands

### `wifi.sta_connect`

- `ssid`: string
- `password`: string

## Thread Commands

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

## Matter Commands

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
