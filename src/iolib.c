/*
 * iolib.c - Implementasi pustaka bersama Tugas 4. Lihat iolib.h.
 */
#include "iolib.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ================= 1. Timer ================= */

uint64_t io_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ================= 2. Metrik OS ================= */

/* Format baris /proc/meminfo: "Dirty:              412 kB" */
int io_read_meminfo(io_meminfo_t *m)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return -1;

    char line[256];
    int found = 0;
    m->dirty_kb = m->writeback_kb = -1;

    while (fgets(line, sizeof line, fp) && found < 2) {
        long v;
        /* "Writeback:" harus dicocokkan persis, karena ada juga
         * baris "WritebackTmp:" */
        if (sscanf(line, "Dirty: %ld", &v) == 1) {
            m->dirty_kb = v;
            found++;
        } else if (sscanf(line, "Writeback: %ld", &v) == 1) {
            m->writeback_kb = v;
            found++;
        }
    }
    fclose(fp);
    return found == 2 ? 0 : -1;
}

/* Kolom /sys/block/<dev>/stat (Documentation/block/stat.rst):
 *   1 read ios   2 read merges   3 read sectors   4 read ticks
 *   5 write ios  6 write merges  7 write sectors  8 write ticks  ...
 * Satuan sektor selalu 512 byte. */
int io_read_devstat(const char *dev, io_devstat_t *s)
{
    if (!dev || !*dev)
        return -1;
    /* /sys/class/block/<dev>/stat = file yang sama dengan
     * /sys/block/<dev>/stat, tapi juga ada untuk partisi (sda2) */
    char path[256];
    snprintf(path, sizeof path, "/sys/class/block/%s/stat", dev);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    /* Lewati 4 kolom baca (%*), ambil kolom 5-7 */
    int n = fscanf(fp, "%*u %*u %*u %*u %" SCNu64 " %" SCNu64 " %" SCNu64,
                   &s->write_ios, &s->write_merges, &s->write_sectors);
    fclose(fp);
    return n == 3 ? 0 : -1;
}

io_devstat_t io_devstat_diff(const io_devstat_t *b, const io_devstat_t *a)
{
    io_devstat_t d;
    d.write_ios     = a->write_ios     - b->write_ios;
    d.write_merges  = a->write_merges  - b->write_merges;
    d.write_sectors = a->write_sectors - b->write_sectors;
    return d;
}

/* ================= 3. Statistik ================= */

static int cmp_u64(const void *x, const void *y)
{
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return (a > b) - (a < b);
}

/* Persentil metode nearest-rank: elemen ke-ceil(p/100 * n) (1-based)
 * dari data terurut. */
static uint64_t percentile(const uint64_t *sorted, size_t n, int p)
{
    size_t rank = ((size_t)p * n + 99) / 100;   /* ceil tanpa float */
    if (rank == 0)
        rank = 1;
    return sorted[rank - 1];
}

int io_compute_stats(const uint64_t *lat, size_t n, io_stats_t *out)
{
    memset(out, 0, sizeof *out);
    if (n == 0)
        return -1;

    uint64_t *s = malloc(n * sizeof *s);
    if (!s)
        return -1;
    memcpy(s, lat, n * sizeof *s);
    qsort(s, n, sizeof *s, cmp_u64);

    double sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += (double)s[i];

    out->n   = n;
    out->avg = sum / (double)n;
    out->min = s[0];
    out->max = s[n - 1];
    out->p50 = percentile(s, n, 50);
    out->p95 = percentile(s, n, 95);
    out->p99 = percentile(s, n, 99);

    free(s);
    return 0;
}

/* ================= 4. Log CSV ================= */

/* Folder log: ../logs kalau program dijalankan dari folder src/
 * (folder logs/ ada di root repo), selain itu logs. */
const char *io_log_dir(void)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) {
        const char *base = strrchr(cwd, '/');
        base = base ? base + 1 : cwd;
        if (strcmp(base, "src") == 0)
            return "../logs";
    }
    return "logs";
}

/* Timestamp sesi "YYYYMMDD-HHMMSS", ditetapkan sekali supaya CSV dan
 * file env dari satu sesi memakai timestamp yang sama. */
static const char *session_ts(void)
{
    static char ts[32];
    if (!ts[0]) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", &tm);
    }
    return ts;
}

static int ensure_log_dir(void)
{
    if (mkdir(io_log_dir(), 0755) == 0 || errno == EEXIST)
        return 0;
    perror("iolib: mkdir log dir");
    return -1;
}

int io_csv_open(io_csv_t *c, const char *program, const char *scenario)
{
    c->fp = NULL;
    if (ensure_log_dir() < 0)
        return -1;

    snprintf(c->path, sizeof c->path, "%s/%s-%s-%s-%s.csv",
             io_log_dir(), KELOMPOK, program, scenario, session_ts());

    c->fp = fopen(c->path, "w");
    if (!c->fp) {
        perror("iolib: fopen csv");
        return -1;
    }
    fprintf(c->fp, "run_id,scenario,op_index,latency_ns,bytes\n");
    return 0;
}

int io_csv_write(io_csv_t *c, int run_id, const char *scenario,
                 size_t op_index, uint64_t latency_ns, size_t bytes)
{
    if (!c->fp)
        return -1;
    return fprintf(c->fp, "%d,%s,%zu,%" PRIu64 ",%zu\n",
                   run_id, scenario, op_index, latency_ns, bytes) < 0 ? -1 : 0;
}

void io_csv_close(io_csv_t *c)
{
    if (c->fp) {
        fclose(c->fp);
        c->fp = NULL;
    }
}

/* ================= 5. Spesifikasi lingkungan ================= */

/* Baca baris pertama sebuah file ke buf (tanpa newline). */
static int read_first_line(const char *path, char *buf, size_t len)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    if (!fgets(buf, (int)len, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* Tulis "label : isi file", atau "label : -" bila file tidak ada. */
static void env_line(FILE *out, const char *label, const char *path)
{
    char v[256];
    if (read_first_line(path, v, sizeof v) == 0)
        fprintf(out, "%s : %s\n", label, v);
    else
        fprintf(out, "%s : -\n", label);
}

/* Jalankan perintah shell dan salin output-nya ke out (indentasi 2 spasi). */
static void env_cmd(FILE *out, const char *cmd)
{
    FILE *p = popen(cmd, "r");
    if (!p) {
        fprintf(out, "  (gagal menjalankan: %s)\n", cmd);
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, p))
        fprintf(out, "  %s", line);
    pclose(p);
}

/* Total RAM dari /proc/meminfo (baris MemTotal, dalam kB). */
static double ram_gb(void)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return 0;
    char line[256];
    long kb = 0;
    while (fgets(line, sizeof line, fp))
        if (sscanf(line, "MemTotal: %ld", &kb) == 1)
            break;
    fclose(fp);
    return kb / 1024.0 / 1024.0;
}

/* Read-ahead ada di <disk>/queue/read_ahead_kb. Untuk partisi (sda2),
 * folder queue ada di disk induknya, yaitu satu tingkat di atas. */
static void env_readahead(FILE *out, const char *dev)
{
    char path[256], v[64];
    snprintf(path, sizeof path, "/sys/class/block/%s/queue/read_ahead_kb", dev);
    if (read_first_line(path, v, sizeof v) < 0) {
        snprintf(path, sizeof path, "/sys/class/block/%s/../queue/read_ahead_kb", dev);
        if (read_first_line(path, v, sizeof v) < 0)
            strcpy(v, "-");
    }
    fprintf(out, "Read-ahead : %s kB\n", v);
}

int io_save_env(const char *media_dir, const char *dev,
                char *out_path, size_t out_len)
{
    if (ensure_log_dir() < 0)
        return -1;

    char path[512];
    snprintf(path, sizeof path, "%s/%s-env-%s.txt",
             io_log_dir(), KELOMPOK, session_ts());

    FILE *out = fopen(path, "w");
    if (!out) {
        perror("iolib: fopen env");
        return -1;
    }

    struct utsname u;
    if (uname(&u) == 0)
        fprintf(out, "Kernel : %s\n", u.release);
    fprintf(out, "RAM : %.1f GB\n", ram_gb());

    /* Media : folder (sumber fstype ukuran) [dev=...]
     * findmnt -r (raw) memisahkan kolom dengan satu spasi */
    if (media_dir) {
        char cmd[600], info[256] = "?";
        snprintf(cmd, sizeof cmd,
                 "findmnt -rno SOURCE,FSTYPE,SIZE -T '%s' 2>/dev/null", media_dir);
        FILE *p = popen(cmd, "r");
        if (p) {
            if (fgets(info, sizeof info, p))
                info[strcspn(info, "\n")] = '\0';
            pclose(p);
        }
        fprintf(out, "Media : %s (%s) [dev=%s]\n", media_dir, info, dev ? dev : "-");
    }

    env_line(out, "vm.dirty_ratio", "/proc/sys/vm/dirty_ratio");
    env_line(out, "vm.dirty_background_ratio", "/proc/sys/vm/dirty_background_ratio");

    if (dev && *dev) {
        env_readahead(out, dev);
    }

    fprintf(out, "lsblk :\n");
    env_cmd(out, "lsblk 2>&1");

    fclose(out);
    if (out_path && out_len)
        snprintf(out_path, out_len, "%s", path);
    return 0;
}
