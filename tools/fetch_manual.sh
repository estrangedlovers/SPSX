#!/usr/bin/env bash
# Downloads the hardware owner's manual (multilingual, English = PDF pages 3-54)
# into docs/. The file is git-ignored: it is large and Roland-copyrighted.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
url="https://static.roland.com/assets/media/pdf/SP-404SX_egfsipd02_W.pdf"
curl -fSL -o "$here/docs/SP-404SX_Owners_Manual.pdf" "$url"
echo "Saved to docs/SP-404SX_Owners_Manual.pdf"
