# Reference Voltage Generator - GUI v2.0
#
# Native build on Linux:
#     sudo apt install build-essential libgtk-4-dev pkg-config
#     make
#
# Native build on Windows, from an MSYS2 MinGW 64-bit shell:
#     pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk4 \
#               mingw-w64-x86_64-pkgconf mingw-w64-x86_64-make
#     mingw32-make
#
#     Use mingw32-make, not the msys `make`: the latter scrubs TMP out of
#     the environment when it launches the native gcc, which then fails
#     with "Cannot create temporary file in C:\WINDOWS\".
#
# Cross-compile from Linux to Windows (quasi-msys2, see the README):
#     make CC=x86_64-w64-mingw32-gcc BIN=ReferenceVoltageGenerator.exe

CC         ?= gcc
PKG_CONFIG ?= pkg-config

# Windows needs the .exe suffix on the target so that `make` can tell
# whether the binary is up to date and `clean` can find it. $(OS) is set by
# Windows itself, and uname covers the MSYS2/Cygwin shells that scrub it.
UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(OS),Windows_NT)
    EXE := .exe
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    EXE := .exe
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    EXE := .exe
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    EXE := .exe
else
    EXE :=
endif

BIN ?= main$(EXE)

SRC_DIR := src
SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(SRCS:.c=.o)
DEPS    := $(OBJS:.o=.d)

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk4)

# -Wno-unused-parameter: GTK callback signatures are fixed, so unused
# parameters are the norm rather than a smell.
WARNINGS := -Wall -Wextra -Wno-unused-parameter

CFLAGS  += -std=c11 -O2 $(WARNINGS) $(GTK_CFLAGS) -MMD -MP
LDLIBS  += $(GTK_LIBS) -lm

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)

-include $(DEPS)
