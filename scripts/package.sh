#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/package.sh [VERSION] [--skip-build]

Creates dist/mini-cloud-<VERSION>.tar.gz containing the server, client, and
deployment assets. VERSION defaults to the current UTC timestamp.
EOF
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION=""
SKIP_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [[ -n "$VERSION" ]]; then
                echo "Multiple VERSION arguments detected" >&2
                usage
                exit 1
            fi
            VERSION="$1"
            shift
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    VERSION="$(date -u +%Y%m%d-%H%M%S)"
fi

STAGING_DIR="$DIST_DIR/mini-cloud-$VERSION"
TARBALL="$DIST_DIR/mini-cloud-$VERSION.tar.gz"

mkdir -p "$DIST_DIR"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/bin" "$STAGING_DIR/deploy" "$STAGING_DIR/storage"

if [[ $SKIP_BUILD -eq 0 ]]; then
    (cd "$ROOT_DIR" && make clean && make server client)
fi

for bin in server client; do
    if [[ ! -x "$ROOT_DIR/bin/$bin" ]]; then
        echo "Missing binary: $ROOT_DIR/bin/$bin" >&2
        exit 1
    fi
    cp "$ROOT_DIR/bin/$bin" "$STAGING_DIR/bin/$bin"
    strip -s "$STAGING_DIR/bin/$bin" 2>/dev/null || true
done

cp "$ROOT_DIR/README.md" "$STAGING_DIR/README.md"
cp "$ROOT_DIR/deploy/README_DEPLOY.md" "$STAGING_DIR/deploy/README_DEPLOY.md"
cp "$ROOT_DIR/deploy/server.env.example" "$STAGING_DIR/deploy/server.env.example"
cp "$ROOT_DIR/deploy/mini-cloud.service" "$STAGING_DIR/deploy/mini-cloud.service"

cat <<'NOTE' >"$STAGING_DIR/storage/README.txt"
This directory will hold files uploaded by clients at runtime.
Create a dedicated volume for it in production and ensure the service user
has write permission.
NOTE

( cd "$DIST_DIR" && tar -czf "$TARBALL" "mini-cloud-$VERSION" )
rm -rf "$STAGING_DIR"

echo "Created $TARBALL"
