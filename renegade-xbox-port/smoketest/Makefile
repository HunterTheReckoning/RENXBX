# Renegade Xbox port: foundation smoke test.
#
#   make CODE=/workspaces/RENXBX/Code
#
# Builds the foundation libraries into renegade_foundation.lib (so only what the test uses
# is linked, as in the real game), links the test, and packs bin/ into an ISO for xemu.
# Put one or more of the game's .mix/.dat files in data/ first; they are copied onto the disc.

XBE_TITLE = Renegade\ port\ smoke\ test
GEN_XISO = renegade_smoke.iso
NXDK_DIR ?= /workspaces/nxdk
NXDK_CXX = y
CODE ?= /workspaces/RENXBX/Code

SRCS = $(CURDIR)/main.cpp

# Port flags every Xbox build of the engine needs (see the package README).
PORT_INCLUDES = -I$(CODE)/wwlib -I$(CODE)/wwdebug -I$(CODE)/WWMath -I$(CODE)/wwsaveload \
                -I$(CODE)/wwbitpack -I$(CODE)/wwtranslatedb -I$(CODE)/wwutil
CXXFLAGS += -I$(CODE)/xbox/include -include $(CODE)/xbox/include/xbox_prelude.h \
            -Wno-c++11-narrowing -w $(PORT_INCLUDES)

FOUNDATION_SRCS := $(shell python3 $(CURDIR)/../tools/xbox_sources.py $(CODE) foundation)
FOUNDATION_OBJS := $(addsuffix .obj,$(basename $(FOUNDATION_SRCS)))
FOUNDATION_LIB  := $(CURDIR)/renegade_foundation.lib

include $(NXDK_DIR)/Makefile

# Link the foundation as a library: the linker pulls in only the objects the test needs.
main.exe: $(FOUNDATION_LIB)

$(FOUNDATION_LIB): $(FOUNDATION_OBJS)
	@echo "[ LIB      ] $@"
	$(VE) $(LIB) -out:'$@' $^

# EA's source has one upper-case extension (wwlib/TARGA.CPP).
%.obj: %.CPP
	@echo "[ CXX      ] $@"
	$(VE) $(CXX) $(NXDK_CXXFLAGS) $(CXXFLAGS) -c -o '$@' '$<'

# Copy the game archives onto the disc before the ISO is made.
.PHONY: copy-data
copy-data: $(OUTPUT_DIR)
	@cp -f $(CURDIR)/data/*.mix $(CURDIR)/data/*.dat $(CURDIR)/data/*.MIX $(CURDIR)/data/*.DAT \
	    $(OUTPUT_DIR)/ 2>/dev/null || true
$(GEN_XISO): copy-data

.PHONY: clean-foundation
clean-foundation:
	$(VE) rm -f $(FOUNDATION_OBJS) $(FOUNDATION_LIB)
clean: clean-foundation
