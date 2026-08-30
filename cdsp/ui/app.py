import streamlit as st

from ui_app.context import init_session_state
from ui_app.header_pannel import render_header
from ui_app.information_layer import render_information_layer_panel
from ui_app.knowledge_layer import render_knowledge_layer
from ui_app.resource_monitoring_chart import render_charts, render_metrics
from ui_app.speed_angle_map import render_live_signal_charts
from ui_app.websocket_layer import (
    drain_cqels_messages,
    drain_messages,
    drain_monitor_control_messages,
    drain_rdf4j_reasoner_subscription_messages,
    drain_subscription_messages,
    ensure_information_layer_connected,
    ensure_backend_service_connected,
)


st.set_page_config(
    page_title="CDSP Monitoring UI",
    page_icon="chart_with_upwards_trend",
    layout="wide",
)


@st.fragment(run_every = 1.0)
def render_live_subscription_fragment() -> None:
    drain_subscription_messages()
    render_information_layer_panel()
    render_live_signal_charts(st.session_state["signal_history"])


@st.fragment(run_every=1.0)
def render_live_header_fragment() -> None:
    ensure_backend_service_connected()
    drain_messages()
    drain_monitor_control_messages()
    drain_rdf4j_reasoner_subscription_messages()
    render_header(st.session_state["latest_payload"])


@st.fragment(run_every=1.0)
def render_live_monitor_fragment() -> None:
    render_metrics(st.session_state["latest_payload"])
    render_charts(st.session_state["history"])


@st.fragment(run_every=1.0)
def render_live_knowledge_fragment() -> None:
    drain_cqels_messages()
    render_knowledge_layer()


init_session_state()
render_live_header_fragment()
ensure_information_layer_connected()
render_live_subscription_fragment()
render_live_knowledge_fragment()
render_live_monitor_fragment()
