#!/bin/sh
# Regenerates libs/glad: GLAD 2.0.8, GL 3.2 core plus the one extension the
# engine calls unconditionally, GL_ARB_texture_storage. The output is checked
# in, so this is only run when the loader is deliberately refreshed.
set -eu

uvx --from glad2==2.0.8 glad \
    --api gl:core=3.2 --extensions GL_ARB_texture_storage \
    --out-path "$(dirname "$0")/../libs/glad"
