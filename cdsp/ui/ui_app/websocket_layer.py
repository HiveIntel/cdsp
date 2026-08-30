import json
import queue
import threading
from collections import deque
from datetime import datetime
from typing import Any

import streamlit as st
import websocket

from ui_app.speed_angle_map import extract_signal_sample
from ui_app.context import (
    MONITOR_SUBSCRIBE_INTERVAL_SECONDS,
    RDF4J_REASONER_SUBSCRIBE_MESSAGE,
    il_open_flag,
    message_queue,
    monitor_control_queue,
    monitor_subscription_ack_flag,
    rdf4j_reasoner_il_open_flag,
    rdf4j_reasoner_subscription_queue,
    subscription_queue,
    ws_open_flag,
)
from ui_app.logs import (
    append_cqels_log,
    append_monitor_service_log,
    append_rdf4j_subscribe_log,
    append_signal_log,
    append_subscribe_log,
    ensure_cqels_result_log_file,
    ensure_monitor_service_log_file,
    ensure_rdf4j_result_log_file,
    ensure_signal_log_file,
    render_cqels_log_output,
    render_rdf4j_log_output,
)


def enqueue_status(status: str, payload: dict[str, Any] | None = None, error: str = "") -> None:
    message_queue.put({"status": status, "payload": payload, "error": error})


def enqueue_monitor_control_event(kind: str, payload: dict[str, Any] | None = None, error: str = "") -> None:
    monitor_control_queue.put({"kind": kind, "payload": payload, "error": error})


def enqueue_subscription_event(status: str, payload: dict[str, Any] | None = None, error: str = "") -> None:
    subscription_queue.put({"status": status, "payload": payload, "error": error})


def enqueue_rdf4j_reasoner_subscription_event(
    status: str, payload: dict[str, Any] | None = None, error: str = ""
) -> None:
    rdf4j_reasoner_subscription_queue.put({"status": status, "payload": payload, "error": error})


@st.cache_resource
def get_cqels_state() -> tuple[queue.Queue, threading.Event]:
    return queue.Queue(), threading.Event()


cqels_queue, cqels_open_flag = get_cqels_state()


def enqueue_cqels_event(status: str, payload: dict[str, Any] | None = None, error: str = "") -> None:
    cqels_queue.put({"status": status, "payload": payload, "error": error})


def send_cqels_request(request: dict[str, Any], *, log_title: str = "CQELS request sent") -> bool:
    ws_app = st.session_state.get("cqels_ws_app")
    if ws_app is None or ws_app.sock is None or not ws_app.sock.connected:
        st.session_state["pending_cqels_request"] = request
        ensure_cqels_connected()
        return False

    ws_app.send(json.dumps(request))
    st.session_state["pending_cqels_request"] = None
    append_cqels_log(log_title, request)
    return True


def on_open(ws: websocket.WebSocketApp) -> None:
    ws_open_flag.set()
    monitor_subscription_ack_flag.clear()
    enqueue_monitor_control_event("subscription_reset")
    send_monitor_subscription(ws)
    enqueue_status("Live")


def on_message(ws: websocket.WebSocketApp, message: str) -> None:
    try:
        payload = json.loads(message)
    except json.JSONDecodeError as exc:
        append_monitor_service_log("inbound_error", "invalid_json", message)
        enqueue_status("Error", error=f"Invalid JSON payload: {exc}")
        return

    message_type = payload.get("type")
    if message_type == "ack" and payload.get("command") == "subscribe":
        append_monitor_service_log("inbound", "subscription_ack", payload)
        monitor_subscription_ack_flag.set()
        enqueue_monitor_control_event("subscription_ack", payload=payload)
        return

    if message_type == "event":
        event_name = payload.get("event")
        append_monitor_service_log("inbound", event_name or "event", payload)
        event_payload = payload.get("payload", {})
        if event_name == "resource_update" and isinstance(event_payload, dict):
            enqueue_status("Live", payload=event_payload)
            return
        enqueue_monitor_control_event(event_name or "event", payload=payload)
        return

    if message_type == "error":
        error_info = payload.get("error", {})
        message_text = error_info.get("message", "Unknown monitoring service error.")
        append_monitor_service_log("inbound_error", "error", payload)
        enqueue_monitor_control_event("error", payload=payload, error=message_text)
        enqueue_status("Error", error=message_text)
        return

    if is_metrics_payload(payload):
        enqueue_status("Live", payload=payload)
        return

    append_monitor_service_log("inbound", message_type or "message", payload)
    enqueue_monitor_control_event("message", payload=payload)


def on_error(ws: websocket.WebSocketApp, error: Any) -> None:
    monitor_subscription_ack_flag.clear()
    append_monitor_service_log("inbound_error", "websocket_error", str(error))
    enqueue_monitor_control_event("error", error=str(error))
    enqueue_status("Error", error=str(error))


def on_close(ws: websocket.WebSocketApp, status_code: int, reason: str) -> None:
    ws_open_flag.clear()
    monitor_subscription_ack_flag.clear()
    append_monitor_service_log(
        "inbound",
        "websocket_closed",
        {"status_code": status_code, "reason": reason or "by user"},
    )
    enqueue_monitor_control_event("subscription_reset")
    enqueue_status("Disconnected", error=reason or "by user")


def on_il_open(ws: websocket.WebSocketApp) -> None:
    il_open_flag.set()
    enqueue_subscription_event("Live")

def on_il_message(ws: websocket.WebSocketApp, message: str) -> None:
    try:
        payload = json.loads(message)
        enqueue_subscription_event("Live", payload=payload)
    except json.JSONDecodeError as exc:
        enqueue_subscription_event("Error", error=f"Invalid JSON payload: {exc}")


def on_il_error(ws: websocket.WebSocketApp, error: Any) -> None:
    enqueue_subscription_event("Error", error=str(error))


def on_il_close(ws: websocket.WebSocketApp, status_code: int, reason: str) -> None:
    il_open_flag.clear()
    enqueue_subscription_event("Disconnected", error=reason or "by user")

def on_rdf4j_reasoner_il_open(ws: websocket.WebSocketApp) -> None:
    rdf4j_reasoner_il_open_flag.set()
    enqueue_rdf4j_reasoner_subscription_event("Opened")


def on_rdf4j_reasoner_il_message(ws: websocket.WebSocketApp, message: str) -> None:
    try:
        payload = json.loads(message)
        enqueue_rdf4j_reasoner_subscription_event("Live", payload=payload)
    except json.JSONDecodeError as exc:
        enqueue_rdf4j_reasoner_subscription_event("Error", error=f"Invalid JSON payload: {exc}")


def on_rdf4j_reasoner_il_error(ws: websocket.WebSocketApp, error: Any) -> None:
    enqueue_rdf4j_reasoner_subscription_event("Error", error=str(error))


def on_rdf4j_reasoner_il_close(ws: websocket.WebSocketApp, status_code: int, reason: str) -> None:
    rdf4j_reasoner_il_open_flag.clear()
    enqueue_rdf4j_reasoner_subscription_event("Disconnected", error=reason or "by user")


def on_cqels_open(ws: websocket.WebSocketApp) -> None:
    cqels_open_flag.set()
    enqueue_cqels_event("Opened")


def on_cqels_message(ws: websocket.WebSocketApp, message: str) -> None:
    try:
        payload = json.loads(message)
        enqueue_cqels_event("Live", payload=payload)
    except json.JSONDecodeError as exc:
        enqueue_cqels_event("Error", error=f"Invalid JSON payload: {exc}")


def on_cqels_error(ws: websocket.WebSocketApp, error: Any) -> None:
    enqueue_cqels_event("Error", error=str(error))


def on_cqels_close(ws: websocket.WebSocketApp, status_code: int, reason: str) -> None:
    cqels_open_flag.clear()
    enqueue_cqels_event("Disconnected", error=reason or "by user")


def start_socket_client(
    url: str,
    *,
    on_open_callback: Any,
    on_message_callback: Any,
    on_error_callback: Any,
    on_close_callback: Any,
    session_key: str,
) -> websocket.WebSocketApp:
    ws_app = websocket.WebSocketApp(
        url,
        on_open=on_open_callback,
        on_message=on_message_callback,
        on_error=on_error_callback,
        on_close=on_close_callback,
    )

    def run_forever() -> None:
        ws_app.run_forever(reconnect=3)

    thread = threading.Thread(target=run_forever, daemon=True)
    thread.start()
    st.session_state[session_key] = True
    return ws_app


def start_websocket_client(url: str) -> websocket.WebSocketApp:
    ws_app = start_socket_client(
        url,
        on_open_callback=on_open,
        on_message_callback=on_message,
        on_error_callback=on_error,
        on_close_callback=on_close,
        session_key="ws_thread_started",
    )
    start_monitor_subscription_retry(ws_app)
    return ws_app


def build_monitor_subscribe_message() -> str:
    return json.dumps(
        {
            "type": "subscribe",
            "channel": "resources",
            "requestId": "monitor-resource-subscribe",
            "payload": {"intervalSeconds": 1},
        }
    )


def send_monitor_subscription(ws: websocket.WebSocketApp) -> None:
    message = build_monitor_subscribe_message()
    try:
        ws.send(message)
        append_monitor_service_log("outbound", "subscribe_resources", json.loads(message))
    except Exception as exc:
        append_monitor_service_log("outbound_error", "subscribe_resources", str(exc))
        enqueue_monitor_control_event("error", error=f"Failed to send monitor subscription: {exc}")


def start_monitor_subscription_retry(ws_app: websocket.WebSocketApp) -> None:
    def run() -> None:
        while True:
            try:
                if ws_app.sock is not None and ws_app.sock.connected and not monitor_subscription_ack_flag.is_set():
                    send_monitor_subscription(ws_app)
            except Exception:
                pass
            threading.Event().wait(MONITOR_SUBSCRIBE_INTERVAL_SECONDS)

    threading.Thread(target=run, daemon=True, name="monitor-subscribe-retry").start()


def start_information_layer_client(url: str) -> websocket.WebSocketApp:
    return start_socket_client(
        url,
        on_open_callback=on_il_open,
        on_message_callback=on_il_message,
        on_error_callback=on_il_error,
        on_close_callback=on_il_close,
        session_key="il_ws_thread_started",
    )


def start_rdf4j_reasoner_information_layer_client(url: str) -> websocket.WebSocketApp:
    return start_socket_client(
        url,
        on_open_callback=on_rdf4j_reasoner_il_open,
        on_message_callback=on_rdf4j_reasoner_il_message,
        on_error_callback=on_rdf4j_reasoner_il_error,
        on_close_callback=on_rdf4j_reasoner_il_close,
        session_key="rdf4j_reasoner_il_ws_thread_started",
    )


def start_cqels_client(url: str) -> websocket.WebSocketApp:
    return start_socket_client(
        url,
        on_open_callback=on_cqels_open,
        on_message_callback=on_cqels_message,
        on_error_callback=on_cqels_error,
        on_close_callback=on_cqels_close,
        session_key="cqels_ws_thread_started",
    )


def build_unsubscribe_message(raw_message: str) -> str:
    parsed = json.loads(raw_message)
    if "method" in parsed:
        parsed["method"] = "unsubscribe"
    elif "type" in parsed:
        parsed["type"] = "unsubscribe"
    else:
        parsed["method"] = "unsubscribe"
    return json.dumps(parsed, indent=2)


def ensure_backend_service_connected() -> None:
    if st.session_state.get("monitoring_service_wss") is None:
        st.session_state["connection_status"] = "Connecting ... "
        st.session_state["monitoring_service_wss"] = start_websocket_client(
            st.session_state["monitoring_service_ws"]
        )


def ensure_information_layer_connected() -> None:
    if st.session_state.get("il_ws_app") is None:
        st.session_state["il_ws_app"] = start_information_layer_client(st.session_state["il_url"])

def ensure_rdf4j_reasoner_information_layer_connected() -> None:
    if st.session_state.get("rdf4j_reasoner_il_wss") is None:
        st.session_state["rdf4j_reasoner_il_wss"] = start_rdf4j_reasoner_information_layer_client(
            st.session_state["il_url"]
        )


def ensure_cqels_connected() -> None:
    ws_app = st.session_state.get("cqels_ws_app")
    if ws_app is None or ws_app.sock is None or not ws_app.sock.connected:
        st.session_state["cqels_ws_app"] = None
        st.session_state["cqels_ws_app"] = start_cqels_client(st.session_state["cqels_ws_url"])


def ensure_rdf4j_reasoner_subscription_started() -> None:
    ws_app = st.session_state.get("rdf4j_reasoner_il_wss")
    if ws_app is None or ws_app.sock is None or not ws_app.sock.connected:
        st.session_state["rdf4j_reasoner_subscription_active"] = False
        ensure_rdf4j_reasoner_information_layer_connected()
        return

    if not st.session_state.get("rdf4j_reasoner_subscription_active", False):
       send_rdf4j_reasoner_subscription()


def stop_rdf4j_reasoner_subscription_session() -> None:
    ws_app = st.session_state.get("rdf4j_reasoner_il_wss")
    if ws_app is not None:
        try:
            ws_app.close()
        except Exception:
            pass
    rdf4j_reasoner_il_open_flag.clear()
    st.session_state["rdf4j_reasoner_il_wss"] = None
    st.session_state["rdf4j_reasoner_il_ws_thread_started"] = False
    st.session_state["rdf4j_reasoner_subscription_active"] = False


def stop_cqels_session() -> None:
    ws_app = st.session_state.get("cqels_ws_app")
    if ws_app is not None:
        try:
            ws_app.close()
        except Exception:
            pass
    cqels_open_flag.clear()
    st.session_state["cqels_ws_app"] = None
    st.session_state["cqels_ws_thread_started"] = False
    st.session_state["cqels_query_active"] = False
        

def send_subscription_message(raw_message: str, action_label: str) -> None:
    is_subscribe = action_label.lower() == "subscribe"
    
    if is_subscribe:
        ensure_information_layer_connected()

    ws_app = st.session_state.get("il_ws_app")
    if ws_app is None or ws_app.sock is None or not ws_app.sock.connected:
        st.session_state["info_layer_message"] = (
            f"Information layer WebSocket is not connected at {st.session_state['il_url']}."
        )
        append_subscribe_log("Subscription blocked", "Information layer WebSocket is not connected.")
        return

    try:
        parsed = json.loads(raw_message)
        ws_app.send(json.dumps(parsed))
        st.session_state["subscription_active"] = is_subscribe
        if is_subscribe:
            st.session_state["info_layer_message"] = (
                f"{action_label} message sent. Waiting for live information-layer output from "
                f"{st.session_state['il_url']}."
            )
        else:
            st.session_state["info_layer_message"] = f"Stop subscription sent to {st.session_state['il_url']}."
        print(f"[subscribe-ui] Sent {action_label.lower()} request:", json.dumps(parsed))
    except json.JSONDecodeError as exc:
        st.session_state["info_layer_message"] = f"Invalid JSON: {exc}"
        append_subscribe_log("Invalid JSON", str(exc))
    except Exception as exc:
        st.session_state["info_layer_message"] = f"Send failed: {exc}"
        append_subscribe_log("Send failed", str(exc))


def stop_subscription() -> None:
    raw_message = st.session_state["subscribe_message"]
    try:
        unsubscribe_message = build_unsubscribe_message(raw_message)
    except json.JSONDecodeError as exc:
        st.session_state["info_layer_message"] = f"Invalid JSON: {exc}"
        append_subscribe_log("Invalid JSON", str(exc))
        return

    send_subscription_message(unsubscribe_message, "Unsubscribe")

    il_ws_app = st.session_state.get("il_ws_app")
    if il_ws_app is not None:
        try:
            il_ws_app.close()
        except Exception as exc:
            append_subscribe_log("Disconnect failed", str(exc))

    il_open_flag.clear()
    st.session_state["il_ws_app"] = None
    st.session_state["il_ws_thread_started"] = False
    #st.session_state["information_layer_active"] = False
    st.session_state["subscription_active"] = False
    st.session_state["info_layer_message"] = (
        f"Subscription stopped and disconnected from {st.session_state['il_url']}."
    )
    append_subscribe_log("IL disconnected", st.session_state["il_url"])


def reset_live_buffers() -> None:
    clear_pending_runtime_events()
    stop_rdf4j_reasoner_subscription_session()
    stop_cqels_session()
    st.session_state["subscribe_logs"] = []
    st.session_state["rdf4j_subscribe_logs"] = []
    st.session_state["cqels_logs"] = []
    st.session_state["rdf4j_results"] = render_rdf4j_log_output()
    st.session_state["cqels_results"] = render_cqels_log_output()
    st.session_state["signal_history"] = deque(maxlen=st.session_state["signal_history"].maxlen)
    st.session_state["history"] = deque(maxlen=st.session_state["history"].maxlen)
    st.session_state["latest_payload"] = None
    st.session_state["last_metrics_sample_at"] = None
    st.session_state["last_error"] = ""
    st.session_state["info_layer_message"] = "Cleared live buffers and reset displayed values."
    ensure_signal_log_file(reset=True)
    ensure_rdf4j_result_log_file(reset=True)
    ensure_cqels_result_log_file(reset=True)
    ensure_monitor_service_log_file(reset=True)


def clear_pending_runtime_events() -> None:
    clear_queue(message_queue)
    clear_queue(subscription_queue)
    clear_queue(cqels_queue)
    clear_queue(rdf4j_reasoner_subscription_queue)
    clear_queue(monitor_control_queue)


def clear_queue(target_queue: queue.Queue) -> None:
    while True:
        try:
            target_queue.get_nowait()
        except queue.Empty:
            break


def store_subscription_event(event: dict[str, Any]) -> None:
    payload = event.get("payload")
    error = event.get("error")
    status = event.get("status")

    if status == "Live":
        st.session_state["information_layer_active"] = True

    if error:
        st.session_state["information_layer_active"] = False
        if status == "Disconnected":
            print(f"[subscribe-ui] Information layer WebSocket disconnected: {error}")
            append_subscribe_log("IL disconnected", error)
            st.session_state["info_layer_message"] = (
                f"Information layer disconnected from {st.session_state['il_url']}: {error}"
            )
        else:
            print(f"[subscribe-ui] Subscription error from remote information layer: {error}")
            append_subscribe_log("Subscription error", error)
            st.session_state["info_layer_message"] = (
                f"Subscription failed from {st.session_state['il_url']}: {error}"
            )
        return

    if not payload:
        return

    if not st.session_state.get("subscription_active", False):
        print("[subscribe-ui] Ignoring IL message because subscription is stopped.")
        return

    print("[subscribe-ui] Live IL message received:", json.dumps(payload))
    append_subscribe_log("IL message", payload)

    signal_sample = extract_signal_sample(payload)
    if signal_sample is not None:
        st.session_state["signal_history"].append(signal_sample)
        append_signal_log(signal_sample)

    st.session_state["info_layer_message"] = (
        f"Receiving live information-layer output from {st.session_state['il_url']}."
    )


def store_rdf4j_reasoner_subscription_event(event: dict[str, Any]) -> None:
    payload = event.get("payload")
    error = event.get("error")
    status = event.get("status")

    if status == "Opened":
        send_rdf4j_reasoner_subscription()
        return

    if error:
        append_rdf4j_subscribe_log("RDF4J reasoner subscription", error)
        st.session_state["rdf4j_submit_feedback"] = error
        st.session_state["rdf4j_reasoner_subscription_active"] = False
        return

    if not payload:
        return

    if not st.session_state.get("rdf4j_reasoner_subscription_active", False):
        return

    append_rdf4j_subscribe_log("RDF4J reasoner message", payload)
    st.session_state["rdf4j_submit_feedback"] = "Receiving RDF4J reasoner output from the Information Layer."


def store_cqels_event(event: dict[str, Any]) -> None:
    payload = event.get("payload")
    error = event.get("error")
    status = event.get("status")

    if status == "Opened":
        append_cqels_log("CQELS websocket connected", st.session_state["cqels_ws_url"])
        pending_request = st.session_state.get("pending_cqels_request")
        if isinstance(pending_request, dict):
            try:
                send_cqels_request(pending_request, log_title="CQELS query submitted")
                st.session_state["cqels_submit_feedback"] = "CQELS query submitted. Waiting for results..."
            except Exception as exc:
                message = f"Failed to submit pending CQELS query: {exc}"
                st.session_state["cqels_submit_feedback"] = message
                append_cqels_log("CQELS submit failed", message)
        return

    if error:
        append_cqels_log("CQELS websocket error", error)
        st.session_state["cqels_submit_feedback"] = error
        st.session_state["cqels_query_active"] = False
        return

    if not payload:
        return

    message_type = payload.get("type", "")
    if message_type == "connected":
        append_cqels_log("CQELS connected", payload)
        return

    if message_type == "ack":
        command = payload.get("command", "")
        if command == "register_query":
            data = payload.get("data", {})
            st.session_state["cqels_subscription_id"] = data.get(
                "subscriptionId",
                st.session_state.get("cqels_subscription_id", ""),
            )
            st.session_state["cqels_query_active"] = True
            st.session_state["cqels_submit_feedback"] = "CQELS query submitted successfully."
            append_cqels_log("CQELS query registered", payload)
        elif command == "unregister_query":
            st.session_state["cqels_query_active"] = False
            st.session_state["cqels_submit_feedback"] = "CQELS query unregistered."
            append_cqels_log("CQELS query unregistered", payload)
        else:
            append_cqels_log("CQELS ack", payload)
        return

    if message_type == "query_result":
        st.session_state["cqels_submit_feedback"] = "Receiving CQELS query results."
        append_cqels_log("CQELS result", payload.get("data", payload))
        return

    if message_type == "error":
        error_payload = payload.get("error", {})
        message = error_payload.get("message", "CQELS service error.")
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS error", payload)
        return

    append_cqels_log("CQELS message", payload)

def is_metrics_payload(payload: dict[str, Any]) -> bool:
    return all(key in payload for key in ("timestamp", "cpu", "memory"))

def parse_payload_timestamp(payload: dict[str, Any]) -> datetime:
    timestamp_raw = payload.get("timestamp")
    try:
        return datetime.fromisoformat(timestamp_raw)
    except (TypeError, ValueError):
        return datetime.utcnow()

def drain_messages() -> None:
    while not message_queue.empty():
        event = message_queue.get()
        st.session_state["connection_status"] = event["status"]
        
        if event["error"]:
            st.session_state["last_error"] = event["error"]
            st.session_state["connection_status_error"] = event["error"]
            
        payload = event.get("payload")
        
        if payload and is_metrics_payload(payload):
            payload_timestamp = parse_payload_timestamp(payload)
            last_sample_at = st.session_state.get("last_metrics_sample_at")
            should_update_metrics = (
                last_sample_at is None
                or (payload_timestamp - last_sample_at).total_seconds() >= 1.0
            )
            if should_update_metrics:
                payload["reasoner"] = payload.get("reasoner") or st.session_state["active_reasoner"]
                st.session_state["latest_payload"] = payload
                st.session_state["history"].append(payload)
                st.session_state["last_metrics_sample_at"] = payload_timestamp
                st.session_state["last_error"] = ""


def drain_monitor_control_messages() -> None:
    while not monitor_control_queue.empty():
        event = monitor_control_queue.get()
        kind = event.get("kind", "")
        payload = event.get("payload") or {}
        error = event.get("error", "")

        if kind == "subscription_ack":
            st.session_state["monitor_subscription_ack"] = True
            st.session_state["connection_status"] = "Live"
            continue

        if kind == "subscription_reset":
            st.session_state["monitor_subscription_ack"] = False
            #st.session_state["information_layer_active"] = False
            stop_rdf4j_reasoner_subscription_session()
            continue

        if kind == "reasoner_state":
            event_payload = payload.get("payload", {}) if isinstance(payload, dict) else {}
            st.session_state["active_reasoner"] = event_payload.get("activeReasoner", "")
            continue

        if kind == "command_log":
            event_payload = payload.get("payload", {}) if isinstance(payload, dict) else {}
            reasoner = event_payload.get("reasoner", "")
            line = event_payload.get("line", "")
            script_name = event_payload.get("script", "")
            rendered = f"{script_name}: {line}" if script_name else line
            if reasoner == "CQELS":
                append_cqels_log("Backend", rendered)
                st.session_state["cqels_submit_feedback"] = rendered
            elif reasoner == "RDF4J":
                append_rdf4j_subscribe_log("Backend", rendered)
                st.session_state["rdf4j_submit_feedback"] = rendered
            elif reasoner == "INFORMATION_LAYER":
                append_subscribe_log("Backend", rendered)
                st.session_state["info_layer_message"] = rendered
            continue

        if kind == "command_started":
            event_payload = payload.get("payload", {}) if isinstance(payload, dict) else {}
            reasoner = event_payload.get("reasoner", "")
            mode = event_payload.get("mode", "")
            message = f"Starting {reasoner} in {mode} mode."
            if reasoner == "CQELS":
                st.session_state["cqels_submit_feedback"] = message
                stop_rdf4j_reasoner_subscription_session()
            elif reasoner == "RDF4J":
                st.session_state["rdf4j_submit_feedback"] = message
                ensure_rdf4j_reasoner_subscription_started()
            continue

        if kind == "error":
            error_message = error or "Monitoring control error."
            st.session_state["last_error"] = error_message
            st.session_state["info_layer_message"] = error_message


def drain_subscription_messages() -> None:
    while not subscription_queue.empty():
        store_subscription_event(subscription_queue.get())


def drain_rdf4j_reasoner_subscription_messages() -> None:
    while not rdf4j_reasoner_subscription_queue.empty():
        store_rdf4j_reasoner_subscription_event(rdf4j_reasoner_subscription_queue.get())


def drain_cqels_messages() -> None:
    while not cqels_queue.empty():
        store_cqels_event(cqels_queue.get())


def send_rdf4j_reasoner_subscription() -> None:
    ws_app = st.session_state.get("rdf4j_reasoner_il_wss")
    if ws_app is None or ws_app.sock is None or not ws_app.sock.connected:
        st.session_state["rdf4j_reasoner_subscription_active"] = False
        return

    try:
        parsed = json.loads(RDF4J_REASONER_SUBSCRIBE_MESSAGE)
        ws_app.send(json.dumps(parsed))
        st.session_state["rdf4j_reasoner_subscription_active"] = True
        append_rdf4j_subscribe_log("RDF4J reasoner subscribe sent", parsed)
    except Exception as exc:
        st.session_state["rdf4j_reasoner_subscription_active"] = False
        append_rdf4j_subscribe_log("RDF4J reasoner subscribe failed", str(exc))
