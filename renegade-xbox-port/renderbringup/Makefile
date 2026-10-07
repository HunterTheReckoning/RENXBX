# Renegade Xbox port: render bring-up test (3D engine, stage 2, milestone 2, step 1).
#
#   make CODE=/workspaces/RENXBX/Code
#
# Builds the foundation, the 3D engine (ww3d2) and the port's Direct3D layer into one library,
# links the test against it (only what the test uses is pulled in), and makes an ISO for xemu.

XBE_TITLE = Renegade\ render\ bring-up
GEN_XISO = renegade_render.iso
NXDK_DIR ?= /workspaces/nxdk
NXDK_CXX = y
NXDK_SDL = y
CODE ?= /workspaces/RENXBX/Code

SRCS = $(CURDIR)/main.cpp

# Port flags every Xbox build of the engine needs (see the package README), and the include
# folders EA's project files use for these libraries.
PORT_INCLUDES = -I$(CODE)/xbox/d3d8 -I$(CODE)/wwphys -I$(CODE)/ww3d2 -I$(CODE)/WWAudio -I$(CODE)/wwlib -I$(CODE)/wwdebug -I$(CODE)/WWMath \
                -I$(CODE)/wwsaveload -I$(CODE)/wwbitpack -I$(CODE)/wwtranslatedb -I$(CODE)/wwutil
CXXFLAGS += -I$(CODE)/xbox/include -include $(CODE)/xbox/include/xbox_prelude.h \
            -Wno-c++11-narrowing -w $(PORT_INCLUDES)

ENGINE_SRCS := $(shell python3 $(CURDIR)/../tools/xbox_sources.py $(CODE) engine)

# wwphys is linked in directly rather than through the library: each definition class registers
# its factory through a global object in its own file, and a linker only takes a file out of a
# library when something refers to it. The game refers to every physics class from Combat; this
# test doesn't, so without this, definitions (doors, elevators...) would be skipped silently.
WWPHYS_SRCS := $(filter $(CODE)/wwphys/%,$(ENGINE_SRCS))
SRCS += $(WWPHYS_SRCS)
ENGINE_OBJS := $(addsuffix .obj,$(basename $(filter-out $(CODE)/wwphys/%,$(ENGINE_SRCS))))
ENGINE_LIB  := $(CURDIR)/renegade_engine.lib

include $(NXDK_DIR)/Makefile

main.exe: $(ENGINE_LIB)

$(ENGINE_LIB): $(ENGINE_OBJS)
	@echo "[ LIB      ] $@"
	$(VE) $(LIB) -out:'$@' $^

# EA's source has one upper-case extension (wwlib/TARGA.CPP).
%.obj: %.CPP
	@echo "[ CXX      ] $@"
	$(VE) $(CXX) $(NXDK_CXXFLAGS) $(CXXFLAGS) -c -o '$@' '$<'

# Game files placed in data/ go to the root of the disc (D:\), where the engine finds them by name.
# Symbolic links are followed, so large archives can be linked into data/ instead of copied there.
.PHONY: copy-data
copy-data: $(OUTPUT_DIR)
	@cp -fL $(CURDIR)/data/*.dds $(CURDIR)/data/*.DDS $(CURDIR)/data/*.tga $(CURDIR)/data/*.TGA \
	    $(CURDIR)/data/*.w3d $(CURDIR)/data/*.W3D $(CURDIR)/data/*.ini $(CURDIR)/data/*.INI \
	    $(CURDIR)/data/*.dat $(CURDIR)/data/*.DAT $(CURDIR)/data/*.mix $(CURDIR)/data/*.MIX \
	    $(CURDIR)/data/*.dbs $(CURDIR)/data/*.DBS \
	    $(OUTPUT_DIR)/ 2>/dev/null || true
$(GEN_XISO): copy-data

.PHONY: clean-engine
clean-engine:
	$(VE) rm -f $(ENGINE_OBJS) $(ENGINE_LIB)
clean: clean-engine
