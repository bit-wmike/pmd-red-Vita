#!/usr/bin/env bash
# vita.sh - test data of the PMD Vita port in one step (runs on the Pi).
#
#   ./vita.sh fetch <name>   fetch log.txt, log_prev.txt, crumbs.txt, settings.ini,
#                            romindex.txt (size only), the quick save, all core dumps
#                            (ux0:data/*.psp2dmp) and all screenshots
#                            (ux0:picture/SCREENSHOT/**) from the Vita into runs/<name>/,
#                            delete the screenshots and dumps on the Vita, commit, push
#   ./vita.sh vpk <file>     upload a VPK to ux0:data/pmd/ (install it with VitaShell)
#   ./vita.sh log            show the tail of the Vita's log.txt without fetching
#
# Needs: lftp (sudo apt install lftp), VitaShell with its FTP server running
# (SELECT in VitaShell), and the Vita's address in vita.conf next to this
# script:   VITA=192.168.178.50   PORT=1337   (PORT is optional)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
conf="$here/vita.conf"
[ -f "$conf" ] || { echo "vita.conf fehlt: 'VITA=<IP der Vita>' hineinschreiben (VitaShell -> FTP zeigt sie an)"; exit 1; }
# shellcheck disable=SC1090
. "$conf"
PORT="${PORT:-1337}"
URL="ftp://$VITA:$PORT"
DATA="ux0:/data/pmd"

ftp() { lftp -e "set net:timeout 10; set net:max-retries 2; set ftp:passive-mode on; $1; bye" "$URL"; }

case "${1:-}" in
fetch)
    name="${2:?Name des Laufs fehlt, z. B. run3}"
    out="$here/runs/$name"
    rm -rf "$out"
    mkdir -p "$out/screenshots" "$out/dumps"
    echo "== Logs"
    ftp "lcd '$out'; mget -O . $DATA/log.txt $DATA/log_prev.txt $DATA/crumbs.txt $DATA/settings.ini $DATA/quicksave.bin" || true
    echo "== Core-Dumps (ux0:data/*.psp2dmp)"
    ftp "lcd '$out/dumps'; mget -O . 'ux0:/data/*.psp2dmp'" 2>/dev/null || true
    echo "== Screenshots (alle Unterordner)"
    ftp "mirror --verbose=0 ux0:/picture/SCREENSHOT '$out/screenshots'" 2>/dev/null || true
    find "$out/screenshots" -type d -empty -delete 2>/dev/null || true
    echo "== Auf der Vita loeschen (Screenshots, Dumps)"
    ftp "rm -rf ux0:/picture/SCREENSHOT; mkdir ux0:/picture/SCREENSHOT; mrm 'ux0:/data/*.psp2dmp'" 2>/dev/null || true
    # dumps are large: keep them out of git beyond 50 MB
    find "$out/dumps" -type f -size +50M -exec sh -c 'echo "zu gross fuer git, bleibt lokal: $1"; mv "$1" "$1.local"' _ {} \;
    printf '%s\n' "fetched $(date -Is) from $VITA" > "$out/INFO.txt"
    ls -la "$out" "$out/screenshots" 2>/dev/null | head -40
    cd "$here"
    git add -A "runs/$name"
    git commit -q -m "Vita run $name" && git push
    echo "== fertig: runs/$name ist auf GitHub"
    ;;
vpk)
    vpk="${2:?Pfad zur VPK fehlt}"
    ftp "put -O $DATA '$vpk'"
    echo "== $vpk liegt in $DATA/ - in VitaShell oeffnen und installieren"
    ;;
log)
    ftp "cat $DATA/log.txt" | tail -40
    ;;
*)
    sed -n 2,14p "$0"
    exit 1
    ;;
esac
