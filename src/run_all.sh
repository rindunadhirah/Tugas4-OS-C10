#!/bin/bash
set -euo pipefail

# Konfigurasi
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
mount_dir=/mnt/lab
image="$PWD/lab_disk.img"
log_dir=../logs
timestamp=$(date +%Y%m%d-%H%M%S)
mkdir -p "$log_dir"
csv="$log_dir/C10-waf-$timestamp.csv"

# Jangan melepas mount yang bukan milik image lab
check_mount() {
    local source
    if source=$(findmnt -rn -M "$mount_dir" -o SOURCE); then
        if ! sudo losetup -j "$image" | cut -d: -f1 | grep -Fxq "$source"; then
            echo "ERROR: $mount_dir digunakan oleh perangkat lain." >&2
            exit 1
        fi
    fi
}

# Header CSV
echo 'experiment,filesystem,workload,logical_bytes,kbytes_before,kbytes_after,sectors_before,sectors_after,fs_written_bytes,block_written_bytes,fs_waf,block_waf' > "$csv"

# Mengambil io_size dari konfigurasi fio
get_io_size() {
    io_size=$(awk -F= '/^io_size=/ {print $2}' "$1")
    io_size=${io_size%m}
    io_size=$((io_size * 1024 * 1024))
}

# Menjalankan eksperimen pada satu filesystem
run_test() {
    local filesystem=$1 fio_file=$2 experiment=$3
    local virtual_disk loop kbytes_before kbytes_after sectors_before sectors_after
    local written_bytes written_sectors written_bytes_sector_level fs_waf block_waf waf
    local fio_log="$log_dir/C10-fio-$experiment-$filesystem-$timestamp.txt"

    # Format & mount menggunakan skrip kelompok
    check_mount
    sudo env LAB_IMG="$image" LAB_MNT="$mount_dir" LAB_MB=64 FORCE=1 bash ./setup_lab.sh "$filesystem"

    # Mencari loop device
    virtual_disk=$(sudo losetup -j "$image" | awk -F: '{print $1}')
    if [[ ! "$virtual_disk" =~ ^/dev/loop[0-9]+$ ]] || [[ $(findmnt -rn -M "$mount_dir" -o SOURCE) != "$virtual_disk" ]] || [[ $(findmnt -rn -M "$mount_dir" -o FSTYPE) != "$filesystem" ]]; then
        echo "ERROR: loop device atau mount tidak sesuai." >&2
        exit 1
    fi
    loop=${virtual_disk#/dev/}

    # Sebelum
    sync
    kbytes_before=$(cat "/sys/fs/$filesystem/$loop/lifetime_write_kbytes")
    sectors_before=$(awk '{print $7}' "/sys/block/$loop/stat")

    # Jalankan eksperimen dan simpan keluaran fio
    fio "$fio_file" --directory="$mount_dir" --output="$fio_log" --output-format=normal
    sync

    # Setelah
    kbytes_after=$(cat "/sys/fs/$filesystem/$loop/lifetime_write_kbytes")
    sectors_after=$(awk '{print $7}' "/sys/block/$loop/stat")

    # Unmount
    sudo umount "$mount_dir"

    # Kalkulasi
    written_bytes=$(( (kbytes_after-kbytes_before) * 1024 ))
    written_sectors=$((sectors_after-sectors_before))
    written_bytes_sector_level=$((written_sectors * 512))

    fs_waf=$(bc <<<"scale=10; $written_bytes / $io_size")
    block_waf=$(bc <<<"scale=10; $written_bytes_sector_level / $io_size")

    if [[ "$experiment" == A ]]; then
        waf=$fs_waf
    else
        waf=$block_waf
    fi

    echo "Bytes yang tertulis $filesystem: $written_bytes"
    echo "Sektor yang tertulis $filesystem: $written_sectors"
    echo "WAF $filesystem: $waf"

    # Simpan kedua level WAF untuk analisis laporan
    echo "$experiment,$filesystem,$fio_file,$io_size,$kbytes_before,$kbytes_after,$sectors_before,$sectors_after,$written_bytes,$written_bytes_sector_level,$fs_waf,$block_waf" >> "$csv"
}

# EKSPERIMEN A
echo "EKSPERIMEN A"
get_io_size rand_write.fio
echo "Byte Logis: $io_size"
run_test ext4 rand_write.fio A
run_test f2fs rand_write.fio A

# EKSPERIMEN B
echo "EKSPERIMEN B"
get_io_size seq_write.fio
echo "Byte Logis: $io_size"
run_test ext4 seq_write.fio B
run_test f2fs seq_write.fio B

echo "Hasil disimpan di $csv"
