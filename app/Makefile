#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#---------------------------------------------------------------------------------
TARGET		:=	hpak
BUILD		:=	build

BOREALIS	:=	../libs/borealis-switch2/library
BOREALIS_LIB	:=	$(BOREALIS)/lib

LIBGET		:=	../libs/libget

SOURCES		:=	source source/activity source/store source/tab source/view \
			$(BOREALIS_LIB)/core \
			$(BOREALIS_LIB)/core/touch \
			$(BOREALIS_LIB)/views \
			$(BOREALIS_LIB)/views/cells \
			$(BOREALIS_LIB)/views/widgets \
			$(BOREALIS_LIB)/platforms/switch \
			$(BOREALIS_LIB)/extern/nanovg \
			$(BOREALIS_LIB)/extern/nanovg/deko3d \
			$(BOREALIS_LIB)/extern/nanovg/deko3d/framework \
			$(BOREALIS_LIB)/extern/glad \
			$(BOREALIS_LIB)/extern/tinyxml2 \
			$(BOREALIS_LIB)/extern/fmt/src \
			$(BOREALIS_LIB)/extern/yoga/yoga \
			$(BOREALIS_LIB)/extern/yoga/yoga/event \
			$(BOREALIS_LIB)/extern/libretro-common/features \
			$(BOREALIS_LIB)/extern/libretro-common/encodings \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/archive \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/bfgrp \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/bfsar \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/bfwar \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/bfwav \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/bfwsd \
			$(BOREALIS_LIB)/extern/switch-libpulsar/src/player \
			$(LIBGET)/src \
			$(LIBGET)/src/repos \
			$(LIBGET)/src/libs/minizip \
			$(LIBGET)/src/libs/tinyxml

DATA		:=	data
INCLUDES	:=	include \
			$(BOREALIS)/include \
			$(BOREALIS)/include/borealis/extern \
			$(BOREALIS)/include/borealis/extern/nanovg \
			$(BOREALIS)/include/borealis/extern/tinyxml2 \
			$(BOREALIS_LIB)/extern/switch-libpulsar/include \
			$(BOREALIS_LIB)/extern/fmt/include \
			$(BOREALIS_LIB)/extern/yoga \
			$(BOREALIS_LIB)/extern/tweeny/include \
			$(LIBGET)/src \
			$(LIBGET)/src/libs/rapidjson/include \
			$(LIBGET)/src/libs/minizip \
			$(LIBGET)/src/libs/tinyxml

ROMFS		:=	romfs

# Nanovg (deko3d/drako3d) shaders required by Borealis on Switch
BOREALIS_SHADERS	:=	$(BOREALIS_LIB)/extern/nanovg/deko3d/shaders
OUT_SHADERS		:=	shaders

APP_TITLE	:=	hpak
APP_AUTHOR	:=	ouncebrew
APP_VERSION	:=	1.0.0
ICON		:=	icon.jpg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	$(SWITCH2_CPU) -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -D__SWITCH2__ -DHAVE_LIBNX \
			-DBOREALIS_USE_DEKO3D -DSTBI_NO_THREAD_LOCALS \
			-DBRLS_RESOURCES=\"romfs:/\" -DYG_ENABLE_EVENTS -DIOAPI_NO_64

# Borealis requires C++17 and RTTI/exceptions enabled.
CXXFLAGS	:= $(CFLAGS) -std=gnu++17 -Wno-volatile -Wno-psabi

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libounce/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# drako3d is API-compatible with deko3d on Switch 2 (libounce)
LIBS	:= -ldrako3d -lcurl -lz -lmbedtls -lmbedx509 -lmbedcrypto -lnx -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(CURDIR)/$(BOREALIS_SHADERS)

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CCFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cc)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GLSLFILES	:=	$(notdir $(wildcard $(BOREALIS_SHADERS)/*.glsl))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

# fmt ships fmt.cc (C++20 module) alongside format.cc/os.cc; exclude it for this build.
CPPFILES	:=	$(filter-out fmt.cc,$(CPPFILES))
CCFILES		:=	$(filter-out fmt.cc,$(CCFILES))
CFILES		:=	$(filter-out fmt.cc,$(CFILES))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES) $(CCFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CCFILES:.cc=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

ifneq ($(strip $(ROMFS)),)
	ROMFS_TARGETS :=
	ROMFS_FOLDERS :=
	ifneq ($(strip $(OUT_SHADERS)),)
		ROMFS_SHADERS := $(ROMFS)/$(OUT_SHADERS)
		ROMFS_TARGETS += $(patsubst %.glsl, $(ROMFS_SHADERS)/%.dksh, $(GLSLFILES))
		ROMFS_FOLDERS += $(ROMFS_SHADERS)
	endif

	export ROMFS_DEPS := $(foreach file,$(ROMFS_TARGETS),$(CURDIR)/$(file))
endif

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

#---------------------------------------------------------------------------------
all: $(ROMFS_TARGETS) | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(strip $(ROMFS_TARGETS)),)

$(ROMFS_TARGETS): | $(ROMFS_FOLDERS)

$(ROMFS_FOLDERS):
	@mkdir -p $@

$(ROMFS_SHADERS)/%_vsh.dksh: %_vsh.glsl
	@echo {vert} $(notdir $<)
	@uam2 -s vert -o $@ $<

$(ROMFS_SHADERS)/%_tcsh.dksh: %_tcsh.glsl
	@echo {tess_ctrl} $(notdir $<)
	@uam2 -s tess_ctrl -o $@ $<

$(ROMFS_SHADERS)/%_tesh.dksh: %_tesh.glsl
	@echo {tess_eval} $(notdir $<)
	@uam2 -s tess_eval -o $@ $<

$(ROMFS_SHADERS)/%_gsh.dksh: %_gsh.glsl
	@echo {geom} $(notdir $<)
	@uam2 -s geom -o $@ $<

$(ROMFS_SHADERS)/%_fsh.dksh: %_fsh.glsl
	@echo {frag} $(notdir $<)
	@uam2 -s frag -o $@ $<

$(ROMFS_SHADERS)/%.dksh: %.glsl
	@echo {comp} $(notdir $<)
	@uam2 -s comp -o $@ $<

endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(ROMFS_FOLDERS) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
	@rm -fr $(BUILD) $(ROMFS_FOLDERS) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
endif


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_DEPS)
else
$(OUTPUT).nro	:	$(OUTPUT).elf $(ROMFS_DEPS)
endif

ifeq ($(strip $(NO_ICON)),)
$(OUTPUT).nro	:	$(APP_ICON)
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# rule for compiling .cc files (e.g. fmt)
#---------------------------------------------------------------------------------
%.o: %.cc
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@ $(ERROR_FILTER)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
