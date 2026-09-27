#!/usr/bin/env bash
# Start a host, run fakejoin.py against it on loopback, stop the host by its saved pid, and exit with
# the joiner's status. The joiner is the check: it prints a PASS or FAIL line per rule in
# docs/TA-NETWORK.md and its exit status is the verdict.
#
#   tools/ta-net/host-check.sh --capture <pcap> [options] -- <host command> [args...]
#
# For now the host is the spike's fakehost.py, which holds the launch until its go file exists:
#
#   tools/ta-net/host-check.sh --capture ta-small.pcap --release /tmp/run/go -- \
#       tools/ta-net/fakehost.py ta-small.pcap --dir /tmp/run --tcp-port 2400 --udp-port 2450 --enum-port 47625
#
# and once the RWE host exists it is the same command with rwe --ta-host in place of it. The default
# ports are 34700/34750/47625 for the host and 34701/34751 for the joiner, clear of the 2300/2350 and
# 47624 a real TA or dplaysvr holds, so a run needs nothing stopped.
set -u

here=$(cd -- "$(dirname -- "$0")" && pwd)
dir=$(mktemp -d "${TMPDIR:-/tmp}/ta-host-check.XXXXXX")
capture=""; play=30; release=""; pass=(); host_pid=""; watcher=""
host_tcp=34700; host_udp=34750; enum_port=47625; tcp_port=34701; udp_port=34751

while [ $# -gt 0 ]; do
    case "$1" in
        --capture) capture=$2; shift 2 ;;
        --play-seconds) play=$2; shift 2 ;;
        --release) release=$2; shift 2 ;;
        --host-tcp) host_tcp=$2; shift 2 ;;
        --host-udp) host_udp=$2; shift 2 ;;
        --enum-port) enum_port=$2; shift 2 ;;
        --tcp-port) tcp_port=$2; shift 2 ;;
        --udp-port) udp_port=$2; shift 2 ;;
        --dir) dir=$2; shift 2 ;;
        --) shift; break ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) pass+=("$1"); shift ;;
    esac
done
host=("$@")
[ ${#host[@]} -gt 0 ] || { echo "usage: $(basename "$0") --capture <pcap> [options] -- <host command> [args...]" >&2; exit 2; }
[ -n "$capture" ] || { echo "no --capture: fakejoin.py needs a capture of a real TA joining, for its unit ids" >&2; exit 2; }
mkdir -p "$dir" || exit 2
host_log="$dir/host.log"; join_log="$dir/fakejoin.log"
: > "$host_log"; : > "$join_log"

# A host that gates its launch on a file (the spike's fakehost does, on "go") is released as soon as
# the joiner says it is ready, so the run does not need a human in it.
watcher=""
if [ -n "$release" ]; then
    mkdir -p "$(dirname -- "$release")" || exit 2
    ( while ! grep -q 'ready: unit sync' "$join_log" 2>/dev/null; do sleep 0.2; done; touch "$release" ) &
    watcher=$!
fi

cleanup() {
    [ -n "$watcher" ] && kill "$watcher" 2>/dev/null
    [ -n "$host_pid" ] && kill "$host_pid" 2>/dev/null
    [ -n "$host_pid" ] && wait "$host_pid" 2>/dev/null
    return 0
}
trap cleanup EXIT INT TERM

"${host[@]}" >"$host_log" 2>&1 &
host_pid=$!
echo $host_pid > "$dir/host.pid"
echo "host: ${host[*]} (pid $host_pid)"

listening=""
waited=0
while [ "$waited" -lt 100 ]; do
    if ! kill -0 "$host_pid" 2>/dev/null; then
        echo "the host exited before it was listening; its log is $host_log" >&2
        tail -20 "$host_log" >&2
        exit 1
    fi
    if (exec 3<>"/dev/tcp/127.0.0.1/$host_tcp") 2>/dev/null; then
        exec 3>&- 2>/dev/null
        listening=yes
        echo "the host is listening on $host_tcp"
        break
    fi
    sleep 0.1; waited=$((waited + 1))
done
[ -n "$listening" ] || { echo "nothing is listening on $host_tcp after 10s" >&2; exit 1; }

uv run --script "$here/fakejoin.py" "$capture" "${pass[@]}" \
    --host 127.0.0.1 --host-tcp "$host_tcp" --host-udp "$host_udp" --enum-port "$enum_port" \
    --tcp-port "$tcp_port" --udp-port "$udp_port" --play-seconds "$play" 2>&1 | tee "$join_log"
status=${PIPESTATUS[0]}

cleanup
trap - EXIT INT TERM
echo "host log: $host_log   joiner log: $join_log"
if [ "$status" -ne 0 ]; then
    echo "--- last 20 lines of the host's log"
    tail -20 "$host_log"
fi
exit "$status"
