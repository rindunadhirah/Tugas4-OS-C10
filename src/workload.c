#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "../iolib.h"
#include "banner.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BYTES_PER_KB 1024ULL
#define BYTES_PER_MB (1024ULL * 1024ULL)
#define WORKLOAD_FILE "workload.dat"

typedef enum {
    MODE_SEQWRITE,
    MODE_RANDWRITE,
    MODE_SEQREAD,
    MODE_RANDREAD
} workload_mode_t;

typedef struct {
    const char *dir;
    const char *dev;
    const char *mode_name;
    size_t total_bytes;
    size_t block_bytes;
    workload_mode_t mode;
    bool do_fsync;
    bool have_dir;
    bool have_size;
    bool have_block_size;
    bool have_mode;
} options_t;

static bool mode_is_write(workload_mode_t mode)
{
    return mode == MODE_SEQWRITE || mode == MODE_RANDWRITE;
}

static bool mode_is_random(workload_mode_t mode)
{
    return mode == MODE_RANDWRITE || mode == MODE_RANDREAD;
}

static int parse_positive_size(const char *text, size_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !*text || *text == '-')
        return -1;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || end == text || *end != '\0' || parsed == 0 ||
        parsed > (unsigned long long)SIZE_MAX)
        return -1;
    *value = (size_t)parsed;
    return 0;
}

static int parse_mode(const char *text, workload_mode_t *mode)
{
    if (strcmp(text, "seqwrite") == 0)
        *mode = MODE_SEQWRITE;
    else if (strcmp(text, "randwrite") == 0)
        *mode = MODE_RANDWRITE;
    else if (strcmp(text, "seqread") == 0)
        *mode = MODE_SEQREAD;
    else if (strcmp(text, "randread") == 0)
        *mode = MODE_RANDREAD;
    else
        return -1;
    return 0;
}

static int parse_options(int argc, char **argv, options_t *opts)
{
    memset(opts, 0, sizeof *opts);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            banner_usage(stdout, argv[0]);
            return 1;
        } else if (strcmp(arg, "--fsync") == 0) {
            opts->do_fsync = true;
        } else if (strcmp(arg, "--dir") == 0 || strcmp(arg, "--size") == 0 ||
                   strcmp(arg, "--bs") == 0 || strcmp(arg, "--mode") == 0 ||
                   strcmp(arg, "--dev") == 0) {
            if (i + 1 >= argc || strncmp(argv[i + 1], "--", 2) == 0) {
                fprintf(stderr, "Opsi %s membutuhkan nilai.\n", arg);
                return -1;
            }
            const char *value = argv[++i];

            if (strcmp(arg, "--dir") == 0) {
                opts->dir = value;
                opts->have_dir = true;
            } else if (strcmp(arg, "--size") == 0) {
                size_t mb;
                if (parse_positive_size(value, &mb) < 0 ||
                    mb > SIZE_MAX / (size_t)BYTES_PER_MB ||
                    mb > (size_t)INT64_MAX / (size_t)BYTES_PER_MB) {
                    fprintf(stderr, "Nilai --size harus bilangan MB positif yang muat di file.\n");
                    return -1;
                }
                opts->total_bytes = mb * (size_t)BYTES_PER_MB;
                opts->have_size = true;
            } else if (strcmp(arg, "--bs") == 0) {
                size_t kb;
                if (parse_positive_size(value, &kb) < 0 ||
                    kb > SIZE_MAX / (size_t)BYTES_PER_KB ||
                    kb > (size_t)SSIZE_MAX / (size_t)BYTES_PER_KB) {
                    fprintf(stderr, "Nilai --bs harus bilangan KB positif yang muat dalam satu operasi I/O.\n");
                    return -1;
                }
                opts->block_bytes = kb * (size_t)BYTES_PER_KB;
                opts->have_block_size = true;
            } else if (strcmp(arg, "--mode") == 0) {
                if (parse_mode(value, &opts->mode) < 0) {
                    fprintf(stderr, "Mode tidak dikenal: %s\n", value);
                    return -1;
                }
                opts->mode_name = value;
                opts->have_mode = true;
            } else {
                opts->dev = value;
                if (strncmp(opts->dev, "/dev/", 5) == 0)
                    opts->dev += 5;
                if (!*opts->dev) {
                    fprintf(stderr, "Nama perangkat --dev tidak boleh kosong.\n");
                    return -1;
                }
            }
        } else {
            fprintf(stderr, "Opsi tidak dikenal: %s\n", arg);
            return -1;
        }
    }

    if (!opts->have_dir || !opts->have_size || !opts->have_block_size || !opts->have_mode) {
        fprintf(stderr, "Opsi --dir, --size, --bs, dan --mode wajib diberikan.\n");
        return -1;
    }
    if (opts->do_fsync && !mode_is_write(opts->mode)) {
        fprintf(stderr, "Opsi --fsync hanya berlaku untuk mode penulisan.\n");
        return -1;
    }
    return 0;
}

static uint64_t random_next(uint64_t *state)
{
    /* xorshift64* untuk memilih posisi blok tanpa ketergantungan tambahan. */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

static int transfer_exact(int fd, void *buffer, size_t bytes, off_t offset, bool writing)
{
    size_t done = 0;

    while (done < bytes) {
        ssize_t n;
        if (writing)
            n = pwrite(fd, (const char *)buffer + done, bytes - done, offset + (off_t)done);
        else
            n = pread(fd, (char *)buffer + done, bytes - done, offset + (off_t)done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int build_file_path(const char *dir, char *path, size_t path_len)
{
    struct stat st;
    int n;

    if (stat(dir, &st) < 0) {
        perror("Tidak bisa membaca direktori --dir");
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "--dir bukan sebuah direktori: %s\n", dir);
        return -1;
    }

    n = snprintf(path, path_len, "%s/%s", dir, WORKLOAD_FILE);
    if (n < 0 || (size_t)n >= path_len) {
        fprintf(stderr, "Path berkas workload terlalu panjang.\n");
        return -1;
    }
    return 0;
}

static int prepare_file(int fd, size_t required_bytes)
{
    struct stat st;

    if (fstat(fd, &st) < 0) {
        perror("fstat workload.dat");
        return -1;
    }
    if (st.st_size < 0) {
        fprintf(stderr, "Ukuran workload.dat tidak valid.\n");
        return -1;
    }
    if ((uintmax_t)st.st_size < (uintmax_t)required_bytes &&
        ftruncate(fd, (off_t)required_bytes) < 0) {
        perror("ftruncate workload.dat");
        return -1;
    }
    return 0;
}

static void print_meminfo(const char *label, const io_meminfo_t *mem, bool valid)
{
    printf("%s\n", label);
    if (valid) {
        printf("  Dirty      : %ld kB\n", mem->dirty_kb);
        printf("  Writeback  : %ld kB\n", mem->writeback_kb);
    } else {
        printf("  Dirty      : tidak tersedia\n");
        printf("  Writeback  : tidak tersedia\n");
    }
}

static void print_devstat(const char *label, const char *dev,
                          const io_devstat_t *stat, bool valid)
{
    printf("%s\n", label);
    if (!dev) {
        printf("  Statistik perangkat: tidak diminta (--dev belum diisi)\n");
    } else if (!valid) {
        printf("  %s stat: tidak tersedia\n", dev);
    } else {
        printf("  %s stat : write ios %" PRIu64 " | write merges %" PRIu64
               " | write sectors %" PRIu64 "\n",
               dev, stat->write_ios, stat->write_merges, stat->write_sectors);
    }
}

int main(int argc, char **argv)
{
    options_t opts;
    char file_path[4096];
    char env_path[512];
    int fd = -1;
    int parse_result;
    int exit_status = EXIT_FAILURE;
    size_t operations;
    unsigned char *buffer = NULL;
    uint64_t *latencies = NULL;
    io_csv_t csv = {0};
    bool csv_open = false;
    bool mem_before_valid = false, mem_after_valid = false;
    bool dev_before_valid = false, dev_after_valid = false;
    io_meminfo_t mem_before = {0}, mem_after = {0};
    io_devstat_t dev_before = {0}, dev_after = {0};
    io_stats_t stats;
    uint64_t rng_state;
    uint64_t start_ns, end_ns;

    banner_print();
    parse_result = parse_options(argc, argv, &opts);
    if (parse_result > 0)
        return EXIT_SUCCESS;
    if (parse_result < 0) {
        banner_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    operations = opts.total_bytes / opts.block_bytes;
    if (opts.total_bytes % opts.block_bytes != 0)
        operations++;
    if (operations == 0 || operations > SIZE_MAX / sizeof *latencies) {
        fprintf(stderr, "Jumlah operasi terlalu besar.\n");
        goto cleanup;
    }

    if (build_file_path(opts.dir, file_path, sizeof file_path) < 0)
        goto cleanup;
    fd = open(file_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        perror("open workload.dat");
        goto cleanup;
    }
    /* Untuk mode baca, ini juga membuat file berukuran cukup jika belum ada. */
    if (prepare_file(fd, opts.total_bytes) < 0)
        goto cleanup;

    buffer = malloc(opts.block_bytes);
    latencies = malloc(operations * sizeof *latencies);
    if (!buffer || !latencies) {
        perror("malloc workload");
        goto cleanup;
    }
    memset(buffer, 0xA5, opts.block_bytes);

    if (io_save_env(opts.dir, opts.dev, env_path, sizeof env_path) == 0)
        printf("Spesifikasi lingkungan : %s\n", env_path);
    else
        fprintf(stderr, "Peringatan: spesifikasi lingkungan gagal disimpan.\n");

    if (io_csv_open(&csv, "workload", opts.mode_name) < 0)
        goto cleanup;
    csv_open = true;

    mem_before_valid = io_read_meminfo(&mem_before) == 0;
    if (opts.dev)
        dev_before_valid = io_read_devstat(opts.dev, &dev_before) == 0;

    printf("File workload : %s\n", file_path);
    printf("Mode          : %s\n", opts.mode_name);
    printf("Ukuran        : %zu MB, blok %zu KB, %zu operasi\n",
           opts.total_bytes / (size_t)BYTES_PER_MB,
           opts.block_bytes / (size_t)BYTES_PER_KB, operations);
    print_meminfo("[Sebelum]", &mem_before, mem_before_valid);
    print_devstat("[Stat perangkat sebelum]", opts.dev, &dev_before, dev_before_valid);

    rng_state = io_now_ns() ^ (uint64_t)getpid();
    if (rng_state == 0)
        rng_state = UINT64_C(0x9e3779b97f4a7c15);

    start_ns = io_now_ns();
    for (size_t i = 0; i < operations; i++) {
        size_t block_index = mode_is_random(opts.mode)
                                 ? (size_t)(random_next(&rng_state) % operations)
                                 : i;
        size_t offset = block_index * opts.block_bytes;
        size_t bytes = opts.total_bytes - offset;
        uint64_t op_start, op_end;

        if (bytes > opts.block_bytes)
            bytes = opts.block_bytes;

        op_start = io_now_ns();
        if (transfer_exact(fd, buffer, bytes, (off_t)offset, mode_is_write(opts.mode)) < 0) {
            fprintf(stderr, "I/O gagal pada operasi %zu (%s): %s\n",
                    i, opts.mode_name, strerror(errno));
            goto cleanup;
        }
        op_end = io_now_ns();
        latencies[i] = op_end - op_start;

        if (io_csv_write(&csv, 1, opts.mode_name, i, latencies[i], bytes) < 0) {
            fprintf(stderr, "Gagal menulis data ke CSV.\n");
            goto cleanup;
        }
    }
    if (opts.do_fsync && fsync(fd) < 0) {
        perror("fsync workload.dat");
        goto cleanup;
    }
    end_ns = io_now_ns();

    mem_after_valid = io_read_meminfo(&mem_after) == 0;
    if (opts.dev)
        dev_after_valid = io_read_devstat(opts.dev, &dev_after) == 0;

    print_meminfo("[Sesudah]", &mem_after, mem_after_valid);
    print_devstat("[Stat perangkat sesudah]", opts.dev, &dev_after, dev_after_valid);
    if (opts.dev && dev_before_valid && dev_after_valid) {
        io_devstat_t diff = io_devstat_diff(&dev_before, &dev_after);
        printf("Selisih %s : write ios +%" PRIu64 " | write merges +%" PRIu64
               " | write sectors +%" PRIu64 "\n",
               opts.dev, diff.write_ios, diff.write_merges, diff.write_sectors);
    }

    if (io_compute_stats(latencies, operations, &stats) < 0) {
        fprintf(stderr, "Tidak bisa menghitung statistik latensi.\n");
        goto cleanup;
    }
    double elapsed_s = (double)(end_ns - start_ns) / 1000000000.0;
    double throughput = elapsed_s > 0.0
                            ? ((double)opts.total_bytes / (double)BYTES_PER_MB) / elapsed_s
                            : 0.0;
    printf("Waktu eksekusi : %.3f s%s\n", elapsed_s,
           opts.do_fsync ? " (termasuk fsync)" : "");
    printf("Throughput     : %.1f MB/s\n", throughput);
    printf("Latensi (ns)   : avg %.0f | min %" PRIu64 " | max %" PRIu64
           " | p50 %" PRIu64 " | p95 %" PRIu64 " | p99 %" PRIu64 "\n",
           stats.avg, stats.min, stats.max, stats.p50, stats.p95, stats.p99);
    printf("Log            : %s\n", csv.path);

    exit_status = EXIT_SUCCESS;

cleanup:
    if (csv_open)
        io_csv_close(&csv);
    if (fd >= 0)
        close(fd);
    free(latencies);
    free(buffer);
    return exit_status;
}
