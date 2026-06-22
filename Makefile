PLATFORM ?= PLATFORM_DESKTOP
BUILD_MODE ?= DEBUG
WEBP_DIR ?= build/libwebp
DEFINES = -D _DEFAULT_SOURCE -D RAYLIB_BUILD_MODE=$(BUILD_MODE) -D $(PLATFORM)
PLATFORM_OS ?= $(shell uname)

# Source files
SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.c)

# libwebp decoder (lossy, lossless and alpha). Only decoder-side sources are
# compiled, so the viewer stays self-contained without pulling in the encoder.
WEBP_DEC_SOURCES = $(wildcard $(WEBP_DIR)/src/dec/*.c)
WEBP_DSP_SOURCES = \
    $(WEBP_DIR)/src/dsp/alpha_processing.c \
    $(WEBP_DIR)/src/dsp/alpha_processing_sse2.c \
    $(WEBP_DIR)/src/dsp/alpha_processing_sse41.c \
    $(WEBP_DIR)/src/dsp/alpha_processing_neon.c \
    $(WEBP_DIR)/src/dsp/alpha_processing_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/cpu.c \
    $(WEBP_DIR)/src/dsp/dec.c \
    $(WEBP_DIR)/src/dsp/dec_clip_tables.c \
    $(WEBP_DIR)/src/dsp/dec_sse2.c \
    $(WEBP_DIR)/src/dsp/dec_sse41.c \
    $(WEBP_DIR)/src/dsp/dec_neon.c \
    $(WEBP_DIR)/src/dsp/dec_msa.c \
    $(WEBP_DIR)/src/dsp/dec_mips32.c \
    $(WEBP_DIR)/src/dsp/dec_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/filters.c \
    $(WEBP_DIR)/src/dsp/filters_sse2.c \
    $(WEBP_DIR)/src/dsp/filters_neon.c \
    $(WEBP_DIR)/src/dsp/filters_msa.c \
    $(WEBP_DIR)/src/dsp/filters_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/lossless.c \
    $(WEBP_DIR)/src/dsp/lossless_avx2.c \
    $(WEBP_DIR)/src/dsp/lossless_sse2.c \
    $(WEBP_DIR)/src/dsp/lossless_sse41.c \
    $(WEBP_DIR)/src/dsp/lossless_neon.c \
    $(WEBP_DIR)/src/dsp/lossless_msa.c \
    $(WEBP_DIR)/src/dsp/lossless_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/rescaler.c \
    $(WEBP_DIR)/src/dsp/rescaler_sse2.c \
    $(WEBP_DIR)/src/dsp/rescaler_neon.c \
    $(WEBP_DIR)/src/dsp/rescaler_msa.c \
    $(WEBP_DIR)/src/dsp/rescaler_mips32.c \
    $(WEBP_DIR)/src/dsp/rescaler_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/upsampling.c \
    $(WEBP_DIR)/src/dsp/upsampling_sse2.c \
    $(WEBP_DIR)/src/dsp/upsampling_sse41.c \
    $(WEBP_DIR)/src/dsp/upsampling_neon.c \
    $(WEBP_DIR)/src/dsp/upsampling_msa.c \
    $(WEBP_DIR)/src/dsp/upsampling_mips_dsp_r2.c \
    $(WEBP_DIR)/src/dsp/yuv.c \
    $(WEBP_DIR)/src/dsp/yuv_sse2.c \
    $(WEBP_DIR)/src/dsp/yuv_sse41.c \
    $(WEBP_DIR)/src/dsp/yuv_neon.c \
    $(WEBP_DIR)/src/dsp/yuv_mips32.c \
    $(WEBP_DIR)/src/dsp/yuv_mips_dsp_r2.c
WEBP_UTILS_SOURCES = \
    $(WEBP_DIR)/src/utils/bit_reader_utils.c \
    $(WEBP_DIR)/src/utils/color_cache_utils.c \
    $(WEBP_DIR)/src/utils/filters_utils.c \
    $(WEBP_DIR)/src/utils/huffman_utils.c \
    $(WEBP_DIR)/src/utils/palette.c \
    $(WEBP_DIR)/src/utils/quant_levels_dec_utils.c \
    $(WEBP_DIR)/src/utils/random_utils.c \
    $(WEBP_DIR)/src/utils/rescaler_utils.c \
    $(WEBP_DIR)/src/utils/thread_utils.c \
    $(WEBP_DIR)/src/utils/utils.c
SOURCES += $(WEBP_DEC_SOURCES) $(WEBP_DSP_SOURCES) $(WEBP_UTILS_SOURCES)

ifeq ($(PLATFORM),PLATFORM_DESKTOP)
    
    CC = gcc

    ifeq ($(PLATFORM_OS),Darwin) # macOS settings
        EXT=
        RAYLIB_DIR ?= ~/raylib
        INCLUDE_DIR = -I ./ -I $(SRCDIR) -I $(WEBP_DIR) -I $(RAYLIB_DIR)/raylib/src -I $(RAYLIB_DIR)/raygui/src
        LIBRARY_DIR = -L $(RAYLIB_DIR)/raylib/src
        ifeq ($(BUILD_MODE),RELEASE)
            CFLAGS = -D_DEFAULT_SOURCE -DPLATFORM_DESKTOP -Wall -D NDEBUG -O3 $(INCLUDE_DIR) $(LIBRARY_DIR)
        else
            CFLAGS = -D_DEFAULT_SOURCE -DPLATFORM_DESKTOP -Wall -g $(INCLUDE_DIR) $(LIBRARY_DIR)
        endif
        LIBS = -lraylib -framework OpenGL -framework Cocoa  -framework IOKit -framework CoreVideo
    endif

    ifeq ($(findstring Linux,$(PLATFORM_OS)),Linux)
        EXT=
        RAYLIB_DIR ?= ~/raylib
        INCLUDE_DIR = -I ./ -I $(SRCDIR) -I $(WEBP_DIR) -I $(RAYLIB_DIR)/raylib/src -I $(RAYLIB_DIR)/raygui/src
        LIBRARY_DIR = -L $(RAYLIB_DIR)/raylib/src
        ifeq ($(BUILD_MODE),RELEASE)
            CFLAGS ?= $(DEFINES) -Wall -Wno-format-truncation -D NDEBUG -O3 $(INCLUDE_DIR) $(LIBRARY_DIR)
        else
            CFLAGS ?= $(DEFINES) -Wall -Wno-format-truncation -g $(INCLUDE_DIR) $(LIBRARY_DIR)
        endif
        LIBS = -lraylib -lGL -lm
    endif
    
    ifneq ($(filter MINGW Windows_NT,$(PLATFORM_OS)),)
        EXT = .exe
        RAYLIB_DIR ?= C:/raylib
        INCLUDE_DIR = -I ./ -I $(SRCDIR) -I $(WEBP_DIR) -I $(RAYLIB_DIR)/raylib/src -I $(RAYLIB_DIR)/raygui/src
        LIBRARY_DIR = -L $(RAYLIB_DIR)/raylib/src
        ifeq ($(BUILD_MODE),RELEASE)
            CFLAGS ?= bvhview.res $(DEFINES) -Wall -mwindows -D NDEBUG -O3 $(INCLUDE_DIR) $(LIBRARY_DIR) 
        else
            CFLAGS ?= bvhview.res $(DEFINES) -Wall -g $(INCLUDE_DIR) $(LIBRARY_DIR)
        endif
        LIBS = -lraylib -lopengl32 -lgdi32 -lwinmm -lurlmon
    endif

endif

ifeq ($(PLATFORM),PLATFORM_WEB)
    CC = emcc
    EXT = .html
    RAYLIB_DIR ?= C:/raylib
    INCLUDE_DIR = -I ./ -I $(SRCDIR) -I $(WEBP_DIR) -I $(RAYLIB_DIR)/raylib/src -I $(RAYLIB_DIR)/raygui/src
    LIBRARY_DIR = -L $(RAYLIB_DIR)/raylib/src
    ifeq ($(BUILD_MODE),RELEASE)
        CFLAGS ?= $(DEFINES) $(RAYLIB_DIR)/raylib/src/libraylib.web.a -Os -s USE_GLFW=3 -s FORCE_FILESYSTEM=1 -s MAX_WEBGL_VERSION=2 -s ALLOW_MEMORY_GROWTH=1 --shell-file ./shell.html $(INCLUDE_DIR) $(LIBRARY_DIR)
    else
        CFLAGS ?= $(DEFINES) $(RAYLIB_DIR)/raylib/src/libraylib.web.a -Os -s ASSERTIONS -s USE_GLFW=3 -s FORCE_FILESYSTEM=1 -s MAX_WEBGL_VERSION=2 -s ALLOW_MEMORY_GROWTH=1 --shell-file ./shell.html $(INCLUDE_DIR) $(LIBRARY_DIR)
    endif
endif

.PHONY: all

all: bvhview

bvhview: $(SOURCES)
	$(CC) -o $@$(EXT) $(SOURCES) $(CFLAGS) $(LIBS) 

clean:
	rm bvhview$(EXT)

