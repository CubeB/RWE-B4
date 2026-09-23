# The master server, for a host that is not somebody's desktop.
#
# It is one Node process: the game listing and the lobby rooms both, since
# GameServer runs inside master-server.ts. Real game traffic never reaches it
# -- that is peer-to-peer UDP on 6670 and up -- so what this has to be is small
# and always up rather than fast.
#
#   docker build -t rwe-master -f launcher/Dockerfile launcher
#   docker run -p 5000:5000 rwe-master
#
# or, with TLS and a hostname in front of it, deploy/docker-compose.yml.

FROM node:22-bookworm-slim AS build
WORKDIR /build

# The launcher and the master server share one package.json, so installing it
# pulls in Electron -- a hundred megabytes of browser this image will never
# run. The download is the only part that is optional, and this is the flag
# that skips it.
ENV ELECTRON_SKIP_BINARY_DOWNLOAD=1

COPY package.json package-lock.json ./
RUN npm ci

COPY tsconfig.json babel.config.js webpack.master.config.js ./
COPY src ./src

# One file out, with its dependencies bundled into it: webpack targets node
# here and nothing is left external, which is what lets the runtime stage
# carry no node_modules at all.
RUN npm run build:master


FROM node:22-bookworm-slim AS runtime
WORKDIR /app

# node:22-slim ships a `node` user. Nothing here needs root, and the process
# listens on 5000 rather than on a privileged port precisely so it does not
# have to.
COPY --from=build --chown=node:node /build/dist/master-server.js ./master-server.js
USER node

EXPOSE 5000

# Alive when the port answers. The server speaks socket.io and nothing else,
# so there is no health route to ask for and a connect is the whole test.
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "require('net').connect(5000,'127.0.0.1').on('connect',()=>process.exit(0)).on('error',()=>process.exit(1))"

# Overridden by compose, which adds --reverse-proxy: behind Caddy the socket's
# own address is Caddy's, and the lobby hands out player addresses for peers to
# connect to directly, so it has to read the forwarded one instead.
ENTRYPOINT ["node", "master-server.js"]
CMD ["--port", "5000"]
