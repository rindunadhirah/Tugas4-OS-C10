#!/usr/bin/env bash
# teardown_lab.sh - Menghapus Media B: umount -> lepas loop device -> hapus image
#
# Pemakaian:
#   sudo ./teardown_lab.sh


set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG="${LAB_IMG:-$SCRIPT_DIR/lab_disk.img}"
MNT="${LAB_MNT:-/mnt/lab}"

info() { echo "[teardown] $*"; }

if [[ $EUID -ne 0 ]]; then
    exec sudo --preserve-env=LAB_IMG,LAB_MNT "$0" "$@"
fi

IMG="$(realpath -m "$IMG")"

# Semua loop device yang terhubung ke image ini, termasuk yang image-nya
# sudah terhapus (losetup menampilkannya dengan akhiran " (deleted)")
mapfile -t LOOPS < <(
    losetup -nO NAME,BACK-FILE 2>/dev/null \
        | awk -v f="$IMG" '{ n=$1; $1=""; sub(/^ /,""); sub(/ \(deleted\)$/,"") }
                           $0 == f { print n }'
)

safe_umount() {
    local target="$1"
    if umount "$target" 2>/dev/null; then
        info "umount $target"
    else
        info "umount $target gagal (masih dipakai?), proses yang memakai:"
        fuser -vm "$target" 2>&1 | sed 's/^/           /' || true
        info "pakai lazy umount supaya tetap bisa dilepas"
        umount -l "$target" || true
    fi
}

# 1. umount mount point
if mountpoint -q "$MNT"; then
    safe_umount "$MNT"
else
    info "$MNT tidak ter-mount, lewati"
fi

# 2. Lepas semua loop device milik image ini (umount dulu kalau masih ter-mount)
if [[ ${#LOOPS[@]} -eq 0 ]]; then
    info "tidak ada loop device untuk $IMG, lewati"
fi
for loop in "${LOOPS[@]}"; do
    while read -r t; do
        [[ -n "$t" ]] && safe_umount "$t"
    done < <(findmnt -rno TARGET -S "$loop" 2>/dev/null || true)
    # Kalau dmsetup (mis. dari varian lain) masih memegang loop ini
    if command -v dmsetup >/dev/null; then
        holders=(/sys/block/"$(basename "$loop")"/holders/*)
        for h in "${holders[@]}"; do
            [[ -e "$h" ]] || continue
            name="$(cat "$h/dm/name" 2>/dev/null || true)"
            [[ -n "$name" ]] && { info "hapus device-mapper $name"; dmsetup remove -f "$name" || true; }
        done
    fi
    if losetup -d "$loop" 2>/dev/null; then
        info "lepas $loop"
    else
        info "gagal melepas $loop"
    fi
done

# 3. Hapus image
if [[ -f "$IMG" ]]; then
    rm -f "$IMG"
    info "hapus $IMG"
else
    info "image $IMG tidak ada, lewati"
fi

# 4. Hapus mount point kalau kosong (abaikan kalau berisi atau tidak ada)
rmdir "$MNT" 2>/dev/null && info "hapus folder $MNT"

info "selesai"
exit 0
