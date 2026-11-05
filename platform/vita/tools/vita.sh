#!/usr/bin/env bash
# vita.sh - test data of the PMD Vita port in one step (runs on the Pi).
#
#   ./vita.sh fetch <name>   fetch log.txt, log_prev.txt, crumbs.txt, settings.ini,
#                            romindex.txt (size only), the quick save, all core dumps
#                            (ux0:data/*.psp2dmp) and all screenshots
#                            (ux0:picture/SCREENSHOT/**) from the Vita into runs/<name>/,
#                            delete the screenshots and dumps on the Vita, commit, push
#   ./vita.sh get <file>     fetch one file from ux0:data/pmd/ into files/ (e.g. textindex.bin)
#   ./vita.sh vpk <file>     upload a VPK to ux0:data/pmd/ (install it with VitaShell)
#   ./vita.sh install        git pull, then upload the newest builds/*.vpk of the data repo
#   ./vita.sh log            show the tail of the Vita's log.txt without fetching
#   ./vita.sh set key=value  change one key of settings.ini on the Vita (e.g. text_debug=1)
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

# the Vita answers only while VitaShell's FTP server runs (Select in VitaShell)
reach() {
    if ! nc -z -w 3 "$VITA" "${PORT:-1337}" 2>/dev/null; then
        echo "Vita $VITA:${PORT:-1337} nicht erreichbar: VitaShell oeffnen, Select druecken (FTP), IP mit vita.conf vergleichen"
        exit 1
    fi
}
ftp() { reach; lftp -e "set net:timeout 30; set net:max-retries 8; set net:reconnect-interval-base 3; set ftp:passive-mode on; $1; bye" "$URL"; }
# upload <file> [name on the Vita]: curl (retries, resumes, shows progress)
# with lftp as the fallback - the Vita's Wi-Fi is slow to wake up and drops
# long transfers now and then
upload() {
    local f="$1" n="${2:-$(basename "$1")}" i
    reach
    if command -v curl >/dev/null; then
        for i in 1 2 3 4 5; do
            curl -# -T "$f" --ftp-pasv --connect-timeout 15 --speed-time 60 --speed-limit 1 \
                --retry 3 --retry-delay 3 "$URL/$DATA/$n" && return 0
            echo "Upload-Versuch $i fehlgeschlagen, neuer Versuch ..."
            sleep 3
        done
    fi
    lftp -e "set net:timeout 30; set net:max-retries 8; set net:reconnect-interval-base 3; set ftp:passive-mode on; put -c -O $DATA '$f' -o '$n'; bye" "$URL"
}

case "${1:-}" in
fetch)
    name="${2:?Name des Laufs fehlt, z. B. run3}"
    out="$here/runs/$name"
    rm -rf "$out"
    mkdir -p "$out/screenshots" "$out/dumps"
    echo "== Logs"
    ftp "lcd '$out'; mget -O . $DATA/log.txt $DATA/log_prev.txt $DATA/crumbs.txt $DATA/settings.ini $DATA/quicksave.bin $DATA/pmd_red.sav" || true
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
    git commit -q -m "Vita run $name"
    git pull -q --rebase --autostash origin main || true
    git push
    echo "== fertig: runs/$name ist auf GitHub"
    ;;
vpk)
    vpk="${2:?Pfad zur VPK fehlt}"
    upload "$vpk"
    echo "== $vpk liegt in $DATA/ - in VitaShell oeffnen und installieren"
    ;;
get)
    # ./vita.sh get <file>   -> one file from ux0:data/pmd/ into files/ of the data repo (e.g. textindex.bin)
    f="${2:?Dateiname fehlt, z. B. textindex.bin}"
    mkdir -p "$here/files"
    ftp "lcd '$here/files'; get $DATA/$f"
    ls -la "$here/files/$f"
    cd "$here" && git add "files/$f" && git commit -q -m "files: $f" && git pull -q --rebase --autostash origin main && git push -q origin main
    echo "== $f liegt im Daten-Repo unter files/"
    ;;
log)
    ftp "cat $DATA/log.txt" | tail -40
    ;;
install)
    # ./vita.sh install   -> newest VPK from builds/ (git pull first) onto the Vita
    cd "$here" && git pull -q --rebase --autostash origin main || true
    vpk="$(ls -1 "$here"/builds/*.vpk 2>/dev/null | sort -V | tail -1)"
    [ -n "$vpk" ] || { echo "keine VPK in builds/"; exit 1; }
    echo "== $vpk"
    upload "$vpk"
    echo "== liegt in $DATA/ - in VitaShell oeffnen und installieren"
    ;;
set)
    # ./vita.sh set text_debug=1   -> changes one key of settings.ini on the Vita
    kv="${2:?key=value fehlt, z. B. text_debug=1}"
    key="${kv%%=*}"
    tmp="$(mktemp)"
    ftp "get -O '$(dirname "$tmp")' $DATA/settings.ini -o '$(basename "$tmp")'" 2>/dev/null || true
    grep -v "^$key=" "$tmp" > "$tmp.new" 2>/dev/null || true
    echo "$kv" >> "$tmp.new"
    upload "$tmp.new" settings.ini
    rm -f "$tmp" "$tmp.new"
    echo "== settings.ini auf der Vita: $kv (gilt ab dem naechsten Start des Spiels)"
    ;;
*)
    sed -n 2,14p "$0"
    exit 1
    ;;
esac
