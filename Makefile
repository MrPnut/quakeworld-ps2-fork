# ============================================================================
#  Quake PS2 - build system
# ----------------------------------------------------------------------------
#  Produces build/<config>/quake.elf for the PS2 EE, using the modern ps2dev
#  toolchain (mips64r5900el-ps2-elf-*). Built on the PS2SDK sample makefiles.
#
#    make             -> debug build -> build/debug/quake.elf   (VSCode: Shift+Cmd+B)
#    make release     -> optimized   -> build/release/quake.elf
#    make run         -> build + launch in PCSX2  (VSCode: F5)
#    make release run -> the same, with the release build
#    make iso         -> disc build (release) + build/iso_release/quake.iso from id1/
#    make iso_dbg     -> disc build (debug)   + build/iso_debug/quake.iso
#    make iso run     -> the same, then boot the image in PCSX2 (iso_dbg run likewise)
#    make tools       -> build host tools (unpak, imgdump, musenc, pakzip, symbolize) into build/tools/
#    make music       -> encode id1/music/trackNN.wav into the trackNN.adp files the game streams
#    make pkz         -> convert id1/pakN.pak into the compressed pakN.pkz the game prefers
#    make clean       -> remove build artifacts (every config)
#    make clean_vu    -> remove only the compiled VU microprograms and their headers
#
#  Header dependencies are tracked automatically (-MMD), so editing a header
#  rebuilds just the affected objects; no more `make clean` after header edits.
# ============================================================================

# Toolchain / tool locations (override from the environment if needed):
PS2DEV ?= /Users/guilherme/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
PCSX2  ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2

# ----------------------------------------------------------------------------
#  Build configuration
# ----------------------------------------------------------------------------
#
#  Picked by the `release`, `iso` or `iso_dbg` goal, or by BUILD= on the command line:
#
#      make          -> debug       -O2, DWARF info, asserts and debug-only code IN
#      make release  -> release     -O3, no DWARF, asserts and debug-only code OUT
#      make iso      -> iso_release the release build, reading its data off a disc
#      make iso_dbg  -> iso_debug   the debug build, the same
#
#  The disc builds (PS2_QUAKE_ISO=1) load the game data from the CD/DVD drive
#  (ps2/fs/cdvd_device.h) - a disc image booted by Open PS2 Loader or PCSX2 -
#  instead of host:, the HDD or USB, and save to the memory card only. Their
#  goals also make the image, build/<config>/quake.iso, from ISO_DATA (id1/
#  by default) with src/tools/scripts/make_iso.py; `make BUILD=iso_release`
#  builds the ELF alone. The repository has no game data to put on one.
#
#  Each config owns its object tree under build/<config>/, so alternating
#  between them neither mixes objects built with different flags nor forces
#  a full rebuild.

CONFIG_GOALS = $(filter release iso iso_dbg,$(MAKECMDGOALS))
ifneq ($(word 2,$(CONFIG_GOALS)),)
    $(error Pick one of release, iso and iso_dbg, not '$(CONFIG_GOALS)')
endif

ifeq ($(CONFIG_GOALS),release)
    BUILD := release
else ifeq ($(CONFIG_GOALS),iso)
    BUILD := iso_release
else ifeq ($(CONFIG_GOALS),iso_dbg)
    BUILD := iso_debug
else
    BUILD ?= debug
endif

ifeq ($(filter $(BUILD),debug release iso_debug iso_release),)
    $(error BUILD must be 'debug', 'release', 'iso_debug' or 'iso_release', got '$(BUILD)')
endif

# 1 for the disc builds. RELEASE_FLAGS picks the optimized flags below for either kind of release.
ISO_BUILD     = $(if $(filter iso_%,$(BUILD)),1,0)
RELEASE_FLAGS = $(if $(filter release iso_release,$(BUILD)),1,0)

# Strip the linked ELF - in BOTH configs, since the debug build is what gets
# iterated on and DWARF dominates its size. Costs nothing at runtime: the PS2
# loader only reads program headers, which strip leaves alone. The symbols are
# not lost, they stay in quake_unstripped.elf beside it (feed that one to
# addr2line to resolve a crash address). STRIP_ELF=0 turns this off and ships
# the unstripped ELF as quake.elf instead.
STRIP_ELF ?= 1

SRC_DIR    = src
BUILD_DIR  = build
OUTPUT_DIR = $(BUILD_DIR)/$(BUILD)

# The SDK link rule (Makefile.eeglobal_cpp) produces $(EE_BIN) with full symbols;
# $(GAME_ELF) is the binary that actually runs, stripped out of it below.
EE_BIN   = $(OUTPUT_DIR)/quake_unstripped.elf
GAME_ELF = $(OUTPUT_DIR)/quake.elf

# ----------------------------------------------------------------------------
#  Source files
# ----------------------------------------------------------------------------

# New PS2 backend, modern C++:
PS2_CXX_SRC =                         \
	ps2/system/main.cpp               \
	ps2/system/sys.cpp                \
	ps2/system/iop_boot.cpp           \
	ps2/system/video_mode.cpp         \
	ps2/system/heap.cpp               \
	ps2/math/vec_mat.cpp              \
	ps2/net/net.cpp                   \
	ps2/input/input.cpp               \
	ps2/input/pad.cpp                 \
	ps2/input/keyboard.cpp            \
	ps2/input/rumble.cpp              \
	ps2/audio/snd.cpp                 \
	ps2/audio/audsrv_device.cpp       \
	ps2/audio/mix_ring.cpp            \
	ps2/audio/music_stream.cpp        \
	ps2/audio/sound_load.cpp          \
	ps2/audio/cd_audio.cpp            \
	ps2/save/save_api.cpp             \
	ps2/save/save_device.cpp          \
	ps2/save/packed_blob.cpp          \
	ps2/save/slot_archive.cpp         \
	ps2/save/memcard.cpp              \
	ps2/save/drive.cpp                \
	ps2/save/mc_icon.cpp              \
	ps2/save/save_notice.cpp          \
	ps2/fs/pkz_pack.cpp               \
	ps2/fs/async_io.cpp               \
	ps2/fs/readahead_stream.cpp       \
	ps2/fs/aio_pump.cpp               \
	ps2/renderer/gs.cpp               \
	ps2/renderer/vram.cpp             \
	ps2/renderer/texture.cpp          \
	ps2/renderer/scrap_atlas.cpp      \
	ps2/renderer/profile.cpp          \
	ps2/renderer/vu1.cpp              \
	ps2/renderer/cmd_buffer.cpp       \
	ps2/renderer/render_system.cpp    \
	ps2/renderer/vid.cpp              \
	ps2/renderer/gamma.cpp            \
	ps2/renderer/draw.cpp             \
	ps2/renderer/texmgr.cpp           \
	ps2/renderer/brush.cpp            \
	ps2/renderer/lightmap.cpp         \
	ps2/renderer/alias.cpp            \
	ps2/renderer/model_load.cpp       \
	ps2/renderer/sprite.cpp           \
	ps2/renderer/particles.cpp        \
	ps2/renderer/sky.cpp              \
	ps2/renderer/view.cpp             \
	ps2/renderer/refresh.cpp          \
	ps2/renderer/overlays.cpp         \
	ps2/renderer/loading_screen.cpp   \
	ps2/renderer/splash_screen.cpp    \
	ps2/tests/draw_cube.cpp           \
	ps2/tests/map_cycle.cpp           \
	ps2/tests/perf_run.cpp            \
	ps2/tests/test_quit.cpp           \
	ps2/tests/aio_test.cpp            \
	ps2/debug/scr_print.cpp           \
	ps2/debug/stack_trace.cpp         \
	ps2/debug/pipeline_dump.cpp       \
	ps2/debug/exception_handler.cpp   \
	ps2/debug/log_file.cpp            \
	ps2/debug/load_trace.cpp          \
	ps2/debug/io_log.cpp              \
	ps2/debug/debug_menu.cpp          \
	ps2/debug/profile.cpp             \
	ps2/menu/about_menu.cpp           \
	ps2/builtin/palette.cpp           \
	ps2/builtin/qplaque.cpp           \
	ps2/builtin/menuabout.cpp

# Doug Lea's allocator: vendored third-party C, left as C on purpose (see the
# note at the top of dlmalloc.c). Everything else of ours is C++.
PS2_C_SRC = ps2/system/dlmalloc/dlmalloc.c

# QuakeSpasm's C, statically linked. Its OpenGL renderer is not here: the PS2
# backend implements the renderer's public surface (draw.h, render.h,
# gl_texmgr.h, vid.h) instead. The gl_*/r_* files listed are the ones that hold
# engine logic - gl_model.c's BSP loading feeds the server too - built with
# their OpenGL halves cut out.
ENGINE_C_SRC = \
	quake/cl_cam.c \
	quake/cl_demo.c \
	quake/cl_ents.c \
	quake/cl_input.c \
	quake/cl_main.c \
	quake/cl_parse.c \
	quake/cl_pred.c \
	quake/cl_tent.c \
	quake/cmd.c \
	quake/common.c \
	quake/console.c \
	quake/crc.c \
	quake/cvar.c \
	quake/cfgfile.c \
	quake/keys.c \
	quake/mathlib.c \
	quake/md4.c \
	quake/menu.c \
	quake/net_chan.c \
	quake/pcx.c \
	quake/pmove.c \
	quake/pmovetst.c \
	quake/sbar.c \
	quake/skin.c \
	quake/snd_dma.c \
	quake/snd_mem.c \
	quake/snd_mix.c \
	quake/view.c \
	quake/wad.c \
	quake/zone.c \
	quake/strlcat.c \
	quake/strlcpy.c \
	quake/gl_model.c \
	quake/gl_refrag.c \
	quake/gl_rlight.c \
	quake/gl_screen.c \
	quake/gl_fog.c \
	quake/r_part.c \
	ps2_qw/mod_menu.c \
	ps2_qw/ingame_menu.c

# The disc builds' game data device: a read-only ISO9660 file system over the CD/DVD drive.
ifeq ($(ISO_BUILD),1)
PS2_CXX_SRC += ps2/fs/cdvd_device.cpp
endif

C_SRC   = $(PS2_C_SRC) $(ENGINE_C_SRC)
CXX_SRC = $(PS2_CXX_SRC)

# Backend sources that run at load time or not at all in a normal frame: asset
# parsing, IOP module boot, device setup, the debug screen printer. None of them
# are on the per-frame path, so they are built for size instead of speed, which
# is RAM the levels get to use instead.
SIZE_OPT_CXX_SRC =                    \
	ps2/renderer/texture.cpp          \
	ps2/renderer/scrap_atlas.cpp      \
	ps2/system/iop_boot.cpp           \
	ps2/system/video_mode.cpp         \
	ps2/audio/audsrv_device.cpp       \
	ps2/save/save_api.cpp             \
	ps2/save/save_device.cpp          \
	ps2/save/packed_blob.cpp          \
	ps2/save/slot_archive.cpp         \
	ps2/save/memcard.cpp              \
	ps2/save/drive.cpp                \
	ps2/save/mc_icon.cpp              \
	ps2/save/save_notice.cpp          \
	ps2/fs/pkz_pack.cpp               \
	ps2/fs/readahead_stream.cpp       \
	ps2/renderer/brush.cpp            \
	ps2/input/keyboard.cpp            \
	ps2/input/pad.cpp                 \
	ps2/renderer/vid.cpp              \
	ps2/renderer/gamma.cpp            \
	ps2/renderer/loading_screen.cpp   \
	ps2/renderer/splash_screen.cpp    \
	ps2/tests/draw_cube.cpp           \
	ps2/tests/map_cycle.cpp           \
	ps2/tests/perf_run.cpp            \
	ps2/tests/test_quit.cpp           \
	ps2/tests/aio_test.cpp            \
	ps2/debug/scr_print.cpp           \
	ps2/debug/stack_trace.cpp         \
	ps2/debug/pipeline_dump.cpp       \
	ps2/debug/exception_handler.cpp   \
	ps2/debug/log_file.cpp            \
	ps2/debug/load_trace.cpp          \
	ps2/debug/io_log.cpp              \
	ps2/debug/debug_menu.cpp          \
	ps2/debug/profile.cpp             \
	ps2/menu/about_menu.cpp

SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/$(SRC_DIR)/, $(SIZE_OPT_CXX_SRC:.cpp=.o))

C_OBJS   = $(addprefix $(OUTPUT_DIR)/$(SRC_DIR)/, $(C_SRC:.c=.o))
CXX_OBJS = $(addprefix $(OUTPUT_DIR)/$(SRC_DIR)/, $(CXX_SRC:.cpp=.o))

# VU microprograms, written in VUSL: oxide-vusl -> dvp-as. Each .vusl compiles
# into a .vsm, assembled into .vudata with <name>_CodeStart/_CodeEnd link symbols
# (see PS2_DECLARE_VU_MICROPROGRAM in ps2/renderer/vu1.h), and a C++ header,
# <name>.vusl.h, with the program's memory layout: vu1.h includes it and checks
# the host's structs and constants against it. None of the EE compiler flags
# reach this toolchain, so the output is identical in both configs: build it
# once into build/vu/ and share it.
VUSL_PATH    = $(SRC_DIR)/ps2/renderer/vu1progs
VUSL_FILES   = textured_triangles.vusl particles.vusl
VU_OBJS      = $(addprefix $(BUILD_DIR)/vu/, $(VUSL_FILES:.vusl=.o))
VU_HEADERS   = $(addprefix $(BUILD_DIR)/vu/, $(VUSL_FILES:.vusl=.vusl.h))

# Shared include files (frame.vush). The programs include these by bare name,
# which the preprocessor finds next to the including file; they are prerequisites
# here because the pattern rule below would not otherwise see them change.
VUSL_INCS    = $(wildcard $(VUSL_PATH)/*.vush)

# oxide-vusl (https://github.com/glampert/oxide-vcl), the VUSL compiler the VU
# programs are built with: it compiles VUSL to VCL and hands that to oxide-vcl in
# the same process, in oxide mode with every fix and optimization. It is not part
# of the ps2dev distribution: it comes in as the oxide-vcl git submodule and is
# built by cargo into build/tools/oxide-vcl/, so every VU build runs the pinned
# version. --warnings-as-errors fails the build on any warning, the
# preprocessor's or the compiler's.
OXIDE_VCL_PATH   = $(SRC_DIR)/tools/oxide-vcl
OXIDE_VCL_BUILD  = $(BUILD_DIR)/tools/oxide-vcl
OXIDE_VUSL       = $(OXIDE_VCL_BUILD)/release/oxide-vusl
OXIDE_VUSL_FLAGS = --warnings-as-errors

# miniz (https://github.com/richgel999/miniz), the deflate codec the save games are
# compressed with. A git submodule like oxide-vcl; only the raw deflate/inflate and
# CRC-32 sources are built, straight from the submodule. Its headers include a
# miniz_export.h that miniz's CMake would generate: the one in MINIZ_CFG_PATH stands
# in for it and also carries the build configuration, so the library and every file
# including miniz.h agree on it (struct sizes depend on TDEFL_LESS_MEMORY).
MINIZ_PATH     = $(SRC_DIR)/tools/miniz
MINIZ_CFG_PATH = $(SRC_DIR)/ps2/save/miniz_cfg
MINIZ_SRC      = miniz.c miniz_tdef.c miniz_tinfl.c
MINIZ_OBJS     = $(addprefix $(OUTPUT_DIR)/miniz/, $(MINIZ_SRC:.c=.o))

# Standalone command line tools: the C++ ones under src/tools/host, built with the
# HOST C++ compiler (not the EE toolchain) since they run on the development
# machine, and the Python ones under src/tools/scripts. Being host binaries they
# are config-independent, so they live outside build/<config>/.
#
# The C++ tools are a CMake project (src/tools/host/CMakeLists.txt), so they build the
# same way on every host, Windows included. It owns their compiler flags and the host
# configuration of miniz that pakzip links (full-size compressor tables, unlike the
# game's). Its build tree is build/tools/cmake/; the binaries land in build/tools/.
HOST_TOOLS_PATH  = $(SRC_DIR)/tools/host
HOST_TOOLS_CMAKE = $(BUILD_DIR)/tools/cmake
SCRIPTS_PATH     = $(SRC_DIR)/tools/scripts
TOOLS_CXX_BINS   = $(addprefix $(BUILD_DIR)/tools/, unpak imgdump musenc pakzip)
TOOLS_PY_BINS    = $(addprefix $(BUILD_DIR)/tools/, symbolize)
TOOLS_BINS       = $(TOOLS_CXX_BINS) $(TOOLS_PY_BINS)
HOST_CXX        ?= c++
HOST_CC         ?= cc

# IOP/IRX modules embedded into the ELF: HDD/PFS and BDM USB storage, booted
# by ps2/system/iop_boot.cpp when the game data isn't on host:,
# the USB keyboard driver started on demand by ps2/input/keyboard.cpp, and the
# sound driver pair (libsd under audsrv) started by ps2/audio/audsrv_device.cpp.
# A disc build reads its data through the IOP's own CD drivers (the boot ROM's, or OPL's) and has
# no keyboard, so it carries only the sound driver pair.
IRX_PATH  = $(PS2SDK)/iop/irx
ifeq ($(ISO_BUILD),1)
IRX_FILES = libsd.irx audsrv.irx
else
IRX_FILES = iomanX.irx fileXio.irx \
            ps2dev9.irx ps2atad.irx ps2hdd.irx ps2fs.irx \
            bdm.irx bdmfs_fatfs.irx usbd.irx usbmass_bd.irx \
            ps2kbd.irx libsd.irx audsrv.irx \
			netman.irx smap.irx
endif

IRX_OBJS  = $(addprefix $(OUTPUT_DIR)/irx/, $(IRX_FILES:.irx=.o))

EE_OBJS = $(C_OBJS) $(CXX_OBJS) $(VU_OBJS) $(IRX_OBJS) $(MINIZ_OBJS)
DEPS    = $(C_OBJS:.o=.d) $(CXX_OBJS:.o=.d) $(MINIZ_OBJS:.o=.d)

# ----------------------------------------------------------------------------
#  Compiler / linker flags (appended to the SDK defaults from Makefile.eeglobal)
# ----------------------------------------------------------------------------

# Per-config flags. EE_OPTFLAGS and EE_DBGINFOFLAGS are the SDK's own knobs:
# Makefile.eeglobal_cpp defaults them with ?= (to -O2 and -gdwarf-2 -gz), so
# whatever is set here wins - including setting the debug info to empty.
#
# PS2_QUAKE_DEBUG, PS2_QUAKE_ASSERTS, PS2_QUAKE_PROFILE and PS2_QUAKE_ISO are always defined
# to 0 or 1, never undefined. PS2_QUAKE_ISO comes after CONFIG_DEFS, so a CONFIG_DEFS given on
# the command line (performance.md's profiling builds) keeps it.
ifeq ($(RELEASE_FLAGS),1)
    EE_OPTFLAGS     = -O3
    EE_DBGINFOFLAGS =
    CONFIG_DEFS     = -DPS2_QUAKE_DEBUG=0 -DPS2_QUAKE_ASSERTS=0 -DPS2_QUAKE_PROFILE=0 -DNDEBUG
else
    EE_OPTFLAGS     = -O2
    EE_DBGINFOFLAGS = -gdwarf-2 -gz
    CONFIG_DEFS     = -DPS2_QUAKE_DEBUG=1 -DPS2_QUAKE_ASSERTS=1 -DPS2_QUAKE_PROFILE=0
endif

COMMON_DEFS = -DPS2_QUAKE $(CONFIG_DEFS) -DPS2_QUAKE_ISO=$(ISO_BUILD)

EE_INCS += -I$(SRC_DIR)

# The VU programs' generated headers (see VU_HEADERS), included by vu1.h by bare name.
EE_INCS += -I$(BUILD_DIR)/vu

# Floating-point semantics, for QuakeSpasm's C and the backend C++ alike. GCC already models
# the EE FPU as having no NaN or Inf (sqrtf is a bare sqrt.s, fabsf an abs.s), so what is
# left to give up is the sign of zero: -fno-signed-zeros lets GCC turn each float clamp
# (x < lo ? lo : x) into a branchless min.s/max.s and fold x + 0.0f away. Here the perf
# test moved only Sky (-4.9%); Quake II's per-particle clamp gained 13.7% from it. The
# other two are near no-ops kept for intent: -fno-math-errno is already implied,
# -fno-trapping-math moves a few instructions. Soft-float double math keeps its signed
# zeros and NaNs in libgcc, so this changes little there. The EE has no FP exceptions
# either way. Nothing may depend on the sign of a zero result: x / -0.0f saturates to
# -FLT_MAX, x / 0.0f to +FLT_MAX.
# Override with EE_FPFLAGS= (and a separate OUTPUT_DIR) for an A/B.
EE_FPFLAGS ?= -fno-math-errno -fno-trapping-math -fno-signed-zeros

# The C side of the build is QuakeSpasm, the vendored dlmalloc and the bin2c IRX
# blobs - everything of ours is C++. QuakeSpasm is C11: common.h's q_min/q_max/CLAMP
# use _Generic there, and GCC 15 would otherwise compile it as C23. It keeps the
# SDK's -Wall and is not held to the backend's -Werror set.
#
# -fsingle-precision-constant: QuakeSpasm writes its float constants unsuffixed
# (x * 0.5), which C makes double - and the EE has no double FPU, so every one of
# those would turn a float expression into libgcc soft-float calls. The backend's
# C++ needs no such flag: it uses f suffixes, and -Wdouble-promotion enforces them.
#
EE_CFLAGS += -std=gnu11 -fno-strict-aliasing -fsingle-precision-constant $(EE_FPFLAGS) $(COMMON_DEFS) -MMD -MP

# Strict, portable, warnings-as-errors for the new C++ backend (applies ONLY to
# our .cpp - QuakeSpasm's C above stays lenient). The set targets portability and
# undefined behaviour: value-changing/alignment/format hazards, accidental
# float->double promotion (the EE has no hardware doubles), shadowing, VLAs, and
# GCC's near-zero-false-positive logic/duplicate-branch checks.
# -Wconversion/-Wsign-conversion flag every implicit value-, sign- or precision-
# changing conversion (all backend code must cast intentionally); SDK/STL library
# conversions are silenced via -isystem below, so only our own code is enforced.
EE_CXX_WARNFLAGS = -Wall -Wextra -Werror \
	-Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith \
	-Wcast-align -Wwrite-strings -Wredundant-decls -Wnull-dereference \
	-Wnon-virtual-dtor -Woverloaded-virtual -Wvla \
	-Wlogical-op -Wduplicated-cond -Wduplicated-branches

# Reclassify the PS2SDK headers as system headers for C++ so their own warnings
# (e.g. redundant redeclarations in kernel.h) don't trip our -Werror. The same
# dirs are still added via -I by Makefile.eeglobal; GCC then ignores the -I copy
# and treats them as system. Our own headers stay under -Isrc (warnings enforced).
EE_CXX_SYSINCS = -isystem $(PS2SDK)/ee/include -isystem $(PS2SDK)/common/include \
	-isystem $(MINIZ_PATH) -isystem $(MINIZ_CFG_PATH)

# Lean, embedded C++ for the new backend.
EE_CXXFLAGS += -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-strict-aliasing $(EE_FPFLAGS) $(COMMON_DEFS) \
	$(EE_CXX_WARNFLAGS) $(EE_CXX_SYSINCS) \
	-MMD -MP

# -leedebug supplies the level 1 exception vector that src/ps2/debug/exception_handler.cpp
# hangs its post-mortem off. It contributes nothing to a release build - the whole
# handler is behind PS2_QUAKE_DEBUG - but the linker only pulls in what is referenced,
# so leaving it on the line for both configs costs nothing.
EE_LIBS += -lps2ip -lnetman -lkernel -ldraw -lgraph -lpacket2 -ldma -lpad -lkbd -laudsrv -lpatches -lfileXio -lmc -leedebug

# The disc builds read the drive through libcdvd. fileXio stays on the line: they call none of it,
# so none of it is linked.
ifeq ($(ISO_BUILD),1)
EE_LIBS += -lcdvd
endif

# ----------------------------------------------------------------------------
#  Rules
# ----------------------------------------------------------------------------

.PHONY: all release iso iso_dbg iso_image run tools host_tools music pkz clean clean_vu compiledb

all: $(GAME_ELF) tools

# `release` only selects the config (see BUILD above); the build itself is `all`. `iso` and
# `iso_dbg` select a disc build, and make its image too.
release: all
iso iso_dbg: iso_image

# The disc image of a disc build, from your own game data: make_iso.py picks what goes on it (the
# pkz over its pak, the music's .adp tracks or a track's .wav without one, no saves or logs) and
# copies it. It runs on every make, as make can't see a change in ISO_DATA. ISO_FLAGS passes it
# more, e.g. --uncompressed-paks.
ISO_DATA  ?= id1
ISO_FLAGS ?=
ISO_IMAGE  = $(OUTPUT_DIR)/quake.iso
MAKE_ISO   = $(SCRIPTS_PATH)/make_iso.py

iso_image: all
	@test "$(ISO_BUILD)" = 1 || { echo "iso_image needs a disc build: make iso or make iso_dbg"; exit 1; }
	python3 $(MAKE_ISO) --force --elf $(GAME_ELF) --data $(ISO_DATA) -o $(ISO_IMAGE) $(ISO_FLAGS)

# Records which STRIP_ELF setting the current $(GAME_ELF) was made with. make
# compares timestamps, not recipes, so without this a `make STRIP_ELF=0` over an
# already-built tree would leave the stripped ELF in place and report nothing to
# do. Flipping the flag switches to a marker that does not exist yet, which
# re-makes the ELF below.
STRIP_MARKER = $(OUTPUT_DIR)/.strip_elf-$(STRIP_ELF)

$(STRIP_MARKER):
	@mkdir -p $(dir $@)
	@rm -f $(OUTPUT_DIR)/.strip_elf-*
	@touch $@

# The runnable ELF, made from the symbol-carrying one the SDK link rule builds.
ifeq ($(STRIP_ELF),0)
$(GAME_ELF): $(EE_BIN) $(STRIP_MARKER)
	cp -f $< $@
else
$(GAME_ELF): $(EE_BIN) $(STRIP_MARKER)
	$(EE_STRIP) --strip-all -o $@ $<
endif

# Out-of-tree object rules. These static-pattern rules take precedence over the
# generic %.o rules from Makefile.eeglobal so objects land under build/<config>/
# mirroring the src/ tree. ($(EE_BIN) link rule comes from Makefile.eeglobal_cpp.)
#
# One rule per language: the C one is only reached by QuakeSpasm and dlmalloc,
# every backend source goes through the C++ one.
$(C_OBJS): $(OUTPUT_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# dlmalloc is configured with MORECORE_CANNOT_TRIM (see dlmalloc.c), which leaves its
# sYSTRIm helper unused. Vendored code: the warning is silenced for that one file
# rather than for QuakeSpasm as well.
$(OUTPUT_DIR)/$(SRC_DIR)/ps2/system/dlmalloc/dlmalloc.o: EE_CFLAGS += -Wno-unused-function

$(CXX_OBJS): $(OUTPUT_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(EE_CXX) $(EE_CXXFLAGS) $(EE_INCS) $(CXX_OPTFLAGS_FOR) -c $< -o $@

# Per-object optimization override for the cold sources listed above. EE_CXXFLAGS
# already carries $(EE_OPTFLAGS) from Makefile.eeglobal; appending -Os after it
# wins, since the last -O on the command line is the one GCC applies. Target-
# specific variables are inherited by the rule above, so only these objects see it.
$(SIZE_OPT_OBJS): CXX_OPTFLAGS_FOR = -Os

# miniz, straight out of the submodule. Always -O3, in the debug config too: deflating
# a level's state is a few hundred KB of work on a slow CPU, done while the player waits.
# Its warnings are not ours to fix.
$(MINIZ_OBJS): $(OUTPUT_DIR)/miniz/%.o: $(MINIZ_PATH)/%.c
	@mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) -O3 -w -I$(MINIZ_PATH) -I$(MINIZ_CFG_PATH) -c $< -o $@

# A clone without the submodule has no miniz sources, so the rule above has nothing to
# build from; this one stops with the fix instead. It never runs while the files exist.
$(MINIZ_PATH)/%.c:
	@echo "$(MINIZ_PATH) is empty - run 'git submodule update --init'"; exit 1

# oxide-vusl, from the oxide-vcl submodule, through cargo. The up-to-date check is
# made here, on the crates' sources and manifests, so the rule runs again only when
# checking out a new commit changes them, and a make with nothing to do never starts
# cargo. --target-dir keeps cargo's output under build/tools/ rather than in the
# submodule's work tree, where git would report it as untracked content. Every VU
# program depends on the binary, so a new oxide-vusl rebuilds them all.
OXIDE_VCL_SRCS = $(OXIDE_VCL_PATH)/Cargo.toml $(OXIDE_VCL_PATH)/Cargo.lock \
                 $(shell find $(OXIDE_VCL_PATH)/crates -path '*/tests' -prune -o -type f -print 2>/dev/null)

$(OXIDE_VUSL): $(wildcard $(OXIDE_VCL_SRCS))
	@test -f $(OXIDE_VCL_PATH)/Cargo.toml || \
		{ echo "$(OXIDE_VCL_PATH) is empty - run 'git submodule update --init'"; exit 1; }
	@cargo build --quiet --release --locked --bin oxide-vusl \
		--manifest-path $(OXIDE_VCL_PATH)/Cargo.toml --target-dir $(OXIDE_VCL_BUILD)
	@touch $@

# VU1 microprograms. oxide-vusl writes the listing and the header together, or
# neither when the program has an error; the .vsm is kept beside the object for
# reading. In oxide mode oxide-vcl rewrites or rejects an immediate that does not
# fit its field (its 'imm' fix) rather than leaving dvp-as to truncate it. An
# 'operand out of range' warning from dvp-as on a branch longer than 1023
# instructions is harmless: VU1 micro memory is exactly 2048 instructions and the
# PC wraps, so the low 11 bits dvp-as keeps reach the same instruction.
#
# One recipe makes both targets: a pattern rule with two targets is run once for
# the pair.
$(BUILD_DIR)/vu/%.o $(BUILD_DIR)/vu/%.vusl.h: $(VUSL_PATH)/%.vusl $(VUSL_INCS) $(OXIDE_VUSL)
	@mkdir -p $(dir $@)
	$(OXIDE_VUSL) $(OXIDE_VUSL_FLAGS) --header=$(BUILD_DIR)/vu/$*.vusl.h -o$(BUILD_DIR)/vu/$*.vsm $<
	dvp-as $(BUILD_DIR)/vu/$*.vsm -o $(BUILD_DIR)/vu/$*.o

# vu1.h includes the generated headers, so no C++ object can be built before them.
# Order-only: once an object exists, its -MMD dependency file names the headers it
# really includes, and that is what rebuilds it when one changes.
$(CXX_OBJS): | $(VU_HEADERS)

# IOP modules embedded via bin2c.
$(OUTPUT_DIR)/irx/%.o: $(IRX_PATH)/%.irx
	@mkdir -p $(dir $@)
	$(PS2SDK)/bin/bin2c $< $(basename $@).c $(notdir $(basename $@))_irx
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $(basename $@).c -o $@

# Host tools.
tools: $(TOOLS_BINS)

# Configured once. After that, `cmake --build` configures again by itself whenever
# CMakeLists.txt changes. HOST_CC/HOST_CXX only take effect here, so remove build/tools/
# after changing them. No "Built target" lines, so a build with nothing to do is silent.
$(HOST_TOOLS_CMAKE)/CMakeCache.txt:
	cmake -S $(HOST_TOOLS_PATH) -B $(HOST_TOOLS_CMAKE) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(HOST_CC) -DCMAKE_CXX_COMPILER=$(HOST_CXX) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(abspath $(BUILD_DIR)/tools) \
		-DCMAKE_TARGET_MESSAGES=OFF

# CMake tracks the tools' sources and headers itself (musenc shares spu_adpcm.h with the
# game, so it plays back exactly what it measured, pakzip shares pkz_format.h, and both see
# the miniz submodule), and does nothing when none of them changed. So this runs on every
# make, instead of copying that list here. MAKEFLAGS is cleared so the Makefiles CMake
# generates don't try to join this make's jobserver, which isn't passed to a non-recursive
# recipe; --parallel gives them jobs of their own.
host_tools: $(HOST_TOOLS_CMAKE)/CMakeCache.txt
	@MAKEFLAGS= cmake --build $(HOST_TOOLS_CMAKE) --parallel

$(TOOLS_CXX_BINS): host_tools ;

# The soundtrack for the CD audio module (ps2/audio/cd_audio.cpp): every trackNN.wav in
# MUSIC_DIR, any case, is encoded to a lowercase trackNN.adp beside it, skipping the ones
# already newer than both their .wav and the encoder. The .wav files are only the source;
# the game never reads them, so they needn't go onto the USB stick.
MUSIC_DIR ?= id1/music

music: $(BUILD_DIR)/tools/musenc
	@found=0; \
	for wav in $(MUSIC_DIR)/[Tt]rack*.wav; do \
		[ -e "$$wav" ] || continue; \
		found=1; \
		adp="$(MUSIC_DIR)/$$(basename "$$wav" .wav | tr 'A-Z' 'a-z').adp"; \
		if [ "$$adp" -nt "$$wav" ] && [ "$$adp" -nt $(BUILD_DIR)/tools/musenc ]; then continue; fi; \
		$(BUILD_DIR)/tools/musenc "$$wav" "$$adp" || exit 1; \
	done; \
	[ $$found = 1 ] || { echo "No trackNN.wav files in $(MUSIC_DIR)/"; exit 1; }

# The compressed game packs (ps2/fs/pkz_format.h): every pakN.pak in PAK_DIR is converted to
# the pakN.pkz beside it, skipping the ones already newer than both their .pak and the
# converter. The game loads a pkz instead of the pak with the same number, so the .pak needn't
# go onto the USB stick once its pkz exists.
PAK_DIR ?= id1

# Demos stay stored, so the game streams them through its read-ahead ring
# (ps2/fs/readahead_stream.h): a deflated one is read on the main thread, inflating as it goes,
# which on the Quake II port froze a frame for 80 ms per 64 KB refill from the HDD.
PKZ_FLAGS ?= --store-ext .dem

pkz: $(BUILD_DIR)/tools/pakzip
	@found=0; \
	for pak in $(PAK_DIR)/pak[0-9].pak; do \
		[ -e "$$pak" ] || continue; \
		found=1; \
		pkz="$${pak%.pak}.pkz"; \
		if [ "$$pkz" -nt "$$pak" ] && [ "$$pkz" -nt $(BUILD_DIR)/tools/pakzip ]; then continue; fi; \
		$(BUILD_DIR)/tools/pakzip $(PKZ_FLAGS) "$$pak" "$$pkz" || exit 1; \
	done; \
	[ $$found = 1 ] || { echo "No pakN.pak files in $(PAK_DIR)/"; exit 1; }

# Script tools are published into build/tools/ under the same extensionless names
# as the compiled ones, so everything in there is invoked the same way.
$(TOOLS_PY_BINS): $(BUILD_DIR)/tools/%: $(SCRIPTS_PATH)/%.py
	@mkdir -p $(dir $@)
	cp -f $< $@
	@chmod +x $@

# PCSX2 exposes the ELF's directory as host:, so the game data must be reachable
# as build/<config>/id1. A symlink back to the repo's id1/ does it.
$(OUTPUT_DIR)/id1:
	@mkdir -p $(dir $@)
	ln -sfn $(abspath id1) $@

# The game's command line, handed to the ELF through PCSX2's -gameargs, e.g.
# make run RUN_ARGS="-heapsize 20480". +commands need the registered data: on the shareware pak
# QuakeSpasm ignores them, as id's Quake did, so script a session with id1/autoexec.cfg instead.
RUN_ARGS ?=

# A disc build boots its image as a disc, the way PCSX2 boots any game. PCSX2 then serves host:
# from the image's directory, where a disc build writes quake.log, and the id1 link takes its
# frame and I/O logs and screenshots to the repo's id1/, as on the other builds.
ifeq ($(ISO_BUILD),1)
run: iso_image $(OUTPUT_DIR)/id1
	$(PCSX2) -batch -- $(abspath $(ISO_IMAGE))
else
run: all $(OUTPUT_DIR)/id1
	$(PCSX2) -batch -elf $(abspath $(GAME_ELF))$(if $(RUN_ARGS), -gameargs "$(RUN_ARGS)")
endif

# Regenerate compile_commands.json so the editor's IntelliSense uses the exact
# per-file compile flags. Run after adding/removing source files. The debug build's
# lines, then the disc debug build's for the sources only it has (the generator keeps
# the first line it sees for each source).
compiledb:
	@{ $(MAKE) -Bnk BUILD=debug; $(MAKE) -Bnk BUILD=iso_debug; } | python3 $(SCRIPTS_PATH)/gen_compile_commands.py

# Every config, not just the selected one.
clean:
	rm -rf $(BUILD_DIR)/debug $(BUILD_DIR)/release $(BUILD_DIR)/iso_debug $(BUILD_DIR)/iso_release \
		$(BUILD_DIR)/vu $(BUILD_DIR)/iop $(BUILD_DIR)/tools

clean_vu:
	rm -rf $(BUILD_DIR)/vu

-include $(DEPS)

# Pull in the PS2SDK toolchain definitions and the C++ link rule. These provide
# EE_CC/EE_CXX/EE_STRIP, the -D_EE/-G0 defaults (the optimization and debug-info
# ones are set per-config above), EE_LDFLAGS (linkfile, max-page-size) and the
# `$(EE_BIN): $(EE_OBJS)` link recipe (links with g++).
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal_cpp
