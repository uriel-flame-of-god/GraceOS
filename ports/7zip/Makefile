# ============================
# GraceOS 7zip (port)
# ============================

NAME := 7zip
ROOT := ../..
C_DIR := C
UTIL_DIR := C/Util/7z
BUILD_ROOT := $(ROOT)/build
OUT_DIR := $(BUILD_ROOT)/ports/7zip
OBJ_DIR := $(OUT_DIR)/obj
LIB := $(OUT_DIR)/7zdec.a

CC := clang --target=x86_64-elf
AR := llvm-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -m64 -O2 -Wall -Wextra \
	-nostdlib -nostdinc

DEFS := -DGRACEOS -DGRACEOS_BFS -DZ7_EXTRACT_ONLY -DZ7_PPMD_SUPPORT -DZ7_ST

INCLUDES := -I$(C_DIR) -I$(UTIL_DIR) -Igrace \
	-I$(ROOT)/lib/libc -I$(ROOT)/lib/libtranslate -I$(ROOT)/include -I$(ROOT)/lib/libgrace \
	-I$(ROOT)/ports/libsodium/src/libsodium/include \
	-I$(ROOT)/ports/libsodium/grace

OUT_DIR_WIN := $(subst /,\,$(OUT_DIR))
OBJ_DIR_WIN := $(subst /,\,$(OBJ_DIR))

# Minimal C-only decoder sources (see grace/BUILD.MD)
SOURCES := \
	C/Util/7z/7zMain.c \
	C/7zAlloc.c \
	C/7zArcIn.c \
	C/7zBuf.c \
	C/7zBuf2.c \
	C/7zCrc.c \
	C/7zDec.c \
	C/7zFile.c \
	C/7zStream.c \
	C/Aes.c \
	C/Bcj2.c \
	C/Bra.c \
	C/Bra86.c \
	C/BraIA64.c \
	C/CpuArch.c \
	C/Delta.c \
	C/Lzma2Dec.c \
	C/LzmaDec.c \
	C/Ppmd7.c \
	C/Ppmd7Dec.c \
	grace/crypto_gate.c \
	grace/sha256_sodium.c

OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)

all: $(LIB)

$(LIB): $(OBJECTS)
	@if not exist "$(OUT_DIR_WIN)" mkdir "$(OUT_DIR_WIN)"
	$(AR) rcs $@ $(OBJECTS)

$(OBJ_DIR)/%.o: %.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

clean:
	@if exist "$(OUT_DIR_WIN)" rmdir /s /q "$(OUT_DIR_WIN)"

.PHONY: all clean
