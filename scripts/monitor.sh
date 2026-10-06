#!/usr/bin/env bash

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MONITOR="${PROJECT_ROOT}/monitor"
REPORT="${PROJECT_ROOT}/python/report.py"
LOG_DIR="${PROJECT_ROOT}/logs"

usage() {
    cat <<EOF
Usage:
  ./scripts/monitor.sh start
  ./scripts/monitor.sh stop
  ./scripts/monitor.sh snapshot
  ./scripts/monitor.sh log [interval]
  ./scripts/monitor.sh report
  ./scripts/monitor.sh clean
  ./scripts/monitor.sh help
EOF
}

require_monitor() {
    if [[ ! -x "${MONITOR}" ]]; then
        echo "error: monitor binary not found at ${MONITOR}" >&2
        echo "Build the project first." >&2
        return 1
    fi
}

start_monitor() {
    require_monitor || return 1

    mkdir -p "${LOG_DIR}"

    "${MONITOR}" --log "${LOG_DIR}" &
    echo $! > "${LOG_DIR}/monitor.pid"

    echo "monitor started (PID $(cat "${LOG_DIR}/monitor.pid"))"
}

stop_monitor() {
    local pid_file="${LOG_DIR}/monitor.pid"

    if [[ ! -f "${pid_file}" ]]; then
        echo "monitor is not running"
        return 0
    fi

    local pid
    pid="$(cat "${pid_file}")"

    if kill -0 "${pid}" 2>/dev/null; then
        kill -TERM "${pid}"
        echo "sent SIGTERM to monitor (PID ${pid})"
    else
        echo "monitor process ${pid} is not running"
    fi

    rm -f "${pid_file}"
}

snapshot_monitor() {
    require_monitor || return 1

    "${MONITOR}" --snapshot
}

log_monitor() {
    local interval="${1:-2}"

    if ! [[ "${interval}" =~ ^[0-9]+$ ]]; then
        echo "error: interval must be an integer from 1 to 3600" >&2
        return 1
    fi

    if (( interval < 1 || interval > 3600 )); then
        echo "error: interval must be an integer from 1 to 3600" >&2
        return 1
    fi

    require_monitor || return 1

    mkdir -p "${LOG_DIR}"

    "${MONITOR}" --log "${LOG_DIR}" --interval "${interval}"
}
report_monitor() {
    if [[ ! -f "${LOG_DIR}/system.csv" ]]; then
        echo "error: ${LOG_DIR}/system.csv not found" >&2
        return 1
    fi

    py "${REPORT}" "${LOG_DIR}/system.csv"
}

clean_monitor() {
    rm -f \
        "${LOG_DIR}/system.csv" \
        "${LOG_DIR}/processes.csv" \
        "${LOG_DIR}/monitor.pid"

    echo "logs cleaned"
}

case "${1:-help}" in
    start)
        start_monitor
        ;;
    stop)
        stop_monitor
        ;;
    snapshot)
        snapshot_monitor
        ;;
    log)
        log_monitor "${2:-2}"
        ;;
    report)
        report_monitor
        ;;
    clean)
        clean_monitor
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        echo "error: unknown command: ${1}" >&2
        usage >&2
        exit 2
        ;;
esac