# Total Annihilation on the network

Tools from the spike that asked whether RWE could take part in a network game with a real
`TotalA.exe` (issue #386). What they found is written up in
[`docs/TA-NETWORK.md`](../../docs/TA-NETWORK.md); these are kept so it can be repeated and
extended rather than taken on trust.

Nothing here is built or shipped. They run with `uv run --script`, which fetches `scapy` for the
two that read captures. None of them needs root, but recording a capture does.

| Tool | What it does |
|---|---|
| `dpenum.py [my-lan-ip]` | Sends a DirectPlay EnumSessions request every way a joiner might, and prints any ENUMSESSIONSREPLY. The first check on a new machine: if a TA that is hosting does not answer, nothing owns port 47624. |
| `ta-capture.py <pcap>` | Reads a capture of TA playing over DirectPlay: flows, the DirectPlay sequence, and every TA subpacket. `--timeline`, `--settings` (options byte and teams), `--units` (decoded `0x2c`). `--check` exits non-zero unless every TA packet's checksum verifies and every ground `0x2c` re-encodes byte for byte. |
| `fakehost.py <pcap>` | A DirectPlay host that is not TA, for a real TA to join and play against. It needs a capture of a real TA host that a second TA joined, recorded from before the join. It answers the session handshake, pings and unit sync itself, replays the recorded battleroom and launch, and then owns the host's units: `touch go` releases the launch, `echo joiner > walk` walks the host's commander to the joiner's. |
| `tanet.py` | The shared module: DirectPlay framing, TA's transforms and subpacket table, and a `0x2c` encoder and decoder. |

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

Captures are not checked in: they hold LAN addresses and player names. Before starting
`fakehost.py`, stop anything that still holds the DirectPlay ports:
`pkill -f dplaysvr.exe; ss -lntu | grep -E ':(47624|2300|2350)\b'`.
