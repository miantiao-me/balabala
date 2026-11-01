#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/versions.sh"
case "${BALABALA_UI_FONT:-neoxihei}" in
  neoxihei|ipaex) ;;
  *) echo 'BALABALA_UI_FONT must be neoxihei or ipaex' >&2; exit 1 ;;
esac
mkdir -p "$root/.tools/fonts" "$root/.tools/downloads"
font="$root/.tools/fonts/LXGWNeoXiHei.ttf"
if [ ! -f "$font" ]; then
  curl -fL --retry 2 "https://github.com/lxgw/LxgwNeoXiHei/releases/download/v$NEOXIHEI_VERSION/LXGWNeoXiHei.ttf" -o "$font.download"
  [ "$(shasum -a 256 "$font.download" | cut -d ' ' -f 1)" = "$NEOXIHEI_SHA256" ] || { echo 'NeoXiHei SHA256 mismatch' >&2; exit 1; }
  mv "$font.download" "$font"
fi
[ "$(shasum -a 256 "$font" | cut -d ' ' -f 1)" = "$NEOXIHEI_SHA256" ] || { echo 'NeoXiHei SHA256 mismatch' >&2; exit 1; }
[ "${BALABALA_UI_FONT:-neoxihei}" = ipaex ] || exit 0
font="$root/.tools/fonts/ipaexg.ttf"
if [ ! -f "$font" ]; then
  archive="$root/.tools/downloads/ipaexg00401.zip"
  curl -fL --retry 2 'https://moji.or.jp/wp-content/ipafont/IPAexfont/ipaexg00401.zip' -o "$archive"
  [ "$(shasum -a 256 "$archive" | cut -d ' ' -f 1)" = "$IPAEX_ARCHIVE_SHA256" ] || { echo 'IPAex archive SHA256 mismatch' >&2; exit 1; }
  unzip -p "$archive" 'ipaexg00401/ipaexg.ttf' > "$font.download"
  [ "$(shasum -a 256 "$font.download" | cut -d ' ' -f 1)" = "$IPAEX_SHA256" ] || { echo 'IPAex font SHA256 mismatch' >&2; exit 1; }
  mv "$font.download" "$font"
fi
[ "$(shasum -a 256 "$font" | cut -d ' ' -f 1)" = "$IPAEX_SHA256" ] || { echo 'IPAex font SHA256 mismatch' >&2; exit 1; }
