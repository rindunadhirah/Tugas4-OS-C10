#ifndef BANNER_H
#define BANNER_H

#include <stdio.h>

/* Cetak identitas kelompok dan petunjuk pemakaian workload. */
void banner_print(void);
void banner_usage(FILE *out, const char *program);

#endif /* BANNER_H */
