#!/usr/bin/env bash
# setup_lab.sh - Menyiapkan Media B: image 64 MB -> loop device -> mkfs -> mount
#
# Pemakaian:
#   sudo ./setup_lab.sh            # ext4 (default)
#   sudo ./setup_lab.sh f2fs       # F2FS
#   sudo FORCE=1 ./setup_lab.sh    # paksa format ulang walau sudah ter-mount


set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG="${LAB_IMG:-$SCRIPT_DIR/lab_disk.img}"
MNT="${LAB_MNT:-/mnt/lab}"
SIZE_MB="${LAB_MB:-64}"
FS="${1:-ext4}"
FORCE="${FORCE:-0}"

die()  { echo "[setup] ERROR: $*" >&2; exit 1; }
info() { echo "[setup] $*"; }

# Minta root; jalankan ulang dengan sudo bila perlu (env ikut dibawa)
if [[ $EUID -ne 0 ]]; then
    exec sudo --preserve-env=LAB_IMG,LAB_MNT,LAB_MB,FORCE "$0" "$@"
fi

case "$FS" in
    ext4) command -v mkfs.ext4 >/dev/null || die "mkfs.ext4 tidak ditemukan (apt install e2fsprogs)" ;;
    f2fs) command -v mkfs.f2fs >/dev/null || die "mkfs.f2fs tidak ditemukan (apt install f2fs-tools)" ;;
    *)    die "filesystem '$FS' tidak didukung, pakai ext4 atau f2fs" ;;
esac

IMG="$(realpath -m "$IMG")"

# Cari loop device yang sudah terhubung ke image ini
find_loop() {
    losetup -nO NAME,BACK-FILE 2>/dev/null \
        | awk -v f="$IMG" '$2 == f { print $1; exit }'
}

# 1. Image disk
if [[ -f "$IMG" ]]; then
    info "image sudah ada: $IMG"
else
    info "membuat image ${SIZE_MB} MB: $IMG"
    dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
fi

# 2. Loop device
LOOP="$(find_loop || true)"
if [[ -n "$LOOP" ]]; then
    info "loop device sudah terhubung: $LOOP"
else
    LOOP="$(losetup -f --show "$IMG")"
    info "loop device baru: $LOOP"
fi

# 3. Kalau sudah ter-mount dengan fs yang benar, selesai
CUR_SRC="$(findmnt -no SOURCE "$MNT" 2>/dev/null || true)"
CUR_FS="$(findmnt -no FSTYPE "$MNT" 2>/dev/null || true)"
if [[ "$CUR_SRC" == "$LOOP" && "$CUR_FS" == "$FS" && "$FORCE" != "1" ]]; then
    info "$LOOP sudah ter-mount di $MNT sebagai $FS, tidak ada yang diubah"
else
    # Lepas mount lama di $MNT (punya loop lain atau fs lain)
    if mountpoint -q "$MNT"; then
        info "melepas mount lama di $MNT ($CUR_SRC, $CUR_FS)"
        umount "$MNT"
    fi
    # Lepas juga kalau loop ini ter-mount di tempat lain
    while read -r t; do
        [[ -n "$t" ]] && { info "melepas $LOOP dari $t"; umount "$t"; }
    done < <(findmnt -rno TARGET -S "$LOOP" 2>/dev/null || true)

    # 4. mkfs: hanya bila belum ada fs yang diminta, atau dipaksa
    HAVE_FS="$(blkid -o value -s TYPE "$LOOP" 2>/dev/null || true)"
    if [[ "$HAVE_FS" == "$FS" && "$FORCE" != "1" ]]; then
        info "$LOOP sudah berisi $FS, tidak diformat ulang"
    else
        info "memformat $LOOP sebagai $FS"
        if [[ "$FS" == ext4 ]]; then
            mkfs.ext4 -F -q "$LOOP"
        else
            mkfs.f2fs -f -q "$LOOP" >/dev/null
        fi
    fi

    # 5. Mount
    mkdir -p "$MNT"
    mount "$LOOP" "$MNT"
    info "ter-mount: $LOOP -> $MNT ($FS)"
fi

# Supaya workload bisa menulis tanpa sudo
if [[ -n "${SUDO_USER:-}" ]]; then
    chown "$SUDO_USER": "$MNT"
fi

echo
lsblk "$LOOP"
echo
info "Media B siap. Pakai: --dir $MNT --dev $(basename "$LOOP")"
