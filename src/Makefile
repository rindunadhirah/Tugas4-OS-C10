# Target:
#   make all    -> kompilasi semua program ke $(BUILD)/
#   make run    -> jalankan workload di Media A
#   make run-b  -> jalankan workload di Media B (/mnt/lab), untuk demo
#   make clean  -> hapus hasil kompilasi

# Identitas kelompok 
XX       ?= 10

# Compiler 
CC       ?= gcc
CFLAGS   ?= -O2
CFLAGS   += -Wall -Wextra -std=gnu11 -DKELOMPOK=\"$(XX)\"
LDFLAGS  ?=
LDLIBS   ?=

BUILD    := build

# Modul bersama. iolib saat ini berada di direktori utama repo, sedangkan
# banner dan workload berada di src/.
COMMON_OBJ := $(BUILD)/iolib.o $(BUILD)/banner.o
HEADERS    := ../iolib.h banner.h

# Program utama. Varian 6 hanya memakai skrip + fio, jadi cukup workload.
# Kalau nanti ada program C lain, tambahkan namanya di sini.
PROGRAMS := workload
BINS     := $(PROGRAMS:%=$(BUILD)/%)

# Parameter make run 
DIR_A  ?= ../work_a
DIR_B  ?= /mnt/lab
SIZE   ?= 256
SIZE_B ?= 32
BS     ?= 4
MODE   ?= seqwrite
# Perangkat blok di bawah DIR_A (mis. sda2), dideteksi otomatis
DEV_A  ?= $(shell d=$(DIR_A); mkdir -p $$d 2>/dev/null; \
            basename "$$(findmnt -no SOURCE -T $$d 2>/dev/null)" 2>/dev/null)
# Loop device di bawah /mnt/lab (mis. loop0), dideteksi otomatis
DEV_B  ?= $(shell basename "$$(findmnt -no SOURCE $(DIR_B) 2>/dev/null)" 2>/dev/null)

.PHONY: all run run-b clean

# Simpan file .o supaya tidak dikompilasi ulang setiap kali
.SECONDARY:

all: $(BINS)

# Direktori build dibuat oleh Makefile (order-only prerequisite)
$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/iolib.o: ../iolib.c ../iolib.h | $(BUILD)
	$(CC) $(CFLAGS) -c ../iolib.c -o $@

$(BUILD)/%: $(BUILD)/%.o $(COMMON_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

run: $(BUILD)/workload
	@mkdir -p logs $(DIR_A)
	./$(BUILD)/workload --dir $(DIR_A) --size $(SIZE) --bs $(BS) \
		--mode $(MODE) $(if $(DEV_A),--dev $(DEV_A))

run-b: $(BUILD)/workload
	@mountpoint -q $(DIR_B) || { echo "Media B belum di-mount. Jalankan: sudo ./setup_lab.sh"; exit 1; }
	@mkdir -p logs
	./$(BUILD)/workload --dir $(DIR_B) --size $(SIZE_B) --bs $(BS) \
		--mode $(MODE) $(if $(DEV_B),--dev $(DEV_B))

clean:
	rm -rf $(BUILD)
