# Monitoring UI Specification

## Goal

Provide a UI that runs on one device and visualizes live CPU and memory metrics coming from the monitoring service running on another device.

The UI must consume the monitoring stream through a dedicated WebSocket endpoint and show:

- live CPU percentage
- live memory percentage
- recent CPU history chart
- recent memory history chart
- connection status
- target device metadata

## Deployment Model

There are two devices in the system:

1. Service Device
2. UI Device

The monitoring service runs on the Service Device.
The Streamlit UI runs on the UI Device.
The browser connects to the UI Device only.

## Network Contract

### Service Device

- Monitoring control WebSocket URL: `ws://<SERVICE_DEVICE_IP>:1805`
- Information Layer WebSocket URL: `ws://<SERVICE_DEVICE_IP>:8080`
- Reasoner HTTP base URL: `http://<SERVICE_DEVICE_IP>:12110`

### UI Device

- UI base URL: `http://<UI_DEVICE_IP>:8501`

## Fixed Ports

To avoid confusion, these ports are reserved:

- `1805`: monitoring control WebSocket server
- `8080`: Information Layer WebSocket server
- `12110`: reasoner HTTP server
- `8501`: Streamlit monitoring UI

If another service is added later, it must be assigned a different port and documented here before implementation.

## Data Flow

1. Monitoring service samples CPU and memory on the Service Device.
2. Monitoring service publishes metric updates on `ws://<SERVICE_DEVICE_IP>:1805`.
3. Streamlit UI on the UI Device opens an outbound WebSocket connection to that URL.
4. Streamlit UI stores recent samples in memory and renders charts for CPU and memory.
5. Browser users open the UI through `http://<UI_DEVICE_IP>:8501`.

## WebSocket Payload Contract

Each metrics message is JSON with this shape:

```json
{
  "timestamp": "2026-04-16T08:30:00.000000+00:00",
  "target": {
    "kind": "system",
    "name": "device-hostname",
    "source": "psutil",
    "fallback": false,
    "detail": null
  },
  "interval_seconds": 1.0,
  "cpu": {
    "percent": 18.4,
    "core_count": 8,
    "load_average": [0.71, 0.63, 0.59]
  },
  "memory": {
    "used_bytes": 6846218240,
    "total_bytes": 16777216000,
    "available_bytes": 9930997760,
    "percent": 40.8
  }
}
```

## UI Requirements

- UI must read the remote service IP from configuration and build the monitoring, information-layer, and reasoner endpoints automatically.
- Default monitoring URL should resolve to `ws://localhost:1805` for local testing.
- UI must show the currently configured monitoring endpoint.
- UI must show connection state: connecting, live, error, disconnected.
- UI must keep a rolling history window of recent points for charting.
- UI must support remote deployment by reading config from environment variables.

## Configuration

The UI must support these environment variables:

- `UI_HOST`: host binding for Streamlit, default `0.0.0.0`
- `UI_PORT`: Streamlit port, default `8501`
- `SERVICE_DEVICE_IP`: remote service IP, default `localhost`
- `MONITOR_WS_PORT`: monitoring websocket port, default `1805`
- `INFORMATION_LAYER_PORT`: information-layer websocket port, default `8080`
- `REASONER_PORT`: reasoner HTTP port, default `12110`
- `METRICS_HISTORY_LIMIT`: number of points to retain in the UI, default `120`
- `UI_REFRESH_MILLISECONDS`: refresh interval for the Streamlit page, default `2000`

## Why Streamlit Is Acceptable

Streamlit is fine for this phase because:

- it matches the existing team workflow in `covesa_ui_old`
- it is quick to ship and easy to maintain
- it works well for operational dashboards and internal tools
- it can consume WebSocket data with a background client thread

## Better Long-Term Option

If this UI becomes a production-facing or customer-facing application, a browser-native frontend is a better choice than Streamlit.

Recommended long-term direction:

- frontend: React with Vite
- charts: Recharts or ECharts
- transport: direct browser WebSocket to the monitoring service

Reasons:

- lower latency and smoother chart updates
- cleaner separation between UI and backend
- easier multi-page expansion
- easier packaging for kiosk-style device deployments

## Decision For This Iteration

Implement the UI in Streamlit now.

Reason:

- fastest path with the least project risk
- consistent with the older UI approach
- enough for monitoring charts across two devices
