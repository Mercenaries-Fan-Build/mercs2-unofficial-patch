# Builds unofficial_patch.asi — every T3 fix in one plugin, one source file per bug.
#
# Prerequisites (32-bit MinGW; the game is a 32-bit process):
#   macOS:   brew install mingw-w64
#   Ubuntu:  apt install gcc-mingw-w64-i686
#   Windows: MSYS2 + mingw-w64-i686-gcc
#
# The SDK is a submodule. From a fresh clone:
#   git submodule update --init --recursive
#   make -C ../sdk build        # produces m2-sdk.dll + the import library this links against

include ../sdk/sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
TARGET   = ../src/unofficial_patch.asi
SRCS     = $(wildcard src/*.c)

CFLAGS  = -O2 -Wall -Wextra -shared $(M2_CFLAGS)
LDFLAGS = $(M2_LDFLAGS) -lkernel32 -luser32

.PHONY: all verify clean help

all: $(TARGET)

$(TARGET): $(SRCS) $(M2_IMPLIB)
	$(CC_MINGW) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
	@echo "built $(TARGET) ($$(wc -c < $(TARGET) | tr -d ' ') bytes)"

# The SDK is not built by this Makefile — say so by name rather than failing inside ld.
$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make -C ../sdk build' first" >&2
	@exit 1

# Compiling proves the headers agree. Only the import table proves this .asi will bind to the shared
# layer at load time instead of failing on a player's machine with 0x8007007E.
verify: $(TARGET)
	@i686-w64-mingw32-objdump -p $(TARGET) | grep -q "DLL Name: m2-sdk.dll" \
		|| { echo "FAIL: $(TARGET) does not import m2-sdk.dll" >&2; exit 1; }
	@file $(TARGET) | grep -q "PE32 .*80386" \
		|| { echo "FAIL: $(TARGET) is not a 32-bit PE" >&2; exit 1; }
	@echo "OK: $(TARGET) is a 32-bit PE importing m2-sdk.dll"

clean:
	rm -f $(TARGET)

help:
	@echo "make        — build $(TARGET)"
	@echo "make verify — assert it is a 32-bit PE that imports m2-sdk.dll"
