#!/bin/sh
# Build (or flash, monitor, menuconfig ...) one board.
#
#   scripts/build.sh esp32c6-devkitc-1              # build
#   scripts/build.sh esp32c6-devkitc-1 flash monitor
#   scripts/build.sh oh-base menuconfig
#   scripts/build.sh esp32-1732s019 -p /dev/cu.usbmodem101 flash
set -e
BOARD="${1:?usage: $0 <board> [idf.py args...]}"
shift
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
case "$BOARD" in
    oh-base) TARGET=esp32 ;;
    esp32c6-devkitc-1) TARGET=esp32c6 ;;
    esp32-1732s019) TARGET=esp32s3 ;;
    *) echo "unknown board $BOARD (see boards/)"; exit 1 ;;
esac
if ! command -v idf.py >/dev/null 2>&1; then
    . "$ROOT/scripts/env.sh"
fi
cd "$ROOT"
# sdkconfig is generated from the defaults only when it does not exist yet, so
# a change to sdkconfig.defaults or boards/*/sdkconfig.board needs --fresh.
if [ "$1" = "--fresh" ]; then
    rm -f "build/$BOARD/sdkconfig"
    shift
fi
[ $# -eq 0 ] && set -- build
exec idf.py -B "build/$BOARD" -DBOARD="$BOARD" -DIDF_TARGET="$TARGET" "$@"
