# Native port build for Snowboard Kids 2 (N64 decomp -> PowerPC Mac OS X).
#
#   port/build-ppc.sh -j8          cross-build in Docker (the normal way)
#   make -C port list              show the game sources the port compiles
#
# Prerequisites: the N64 matching build has run once (./build-mac.sh); it
# produces snowboardkids2.map (mined for asset ROM offsets and the pinned data
# layout), the splat linker script snowboardkids2.ld (which tells us exactly
# which C files are game code) and the generated headers under build/include.

TARGET ?= ppc-darwin
ROOT   := ..
BUILD  := build-$(TARGET)
GEN    := $(BUILD)/gen

# ---- toolchain -----------------------------------------------------------------
ifeq ($(TARGET),ppc-darwin)
  CROSS    := /usr/local/bin/powerpc-apple-darwin8-
  SYSROOT  := /usr/local/MacOSX10.4u.sdk
  TARGET_CFLAGS  := -isysroot $(SYSROOT) -mmacosx-version-min=10.4 -malign-natural -mone-byte-bool
  # Link the image at 0x10000000 so native data pointers can never be mistaken
  # for N64 physical/segmented addresses (< 0x10000000) inside display lists.
  TARGET_LDFLAGS := -isysroot $(SYSROOT) -mmacosx-version-min=10.4 -Wl,-pagezero_size,0x10000000 \
                    -framework OpenGL -framework Cocoa -framework Carbon -framework IOKit \
                    -framework CoreAudio -framework AudioUnit -framework AudioToolbox \
                    -framework CoreFoundation -framework ForceFeedback -liconv -static-libgcc \
                    -Wl,-undefined,dynamic_lookup  # IOHIDManager (10.5) symbols are not in the 10.4u SDK stubs
  SYM_PREFIX := _
  CTX_ASM    := src/ultra/sbk_ctx_ppc_darwin.s
  EXE        := $(BUILD)/snowboardkids2
else
  $(error unknown TARGET $(TARGET))
endif

CC      := $(CROSS)gcc
AS      := $(CROSS)as
PYTHON  ?= python3
SDL2_PREFIX ?= /work/sdl2

# ---- game sources: exactly the objects the N64 linker script links --------------
LD_SCRIPT  := $(ROOT)/snowboardkids2.ld
N64_MAP    := $(ROOT)/snowboardkids2.map
N64_OBJS   := $(shell grep -oE 'build/src/[^ ]+\.o' $(LD_SCRIPT) | sort -u)
N64_C      := $(patsubst build/%.o,%.c,$(N64_OBJS))
# Everything under src/ except the RSP blobs, the MIPS entrypoint and the pure
# BSS buffer file (buffers.c holds the heap, RSP buffers and framebuffers: it
# lives in the emulated RDRAM like every other pinned global).
GAME_C     := $(filter-out src/rsp/% src/entrypoint/% src/core/buffers.c,$(N64_C))
# ...plus the pure-C parts of libultra the port keeps: the audio synthesizer,
# the gu math library and the Controller Pak file system. Upstream links these
# out of lib/ultralib as a prebuilt archive; the port compiles them itself.
ULTRA_SRC  := lib/ultralib/src
# Exactly the archive members the N64 link pulls in, mapped back to their
# sources: everything else in libultra (the OS, the PI/SI/VI/AI drivers, libc)
# is what port/src/ultra replaces. Assembly members (sqrtf, libm_vals) have no
# .c and are left to the host's libm.
ULTRA_MEMBERS := $(sort $(patsubst %.o,%,$(shell grep -oE 'build/lib/libgultra_rom\.a:[A-Za-z0-9_]+\.o' $(LD_SCRIPT) | sed 's/.*://' | sort -u)))
ULTRA_C    := $(patsubst $(ROOT)/%,%,$(wildcard $(foreach m,$(ULTRA_MEMBERS), \
                  $(ROOT)/$(ULTRA_SRC)/audio/$(m).c $(ROOT)/$(ULTRA_SRC)/gu/$(m).c)))
# osViSetMode indexes osViModeTable, so the whole VI mode table comes along.
ULTRA_C    += $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/$(ULTRA_SRC)/vimodes/*.c)) $(ULTRA_SRC)/io/vitbl.c
# ...plus the Controller Pak file system, which the port drives over a 32 KB
# image (pfsisplug/pfsgetstatus talk to the PIF; port/src/ultra/os_pfs.c has them).
ULTRA_C    += $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/$(ULTRA_SRC)/io/pfs*.c)) $(ULTRA_SRC)/io/contpfs.c
ULTRA_C    := $(filter-out %/pfsisplug.c %/pfsgetstatus.c,$(ULTRA_C))
# ...and the one libmus member the game links (the rest of MusPlayer is
# decompiled into src/race/player.c).
LIBMUS_C   := lib/libmus/src/player_fx.c
# Linker scripts whose assignments are address arithmetic (asset/segment addresses,
# aliases into pinned data).
ABS_LD := $(wildcard $(ROOT)/linker_scripts/*.ld) $(ROOT)/undefined_syms_auto.txt
ABS_LD := $(filter-out %/hardware_regs.ld %/memory_layout_assertions.ld,$(ABS_LD))
# libultra globals the port defines itself (os_misc.c) must not become absolutes.
# The RSP microcode blobs (src/rsp/*.s) are not compiled: the port interprets
# the display and audio command lists instead. Only the symbols' values matter
# -- the game subtracts them to size the boot ucode and picks a ucode by
# address -- so they are emitted as absolutes at their N64 addresses.
RSP_SYMS := rspbootTextStart rspbootTextEnd aspMainTextStart aspMainTextEnd \
            aspMainDataStart aspMainDataEnd \
            gspF3DEX2_fifoTextStart gspF3DEX2_fifoTextEnd \
            gspF3DEX2_fifoDataStart gspF3DEX2_fifoDataEnd \
            gspS2DEX_fifoTextStart gspS2DEX_fifoTextEnd \
            gspS2DEX_fifoDataStart gspS2DEX_fifoDataEnd
PORT_DEFINED := osTvType osRomBase osResetType osAppNMIBuffer osMemSize osRomType osClockRate
# The port's own platform layer.
PORT_C     := $(wildcard src/*.c src/ultra/*.c src/gfx/*.c src/audio/*.c src/platform/*.c src/debug/*.c src/ui/*.c)
PORT_ASM   := $(CTX_ASM)

# ---- overlays (see tools/gen_overlays.py) ---------------------------------------
OV_STAMP   := $(BUILD)/overlays.stamp
OV_THUNKS  := $(BUILD)/overlay_thunks.s
OV_TABLE   := $(BUILD)/overlay_table.c
OV_RENAMES := $(BUILD)/overlay_renames.txt
OV_OBJS    := $(BUILD)/overlay_thunks.o $(BUILD)/overlay_table.o

GAME_OBJS  := $(patsubst %.c,$(BUILD)/game/%.o,$(GAME_C) $(ULTRA_C) $(LIBMUS_C))
PORT_OBJS  := $(patsubst %.c,$(BUILD)/port/%.o,$(PORT_C)) $(patsubst %.s,$(BUILD)/port/%.o,$(PORT_ASM))
ROM_SYMS   := $(BUILD)/rom_syms.o

# ---- flags -----------------------------------------------------------------------
# The sequel is built with KMC GCC 2.7.2, not IDO; the defines below mirror the
# upstream Makefile's MACROS plus what the ultralib headers want.
GAME_DEFS  := -DPORT -DLANGUAGE_C -D_LANGUAGE_C -D_MIPS_SZLONG=32 -D__USE_ISOC99 -DNDEBUG -D_FINALROM \
              -DF3DEX_GBI_2 -DBUILD_VERSION=VERSION_J -DMIPSEB -D_MIPS_SZINT=32 -D_MIPS_SZPTR=32 \
              -D_MIPS_ISA_MIPS2=2 -D_MIPS_ISA=_MIPS_ISA_MIPS2 -D_ABIO32=1 -D_MIPS_SIM=_ABIO32 \
              -D_MIPS_FPSET=16 -DCOMPILING_LIBULTRA -DCC_CHECK=1
# lib/ultralib/include/PR/gbi.h and lib/f3dex2/PR/gbi.h are byte-identical and
# both honour F3DEX_GBI_2, so gfx_pc.c's <PR/gbi.h> and the game's own "gbi.h"
# agree; lib/f3dex2/PR is NOT put on the <PR/...> path, because its rcp.h and
# ultratypes.h are older than ultralib's and would shadow them.
GAME_INC   := -I$(GEN)/src -I$(GEN) -I$(ROOT) -I$(ROOT)/include \
              -I$(ROOT)/lib/ultralib/include -I$(ROOT)/lib/ultralib/include/PR \
              -I$(ROOT)/lib/libmus/include/PR -I$(ROOT)/lib/libmus/src -I$(ROOT)/lib/f3dex2/PR \
              -I$(ROOT)/src -I$(ROOT)/assets/courses -I$(ROOT)/assets/modelpayload -I$(ROOT)/build/include
GAME_CFLAGS := -std=gnu89 -O2 -fno-builtin -fno-strict-aliasing -fwrapv -fno-common -funsigned-char \
               -Wno-unknown-pragmas -Wno-multichar -Wno-pointer-sign -w \
               -include src/port_override.h $(GAME_DEFS) $(GAME_INC) $(TARGET_CFLAGS)
PORT_CFLAGS := -std=gnu99 -O2 -g -fno-strict-aliasing -Wall -Wno-unused-parameter \
               -DPORT -D_LANGUAGE_C -DF3DEX_GBI_2 $(GAME_INC) -Isrc -I$(SDL2_PREFIX)/include \
               $(TARGET_CFLAGS)
LDFLAGS := $(TARGET_LDFLAGS) $(SDL2_PREFIX)/lib/libSDL2.a -lm

# The sequel keeps no initialised data in hand-written assembly (asm/header.s is
# the ROM header), so there are no translated-asm twins.
DATA_OBJS :=

# ---- pinned globals (see tools/gen_pins.py) ------------------------------------
# Overlays share VRAM: .race/.credits/.cutscene all live at 0x800B00C0 and every
# level at 0x800BB2B0, so their globals cannot be pinned (whose would win?) and
# stay native. The resident image, the .rand overlay (an address of its own) and
# the buffers BSS at 0x8016A000 are pinned as usual.
PIN_SKIP   := 0x800B00C0:0x8016A000
PINS_STAMP := $(BUILD)/pins.stamp
PINS_S     := $(BUILD)/pins.s
PINS_TXT   := $(BUILD)/pins.txt
PIN_TABLE  := $(BUILD)/pin_table.c
OBJ_LIST   := $(BUILD)/n64objs.txt
N64_NM     := $(BUILD)/n64syms.txt
PIN_OBJS   := $(BUILD)/pins.o $(BUILD)/pin_table.o

# ---- rules ------------------------------------------------------------------------
all: $(EXE)

.SECONDARY:

# The game's entry point is called main(); keep it out of the host's way.
$(BUILD)/game/src/core/main.o: GAME_CFLAGS += -Dmain=sbk_game_main

# port_aliases.c wraps game functions, so it is compiled as game code.
$(BUILD)/port/src/port_aliases.o: PORT_CFLAGS = $(GAME_CFLAGS)

list:
	@echo "GAME_C ($(words $(GAME_C))):"; printf '  %s\n' $(GAME_C)
	@echo "ULTRA_C ($(words $(ULTRA_C))):"; printf '  %s\n' $(ULTRA_C)
	@echo "LIBMUS_C ($(words $(LIBMUS_C))):"; printf '  %s\n' $(LIBMUS_C)
	@echo "PORT_C ($(words $(PORT_C))):"; printf '  %s\n' $(PORT_C)

$(OBJ_LIST): Makefile
	@mkdir -p $(dir $@)
	@printf '%s\n' $(patsubst %.c,build/%.o,$(GAME_C)) > $@

$(PINS_STAMP): $(N64_MAP) $(OBJ_LIST) tools/gen_pins.py
	$(PYTHON) tools/gen_pins.py $< --objs $(OBJ_LIST) --native-txt $(BUILD)/native_syms.txt $(if $(wildcard $(N64_NM)),--nm $(N64_NM)) $(foreach r,$(PIN_SKIP),--pin-skip $(r)) --prefix '$(SYM_PREFIX)' --pins-s $(PINS_S) --pins-txt $(PINS_TXT)
	@touch $@

$(PINS_S) $(PINS_TXT): $(PINS_STAMP)

# Included fragments (.inc / .inc.c under src/) are pulled in by the main file's
# #include; mirror them too so the include finds the filtered copy.
# src/core/main.c #includes src/data/shared_model_part_display_lists.c, so the
# mirror has to shadow the whole src tree (-I$(GEN)/src comes first in GAME_INC)
# or the include would pull in the unfiltered original and define the pinned
# globals a second time.
GEN_FRAGMENTS := $(patsubst $(ROOT)/%,$(GEN)/%,$(shell find $(ROOT)/src -name '*.c' 2>/dev/null))

# Which twins exist can only be known once every game source has been mirrored:
# a pinned global need not have a C definition to rename (some are
# `__asm__(".globl X\nX = base + N")` aliases into another array), and a copy
# row for a twin that was never emitted is an undefined symbol at load time.
$(GEN)/twins.txt: $(GEN_FRAGMENTS)
	@mkdir -p $(dir $@)
	@grep -rhoE '[A-Za-z_][A-Za-z0-9_]*__sbk_size' $(GEN)/src | sed 's/__sbk_size//' | sort -u > $@

$(PIN_TABLE): $(N64_MAP) $(OBJ_LIST) tools/gen_pins.py $(GEN)/twins.txt
	$(PYTHON) tools/gen_pins.py $< --objs $(OBJ_LIST) $(if $(wildcard $(N64_NM)),--nm $(N64_NM)) $(foreach r,$(PIN_SKIP),--pin-skip $(r)) --prefix '$(SYM_PREFIX)' --twins-present $(GEN)/twins.txt --table-c $@

$(BUILD)/pins.o: $(PINS_S)
	$(AS) -o $@ $<

$(BUILD)/pin_table.o: $(PIN_TABLE) src/ultra/sbk_pins.h
	$(CC) $(PORT_CFLAGS) -c -o $@ $<

# Game sources are compiled from a filtered mirror: the game's string literals
# use its own character encoding (upstream runs tools/textconv.py before the
# compiler, and so must we), definitions of pinned globals are renamed to twins
# and port/patches.txt substitutions are applied.
$(GEN)/%.c: $(ROOT)/%.c tools/mirror_src.py patches.txt $(PINS_TXT) $(OV_RENAMES) $(ROOT)/tools/textconv.py $(ROOT)/tools/charmap.txt
	@mkdir -p $(dir $@)
	@$(PYTHON) $(ROOT)/tools/textconv.py $(ROOT)/tools/charmap.txt $< $@.tc
	@$(PYTHON) tools/mirror_src.py --pins $(PINS_TXT) --renames $(OV_RENAMES) $(MIRROR_FLAGS) $@.tc $@
	@rm -f $@.tc


$(GEN)/lib/%.c: MIRROR_FLAGS = --twins-static

$(BUILD)/game/%.o: $(GEN)/%.c $(GEN_FRAGMENTS) src/port_override.h
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) -I$(dir $<) -I$(ROOT)/$(dir $*) -I$(ROOT)/build/$(dir $*) -c -o $@ $<

# the Xbox One driver includes the 10.4 SDK USB headers through a patched copy (compat/)
$(BUILD)/port/src/platform/input_xone.o: src/platform/input_xone.c
	@mkdir -p $(dir $@)
	$(CC) $(PORT_CFLAGS) -Isrc/platform/compat -c -o $@ $<

$(BUILD)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PORT_CFLAGS) -c -o $@ $<

$(BUILD)/port/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) -o $@ $<

$(BUILD)/rom_syms.s: $(N64_MAP) tools/gen_rom_syms.py $(PINS_TXT) $(PINS_STAMP)
	@mkdir -p $(dir $@)
	$(PYTHON) tools/gen_rom_syms.py $< $(foreach l,$(ABS_LD),--ld $(l)) --extra-abs $(PINS_TXT) --skip-file $(BUILD)/native_syms.txt $(foreach s,$(RSP_SYMS),--fixed $(s)) $(foreach s,$(PORT_DEFINED),--skip $(s)) --prefix '$(SYM_PREFIX)' --out $@

$(ROM_SYMS): $(BUILD)/rom_syms.s
	$(AS) -o $@ $<

$(OV_STAMP): $(N64_MAP) $(LD_SCRIPT) tools/gen_overlays.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/gen_overlays.py $(N64_MAP) --ld $(LD_SCRIPT) --prefix '$(SYM_PREFIX)' --thunks $(OV_THUNKS) --table $(OV_TABLE) --renames $(OV_RENAMES)
	@touch $@

$(OV_THUNKS) $(OV_TABLE) $(OV_RENAMES): $(OV_STAMP)

$(BUILD)/overlay_thunks.o: $(OV_THUNKS)
	$(AS) -o $@ $<

$(BUILD)/overlay_table.o: $(OV_TABLE) src/ultra/sbk_overlay.h
	$(CC) $(PORT_CFLAGS) -c -o $@ $<

$(EXE): $(GAME_OBJS) $(PORT_OBJS) $(DATA_OBJS) $(ROM_SYMS) $(PIN_OBJS) $(OV_OBJS) Makefile
	$(CC) -o $@ $(GAME_OBJS) $(PORT_OBJS) $(DATA_OBJS) $(ROM_SYMS) $(PIN_OBJS) $(OV_OBJS) $(LDFLAGS)
	@echo "built $@"

clean:
	rm -rf $(BUILD)

.PHONY: all list clean

game-objs: $(GAME_OBJS)
	@echo "game objects built"
.PHONY: game-objs
