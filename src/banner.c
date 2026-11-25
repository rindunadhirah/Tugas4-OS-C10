#include "banner.h"

#include "../iolib.h"

void banner_print(void)
{
    printf("=== Kelompok %s | Tugas 4 - Filesystem dan I/O ===\n", KELOMPOK);
}

void banner_usage(FILE *out, const char *program)
{
    fprintf(out,
            "Pemakaian: %s --dir DIREKTORI --size MB --bs KB --mode MODE [opsi]\n"
            "\n"
            "Mode yang tersedia:\n"
            "  seqwrite   Tulis berurutan\n"
            "  randwrite  Tulis pada blok acak\n"
            "  seqread    Baca berurutan\n"
            "  randread   Baca pada blok acak\n"
            "\n"
            "Opsi:\n"
            "  --fsync    Panggil fsync() sekali setelah seluruh penulisan\n"
            "  --dev DEV  Nama perangkat untuk membaca /sys/block/DEV/stat\n"
            "  --help     Tampilkan petunjuk ini\n"
            "\n"
            "Contoh: %s --dir ../work_a --size 256 --bs 4 --mode seqwrite --dev sda2\n",
            program, program);
}
