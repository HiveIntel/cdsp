import json
import subprocess
from datetime import datetime
from pathlib import Path

import streamlit as st

from ui_app.context import DEFAULT_MONITOR_MODE
from ui_app.logs import (
    append_information_layer_ui_log,
    append_monitor_service_log,
)
from ui_app.websocket_layer import reset_live_buffers, send_subscription_message, stop_subscription

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
PLAY_RL_STREAM_SCRIPT = REPO_ROOT / "play_rl_stream.sh"
STOP_RL_STREAM_SCRIPT = REPO_ROOT / "stop_rl_stream.sh"


def send_information_layer_start_command() -> None:
    monitoring_service_wss = st.session_state.get("monitoring_service_wss")
    mode = st.session_state.get("monitor_mode", DEFAULT_MONITOR_MODE)
    append_information_layer_ui_log(
        "start_button_clicked",
        {"mode": mode, "monitoring_service_ws": st.session_state.get("monitoring_service_ws", "")},
    )
    if st.session_state.get("information_layer_active", False):
        message = f"Information layer websocket is already connected at {st.session_state['il_url']}."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("start_button_skipped", message)
        return

    if (
        monitoring_service_wss is None
        or monitoring_service_wss.sock is None
        or not monitoring_service_wss.sock.connected
    ):
        message = (
            "Monitoring control websocket is not connected at "
            f"{st.session_state['monitoring_service_ws']}."
        )
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("start_button_blocked", message)
        return

    request_id = f"information-layer-start-{int(datetime.utcnow().timestamp() * 1000)}"
    message = {
        "type": "command",
        "command": "start_information_layer",
        "requestId": request_id,
        "payload": {
            "active": True,
            "mode": mode,
        },
    }

    try:
        monitoring_service_wss.send(json.dumps(message))
        #st.session_state["information_layer_active"] = False
        success_message = "Start command sent to monitoring service."
        st.session_state["info_layer_message"] = success_message
        append_information_layer_ui_log("start_command_sent", message)
        append_monitor_service_log("outbound", "start_information_layer", message)
    except Exception as exc:
        error_message = f"Failed to send information layer start command: {exc}"
        st.session_state["info_layer_message"] = error_message
        append_information_layer_ui_log("start_command_failed", error_message)
        append_monitor_service_log("outbound_error", "start_information_layer", error_message)

def play_stream_button() -> None:
    target_ip = st.session_state.get("node_ip", "").strip()
    if not target_ip:
        message = "Remote node IP is not configured."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("play_stream_blocked", message)
        return

    if not PLAY_RL_STREAM_SCRIPT.exists():
        message = f"Local replay script not found: {PLAY_RL_STREAM_SCRIPT}"
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("play_stream_failed", message)
        return

    command = [str(PLAY_RL_STREAM_SCRIPT), target_ip]
    append_information_layer_ui_log("play_stream_requested", {"target_ip": target_ip, "command": command})

    try:
        completed = subprocess.run(
            command,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except Exception as exc:
        message = f"Failed to start RL stream: {exc}"
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("play_stream_failed", message)
        return

    output = (completed.stdout or completed.stderr or "").strip()
    if completed.returncode == 0:
        message = output or f"Started RL stream for remote node {target_ip}."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("play_stream_started", message)
    else:
        message = output or f"play_rl_stream.sh failed with exit code {completed.returncode}."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("play_stream_failed", message)


def stop_stream_button() -> None:
    if not STOP_RL_STREAM_SCRIPT.exists():
        message = f"Local stop script not found: {STOP_RL_STREAM_SCRIPT}"
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("stop_stream_failed", message)
        return

    append_information_layer_ui_log("stop_stream_requested", {"command": [str(STOP_RL_STREAM_SCRIPT)]})

    try:
        completed = subprocess.run(
            [str(STOP_RL_STREAM_SCRIPT)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except Exception as exc:
        message = f"Failed to stop RL stream: {exc}"
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("stop_stream_failed", message)
        return

    output = (completed.stdout or completed.stderr or "").strip()
    if completed.returncode == 0:
        message = output or "Stopped RL stream."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("stop_stream_completed", message)
    else:
        message = output or f"stop_rl_stream.sh failed with exit code {completed.returncode}."
        st.session_state["info_layer_message"] = message
        append_information_layer_ui_log("stop_stream_failed", message)



def render_information_layer_panel() -> None:
    st.markdown(
        """
        <style>
        div[data-testid="stButton"] > button[kind="primary"] {
            background-color: #22c55e;
            color: white;
            border: 1px solid #16a34a;
        }
        </style>
        """,
        unsafe_allow_html=True,
    )

    with st.container(border=True):
        control_col, log_col = st.columns([2, 3])

        with control_col:
            with st.container(border=True):
                title_col, start_col = st.columns([4, 1])
                with title_col:
                    st.subheader("Information Layer")
                with start_col:
                    st.button(
                        "Start",
                        use_container_width=True,
                        key="information_layer_start_button",
                        type="primary" if st.session_state.get("information_layer_active") else "secondary",
                        disabled = True if st.session_state.get("information_layer_active") else False, 
                        on_click=send_information_layer_start_command,
                    )

                st.session_state["subscribe_message"] = st.text_area(
                    "Subscribe JSON",
                    value=st.session_state["subscribe_message"],
                    height=120,
                    key="subscribe_message_editor",
                )

                start_sub_col, stop_sub_col = st.columns(2)
                with start_sub_col:
                    if st.button("Start Subscription", use_container_width=True):
                        send_subscription_message(st.session_state["subscribe_message"], "Subscribe")
                with stop_sub_col:
                    if st.button("Stop Subscription", use_container_width=True):
                        stop_subscription()

                start_stream, stop_stream = st.columns(2)
                with start_stream:
                    if st.button("Play Stream", use_container_width=True):
                        stop_stream_button()
                        reset_live_buffers()
                        play_stream_button()
                        
                        
                with stop_stream:
                    if st.button("Clear Buffers", use_container_width=True):
                        #stop_stream_button()
                        reset_live_buffers()
                        
                #if st.button("Clear Buffers", use_container_width=True):
                #    reset_live_buffers()
                    
                #if st.session_state["info_layer_message"]:
                #st.info(st.session_state["information_layer_active"])

        with log_col:
            #st.subheader("Information Layer Logs")
            log_output = "\n\n---\n\n".join(st.session_state["subscribe_logs"][-5:])
            if not log_output:
                log_output = "Waiting for information-layer output..."
            st.code(log_output, language=None, height=420)
