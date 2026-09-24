#!/bin/sh
# Regenerates libs/glad with GLAD 2.0.8: GL 3.2 core plus the one extension
# the engine calls unconditionally, GL_ARB_texture_storage. The env vars keep
# uv working where HOME is read-only.
set -eu

export UV_CACHE_DIR=/tmp/opencode/uv-cache UV_TOOL_DIR=/tmp/opencode/uv-tools \
    UV_PYTHON_INSTALL_DIR=/tmp/opencode/uv-python TMPDIR=/tmp/opencode

uvx --python /usr/bin/python3 --from glad2==2.0.8 glad \
    --api gl:core=3.2 --extensions GL_ARB_texture_storage \
    --out-path "$(dirname "$0")/../libs/glad"
