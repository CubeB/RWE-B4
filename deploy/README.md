# Running a master server

The launcher's multiplayer needs one server, and it is small. `master-server.ts`
is a single Node process serving socket.io on two namespaces — `/master` for the
games list and `/rooms` for the lobbies — and `GameServer` runs inside it, so
that one process holds the listing, the rooms, chat, the archive fingerprint
check and the rejoin relay. **No game traffic passes through it**: a game is
peer-to-peer UDP on 6670 and up, agreed in the lobby and then direct. Its
bandwidth is a few kilobytes a player plus the odd rejoin bundle.

What it does need is to be *always up*. A listing server that has gone to sleep
shows no games, and a lobby socket that drops mid-match takes the rejoin path
with it. That rules out the free tiers that idle out, and serverless generally:
the rooms live in the process's memory and the sockets are long-lived.

## The shape of it

```
  launcher ──wss──▶ Caddy ──http──▶ master-server (node, :5000)
  launcher ◀───────────── UDP 6670+ ─────────────▶ launcher
```

Caddy is there for the certificate. The server itself speaks plain HTTP and
takes `--reverse-proxy`, which makes it read the client's address out of
`X-Forwarded-For` instead of off the socket — and that address matters more than
it looks, because it is what the lobby hands the other players so they can
connect to each other. Get it wrong and the lobby works while every game sends
everybody to the server.

## Deploying

Any host with Docker will do; a €4-a-month VPS is more than enough. You need a
domain name pointed at it — Let's Encrypt will not issue for a bare IP.

```sh
# on the host
git clone <this repo> rwe && cd rwe/deploy
cp .env.example .env            # put your domain in RWE_MASTER_DOMAIN
docker compose up -d --build
docker compose logs -f
```

The first start takes a minute or two: the build installs the launcher's
dependencies (Electron's binary excepted — see the Dockerfile) and webpack
bundles the server into one file, and Caddy fetches a certificate. After that,
`docker compose up -d --build` is also how you deploy a new version.

To run it without Docker, `npm ci && npm run build:master && node
dist/master-server.js --port 5000 --reverse-proxy` is the whole of it; put it
under systemd and something else in front for TLS.

## Pointing the launcher at it

`masterServer()` in `src/common/util.ts` still defaults to
`https://master.rwe.michaelheasell.com`, which is upstream's — and which this
launcher can no longer talk to, the dependency refresh having moved it to
socket.io 4 against that server's socket.io 2. Either set the environment
variable:

```sh
RWE_MASTER_SERVER=https://master.example.com npm start
```

or change the default in that function before building something to hand to
anybody else.

## What is not solved here

Players still need UDP 6670 and up reachable from the other players, which for
most people means port forwarding. The server cannot help with that: it never
sees game traffic. Hole punching or a relay would be a different piece of work
and a much larger one.
