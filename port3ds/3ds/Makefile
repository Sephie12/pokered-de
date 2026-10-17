#---------------------------------------------------------------------------------
# 3DS-Build fuer pokered-de (nativer Port). Basiert auf dem devkitPro-3DS-Template.
# Normalerweise ueber ../Makefile aufrufen - dort werden ROM, Recompilat und
# syms.h vorher erzeugt.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM ist nicht gesetzt. Bitte devkitPro installieren: https://devkitpro.org/wiki/Getting_Started")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := pokered-de
BUILD       := build
SOURCES     := source ../core ../ui ../build/gen
DATA        := data
INCLUDES    := ../core ../ui ../build
GRAPHICS    :=
ROMFS       :=

APP_TITLE       := Pokemon Rote Edition
APP_DESCRIPTION := Nativer 3DS-Port (pokered-de)
APP_AUTHOR      := Sephie12
ICON            := meta/icon.png

#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fno-strict-aliasing \
           -Wno-unused-label -Wno-unused-but-set-variable -Wdeclaration-after-statement \
           $(ARCH)
CFLAGS  += $(INCLUDE) -D__3DS__ -std=gnu11

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD           := $(CC)
export OFILES_BIN   := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CFILES:.c=.o)
export OFILES       := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES       := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-isystem $(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf:  $(OFILES)

$(OFILES_SOURCES): $(HFILES)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
