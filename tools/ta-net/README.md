# Total Annihilation on the network

Tools from the spike that asked whether RWE could take part in a network game with a real
`TotalA.exe` (issue #386), and from the scripted joiner that checks a host without one (issue #430).
What they found is written up in [`docs/TA-NETWORK.md`](../../docs/TA-NETWORK.md); these are kept so
it can be repeated and extended rather than taken on trust.

Nothing here is built or shipped. They run with `uv run --script`, which fetches `scapy` for the two
that read captures. None of them needs root, but recording a capture does.

| Tool | What it does |
|---|---|
| `dpenum.py [my-lan-ip]` | Sends a DirectPlay EnumSessions request every way a joiner might, and prints any ENUMSESSIONSREPLY. The first check on a new machine: if a TA that is hosting does not answer, nothing owns port 47624. |
| `ta-capture.py <pcap>` | Reads a capture of TA playing over DirectPlay: flows, the DirectPlay sequence, and every TA subpacket. `--timeline`, `--settings` (options byte and teams), `--units` (decoded `0x2c`). `--check` exits non-zero unless every TA packet's checksum verifies and every ground `0x2c` re-encodes byte for byte. |
| `fakehost.py <pcap>` | A DirectPlay host that is not TA, for a real TA to join and play against. It needs a capture of a real TA host that a second TA joined, recorded from before the join. It answers the session handshake, pings and unit sync itself, replays the recorded battleroom and launch, and then owns the host's units: `touch go` releases the launch, `echo joiner > walk` walks the host's commander to the joiner's. `--tcp-port`, `--udp-port` and `--enum-port` move what it binds. |
| `fakejoin.py <pcap>` | The other side: a joiner that is not TA, for an RWE host to be checked against. It discovers the host with ENUMSESSIONS, does the join handshake, sits in the battleroom, runs unit sync as a joiner, readies, follows the launch and then owns a commander in game — `0x2c` six ticks to a packet, `--walk X,Z` walks it, `--attack ID` fires at one of the host's units, and it sends `0x0c` when the host's damage should have killed it. It checks the host against `docs/TA-NETWORK.md` as it goes, prints a PASS or FAIL line per check, and exits non-zero on any failure. It needs a capture of a real TA joining a real host, for the unit ids and CRCs it does not compute. |
| `host-check.sh` | Starts a host given as an argument, runs `fakejoin.py` against it on loopback, stops the host by its saved pid, and exits with the joiner's status. Runs on Linux and is CI-able. |
| `tanet.py` | The shared module: DirectPlay framing, the messages a joining client sends, TA's transforms and subpacket table, and a `0x2c` encoder and decoder. |

## Checking a host

`fakejoin.py` against `fakehost.py` is the loopback acceptance test, and it needs nothing but the
two scripts and a capture. Captures are not in the tree, so the path below is wherever yours is —
`ta-small.pcap` and `ta-baseline.pcap` are the two from the spike that were developed against:

```sh
mkdir -p /tmp/fj
tools/ta-net/host-check.sh --capture ta-small.pcap --release /tmp/fj/go --dir /tmp/fj -- \
    tools/ta-net/fakehost.py ta-small.pcap --dir /tmp/fj --tcp-port 34700 --udp-port 34750 --enum-port 47625
```

`--release` is the fake host's gate: it holds the launch until its `go` file exists, and
`host-check.sh` creates it as soon as the joiner reports itself ready, so the run needs no human in
it. The default ports (34700/34750/47625 for the host, 34701/34751 for the joiner) are clear of the
2300/2350 and 47624 a real TA or `dplaysvr` holds.

Once the RWE host exists the only change is the command after `--`:

```sh
tools/ta-net/host-check.sh --capture ta-small.pcap --play-seconds 60 -- \
    build-release/rwe --ta-host --host-map "Coast To Coast"
```

`--walk`, `--attack` and `--simulate-hit` go to the joiner; anything `host-check.sh` does not
recognise is passed to it, so they need no flag of their own.

The C++ port of the session layer lives in the engine: `src/rwe/net/ta/`. Build
`ta_host_probe` and run it with `--session-name x --map <map>` to host a
DirectPlay game the tools here can find; `dpenum.py` then prints its
ENUMSESSIONSREPLY.

## Recording a capture

Two copies of TA on one Linux machine, both under Proton with native DirectPlay. The test bed
section of the write-up says why each step is needed.

```sh
protontricks <appid> directplay                                   # once, in TA's prefix
cp -a ~/.local/share/Steam/steamapps/compatdata/<appid> ~/.ta-p2  # a second prefix, after the first has quit

sudo tcpdump -i any -w game.pcap 'port 47624 or portrange 2300-2400'
# host from Steam; then join from the second prefix at 127.0.0.1:
STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam STEAM_COMPAT_DATA_PATH=~/.ta-p2 \
  "$HOME/.local/share/Steam/steamapps/common/Proton 11.0/proton" run <TA dir>/TotalA.exe
```

For `fakehost.py`, launch the game and stay in it for at least ten seconds, so that the host's
commander has sent a full-state record. Quit the joiner before the host.

`ta-small.pcap` and `ta-baseline.pcap` are what the two scripts were developed against;
`ta-options.pcap` was recorded after the join, so it holds no handshake and neither will take it as a
template. `docs/TA-NETWORK.md`, "Playtesting against a real TA", is the runbook for a session with a
real TA.

Captures are not checked in: they hold LAN addresses and player names. Before starting
`fakehost.py`, stop anything that still holds the DirectPlay ports:
`pkill -f dplaysvr.exe; ss -lntu | grep -E ':(47624|2300|2350)\b'`.
