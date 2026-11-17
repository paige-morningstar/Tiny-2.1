#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET		:=	tiny21
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	include
ROMFS		:=	romfs
APP_TITLE	:=	TINY 2.1
APP_DESCRIPTION	:=	A tiny question-answering transformer
APP_AUTHOR	:=	Fable 5.2

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
		-ffunction-sections \
		$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=	-lctru -lm

#---------------------------------------------------------------------------------
LIBDIRS	:=	$(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD	:=	$(CC)
export OFILES	:=	$(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

ifneq ($(ROMFS),)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

#---------------------------------------------------------------------------------
# an installable title (.cia) needs makerom, and bannertool only to rebuild the
# banner and icon in assets/.  Neither ships with devkitARM.
#---------------------------------------------------------------------------------
MAKEROM		?=	makerom
BANNERTOOL	?=	bannertool
RSF		:=	tiny.rsf
BANNER		:=	assets/banner.bnr
ICON		:=	assets/icon.icn

.PHONY: all clean cia gfx

#---------------------------------------------------------------------------------
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

#---------------------------------------------------------------------------------
cia: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $(TARGET).elf
	$(MAKEROM) -f cia -target t -o $(TARGET).cia -elf $(TARGET).elf \
		-rsf $(RSF) -icon $(ICON) -banner $(BANNER)

#---------------------------------------------------------------------------------
gfx:
	python3 tools/make_gfx.py
	$(BANNERTOOL) makebanner -i assets/banner.png -a assets/banner.wav -o $(BANNER)
	$(BANNERTOOL) makesmdh -s "TINY 2.1" -l "A tiny question answering transformer" \
		-p "Fable 5.2" -i assets/icon.png -o $(ICON)

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES)	:	$(TOPDIR)/include/tiny_model.h

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
