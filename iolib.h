#ifndef IOLIB_H
#define IOLIB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Nomor kelompok, diisi Makefile lewat -DKELOMPOK=\"10\" */
#ifndef KELOMPOK
#define KELOMPOK "00"
#endif

/* ---------- 1. Timer ---------- */

/* Waktu sekarang dalam nanodetik (CLOCK_MONOTONIC, tidak terpengaruh
 * perubahan jam sistem). Pakai selisih dua panggilan untuk latensi. */
uint64_t io_now_ns(void);

/* ---------- 2. Metrik OS ---------- */

typedef struct {
    long dirty_kb;      /* baris "Dirty:" di /proc/meminfo     */
    long writeback_kb;  /* baris "Writeback:" di /proc/meminfo */
} io_meminfo_t;

/* Return 0 kalau berhasil, -1 kalau gagal membaca. */
int io_read_meminfo(io_meminfo_t *m);

typedef struct {
    uint64_t write_ios, write_merges, write_sectors;
} io_devstat_t;

/* dev = nama perangkat tanpa "/dev/", misalnya "loop0" atau "sda2".
 * Return 0 kalau berhasil, -1 kalau gagal. */
int io_read_devstat(const char *dev, io_devstat_t *s);

/* Selisih after - before untuk setiap kolom. */
io_devstat_t io_devstat_diff(const io_devstat_t *before, const io_devstat_t *after);

/* ---------- 3. Statistik ---------- */

typedef struct {
    size_t   n;
    double   avg;
    uint64_t min, max;
    uint64_t p50, p95, p99;
} io_stats_t;

/* Hitung statistik dari n data latensi (ns). Array asli tidak diubah
 * (diurutkan di salinan). Return 0 kalau berhasil, -1 kalau n == 0
 * atau alokasi memori gagal. */
int io_compute_stats(const uint64_t *lat, size_t n, io_stats_t *out);

/* ---------- 4. Log CSV ---------- */

/* Folder log: ../logs kalau program dijalankan dari folder src/,
 * selain itu logs. */
const char *io_log_dir(void);

typedef struct {
    FILE *fp;
    char  path[512];
} io_csv_t;

/* Buka logs/XX-[program]-[scenario]-[timestamp].csv dan tulis header
 * "run_id,scenario,op_index,latency_ns,bytes". Folder dibuat bila belum ada.
 * Return 0 kalau berhasil, -1 kalau gagal. */
int  io_csv_open(io_csv_t *c, const char *program, const char *scenario);

/* Tulis satu baris. */
int  io_csv_write(io_csv_t *c, int run_id, const char *scenario,
                  size_t op_index, uint64_t latency_ns, size_t bytes);

void io_csv_close(io_csv_t *c);

/* ---------- 5. Spesifikasi lingkungan ---------- */

/* Simpan logs/XX-env-[timestamp].txt berisi kernel, RAM, media uji,
 * vm.dirty_ratio, vm.dirty_background_ratio, read-ahead, dan lsblk.
 * media_dir = folder uji (--dir), dev = perangkat (--dev), boleh NULL.
 * Kalau out_path tidak NULL, path file yang dibuat disalin ke situ.
 * Return 0 kalau berhasil, -1 kalau gagal. */
int io_save_env(const char *media_dir, const char *dev,
                char *out_path, size_t out_len);

#endif /* IOLIB_H */