#!/usr/bin/env bash
# Build a LittleFS image of the TamaPoke sprite pack for the 1.75C
# (boards/175c/partitions.csv "storage" partition, 0x1CE0000 bytes at 0x310000).
#
# The full pack (151 normal + 151 shiny + thumbs) is ~41.7 MB and will not
# fit in 32 MB flash. This script packs the 151 normal sprites + thumbs.bin,
# then as many shinies from tools/shiny_175c.txt as fit (most popular first),
# leaving SAFETY_MARGIN bytes of the partition free. A shiny that does not
# fit is omitted; the firmware falls back to the normal sprite.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${ROOT}/tools/sdcard/mons"
STAGING="${ROOT}/tools/build/spritefs_staging"
OUT="${1:-${ROOT}/tools/build/storage.littlefs.bin}"
# Must match the "storage" partition size in boards/175c/partitions.csv
IMG_SIZE=$((0x1CE0000))
BLOCK=4096
PAGE=256
# Left free for LittleFS metadata beyond the estimate, and for a shiny sent later over USB.
SAFETY_MARGIN="${SAFETY_MARGIN:-$((512 * 1024))}"
SHINY_LIST="${SHINY_LIST:-${ROOT}/tools/shiny_175c.txt}"

MKLITTLEFS="${MKLITTLEFS:-}"
if [[ -z "${MKLITTLEFS}" ]]; then
  # Prefer the binary that ships with the Arduino ESP32 core
  for c in \
    "${HOME}/.arduino15/packages/esp32/tools/mklittlefs/"*/mklittlefs \
    "$(command -v mklittlefs || true)"; do
    if [[ -x "$c" ]]; then MKLITTLEFS="$c"; break; fi
  done
fi
if [[ -z "${MKLITTLEFS}" || ! -x "${MKLITTLEFS}" ]]; then
  echo "mklittlefs not found. Install the Arduino ESP32 core or set MKLITTLEFS=" >&2
  exit 1
fi

if [[ ! -d "$SRC" ]]; then
  echo "Sprite pack not found at $SRC" >&2
  echo "Generate it with: python3 tools/pack_pmd.py && python3 tools/make_thumbs.py" >&2
  exit 1
fi

rm -rf "$STAGING"
mkdir -p "$STAGING/mons" "$(dirname "$OUT")"

# Normal forms + gallery thumbs only (see PORTING.md for the trade-off)
copied=0
missing=0
for i in $(seq -w 1 151); do
  n="${i#"${i%%[!0]*}"}"  # not needed, we use printf
  :
done
for n in $(seq 1 151); do
  f=$(printf "p%03d.bin" "$n")
  if [[ -f "$SRC/$f" ]]; then
    cp -f "$SRC/$f" "$STAGING/mons/$f"
    copied=$((copied + 1))
  else
    echo "missing $f" >&2
    missing=$((missing + 1))
  fi
done
if [[ -f "$SRC/thumbs.bin" ]]; then
  cp -f "$SRC/thumbs.bin" "$STAGING/mons/thumbs.bin"
else
  echo "warning: thumbs.bin missing (gallery will have no thumbnails)" >&2
fi

[[ -f "$SHINY_LIST" ]] || { echo "Missing shiny list $SHINY_LIST" >&2; exit 1; }
python3 "${ROOT}/tools/select_shinies.py" "$SRC" "$STAGING" "$SHINY_LIST" "$IMG_SIZE" "$SAFETY_MARGIN" "$BLOCK"

bytes=$(du -sb "$STAGING" | awk '{print $1}')
echo "Staging ${copied} normal sprites + thumbs + shinies, ${bytes} bytes raw (limit ${IMG_SIZE})"
if (( bytes >= IMG_SIZE )); then
  echo "Pack is larger than the LittleFS partition" >&2
  exit 1
fi
if (( missing > 0 )); then
  echo "Refusing to build an incomplete image ($missing missing)" >&2
  exit 1
fi

# -a includes files that would otherwise be skipped; -s is the partition size.
"${MKLITTLEFS}" -c "$STAGING" -b "$BLOCK" -p "$PAGE" -s "$IMG_SIZE" "$OUT"
ls -l "$OUT"
echo "Wrote $OUT"
echo "Flash at 0x310000, e.g.: tools/flash_175c.sh --port /dev/ttyACM0 --fs-only"
