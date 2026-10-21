#!/bin/sh
# Per-user Linux installer. Uses standard system tools; no Python or jq.
set -eu
release=${RWE_RELEASE:-nightly}
root=${RWE_INSTALL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/rwe/program}
bin=${RWE_BIN_DIR:-$HOME/.local/bin}
skip_setup=${RWE_SKIP_SETUP:-0}
case "$skip_setup" in 0|1) ;; *) echo 'RWE_SKIP_SETUP must be 0 or 1.' >&2; exit 1 ;; esac
if [ "$skip_setup" = 1 ] && [ -n "${RWE_TA_DIR:-}" ]; then
    echo 'Choose RWE_TA_DIR or RWE_SKIP_SETUP, not both.' >&2; exit 1
fi
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) ;;
    *) echo 'RWE requires x86-64 Linux.' >&2; exit 1 ;;
esac
case "$release" in *[!a-zA-Z0-9._-]*|'') echo 'Invalid release tag.' >&2; exit 1 ;; esac
fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 --connect-timeout 30 --output "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget --tries=3 --timeout=30 -O "$2" "$1"
    else
        echo 'This system needs curl or wget to download RWE.' >&2; return 1
    fi
}
mkdir -p "$root" "$bin"
root=$(cd "$root" && pwd)
stage=$(mktemp -d "$root/.install-XXXXXXXX")
# stage is an absolute, unique directory directly inside the installation root.
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 1' HUP INT TERM
fetch "https://api.github.com/repos/CubeB/RWE-B4/releases/tags/$release" "$stage/release.json"
# GitHub returns pretty-printed JSON. Extract only the known release asset
# fields; no release content is evaluated as shell code.
awk '
    /"name":/ { selected = ($0 ~ /-x86_64[.]AppImage"/); digest = "" }
    selected && /"digest":/ { digest = $0; sub(/^.*"sha256:/, "", digest); sub(/".*$/, "", digest) }
    selected && /"browser_download_url":/ {
        url = $0; sub(/^.*"browser_download_url": *"/, "", url); sub(/".*$/, "", url)
        print url; print digest
    }
' "$stage/release.json" > "$stage/asset"
if [ "$(wc -l < "$stage/asset")" -ne 2 ]; then
    echo 'Release must contain exactly one x86-64 AppImage.' >&2; exit 1
fi
url=$(sed -n '1p' "$stage/asset")
digest=$(sed -n '2p' "$stage/asset")
case "$url" in https://github.com/CubeB/RWE-B4/releases/download/*) ;;
    *) echo 'Unexpected release download URL.' >&2; exit 1 ;; esac
fetch "$url" "$stage/rwe.AppImage"
if [ "${#digest}" -ne 64 ] || [ "$(sha256sum "$stage/rwe.AppImage" | cut -d ' ' -f 1)" != "$digest" ]; then
    echo 'Missing or mismatched release SHA-256 checksum.' >&2; exit 1
fi
chmod +x "$stage/rwe.AppImage"
version="$root/sha256-$digest"
mkdir -p "$version"
mv -f "$stage/rwe.AppImage" "$version/rwe.AppImage"
ln -sfn "$version/rwe.AppImage" "$root/current.AppImage"
# Resolve the image relative to the launcher so paths containing spaces work.
cat > "$root/rwe" <<'LAUNCH'
#!/bin/sh
directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$directory/current.AppImage" "$@"
LAUNCH
cat > "$root/rwe-setup" <<'LAUNCH'
#!/bin/sh
directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$directory/current.AppImage" setup "$@"
LAUNCH
chmod +x "$root/rwe" "$root/rwe-setup"
# Use small forwarding scripts rather than symlinks: $0 must remain at root.
for name in rwe rwe-setup; do
    # Single-quote paths safely in the generated shell command.
    escaped=$(printf '%s' "$root/$name" | sed "s/'/'\\\\''/g")
    printf '#!/bin/sh\nexec '\''%s'\'' "$@"\n' "$escaped" > "$bin/$name"
    chmod +x "$bin/$name"
done
printf 'RWE installed in %s\n' "$root"
printf 'Play: %s/rwe\nImport game data: %s/rwe-setup --from "/path/to/Total Annihilation"\n' "$bin" "$bin"
case ":$PATH:" in *":$bin:"*) ;; *) printf 'Add %s to PATH to use rwe from any terminal.\n' "$bin" ;; esac
if [ "$skip_setup" = 1 ]; then
    echo 'Game setup skipped. Run rwe-setup when ready.'
else
    echo 'Setting up your Total Annihilation game data...'
    if [ -n "${RWE_TA_DIR:-}" ]; then
        "$root/rwe-setup" --from "$RWE_TA_DIR" || {
            echo 'Game data import failed. RWE remains installed; run rwe-setup to retry.' >&2; exit 1
        }
    else
        "$root/rwe-setup" || {
            echo 'Game setup could not finish. RWE remains installed; run rwe-setup --from "/path/to/Total Annihilation" to retry.' >&2
            exit 1
        }
    fi
fi
