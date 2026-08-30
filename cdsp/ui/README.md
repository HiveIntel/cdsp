# Monitoring UI

This UI displays live CPU and memory charts from the monitoring service.

## Ports

- Monitoring service: `8510`
- Streamlit UI: `8501`

## Start

```bash
pip install -r requirements.txt
UI_HOST=0.0.0.0 UI_PORT=8501 ./start_ui.sh
```

The UI reads the monitoring service IP from `monitoring_config.json` in this
directory.

## Configure The Remote Service IP

Edit `node_ip` in `cdsp-ui/monitoring_config.json`:

```bash
{
  "node_ip": "localhost"
}
```

The UI keeps the monitoring service port fixed at `8510` and automatically builds:

- Monitoring control WebSocket: `ws://<SERVICE_DEVICE_IP>:1805`
- Information Layer WebSocket: `ws://<SERVICE_DEVICE_IP>:8080`
- Reasoner HTTP base URL: `http://<SERVICE_DEVICE_IP>:12110`

For local testing on the same machine:

- `node_ip`: `localhost`
- UI URL: `http://localhost:8501`
- Monitoring control WebSocket: `ws://localhost:1805`
- Information Layer WebSocket: `ws://localhost:8080`
- Reasoner HTTP base URL: `http://localhost:12110`

For your deployment:

- Monitoring service device: `192.168.10.5`
- UI device: `192.168.10.2`
- `node_ip` in `monitoring_config.json`: `192.168.10.5`
- UI URL: `http://192.168.10.2:8501`
- Monitoring control WebSocket: `ws://192.168.10.5:1805`
- Information Layer WebSocket: `ws://192.168.10.5:8080`
- Reasoner HTTP base URL: `http://192.168.10.5:12110`

Open the UI from another machine:

```bash
http://<UI_DEVICE_IP>:8501
```

## Optional Config Path

If you want to store the config in a different location:

```bash
UI_CONFIG_PATH=/path/to/monitoring_config.json ./start_ui.sh
```
