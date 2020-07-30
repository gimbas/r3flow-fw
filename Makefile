# App Config
DEVICEDIR = /opt/cmsis/cmsis-dfp-efm32gg11b/Device/SiliconLabs/EFM32GG11B/Include
COREDIR = /opt/cmsis/cmsis-core/CMSIS/Include
LVGL_DIR = ${shell pwd}/src
MCU_TYPE = EFM32GG11B420F2048GQ100
HFXO_VALUE = 8000000UL
LFXO_VALUE = 32768UL
APP_ADDRESS = 0x00000000
APP_NAME = R3flow_Firmware

# Multiprocessing
MAX_PARALLEL = 12

# Directories
TARGETDIR = bin
SOURCEDIR = src
OBJECTDIR = bin/obj
INCLUDEDIR = include

EXCLUDES = src/lvgl src/lvgl/docs src/lvgl/porting src/lvgl/scripts src/lvgl/scripts/built_in_font src/lvgl/src src/lvgl/.github src/lvgl/.github/ISSUE_TEMPLATE

STRUCT := $(shell find $(SOURCEDIR) -type d)
STRUCT := $(filter-out $(EXCLUDES), $(STRUCT))

SOURCEDIRSTRUCT := $(filter-out %/include, $(STRUCT))
INCLUDEDIRSTRUCT := $(filter %/include, $(STRUCT)) $(DEVICEDIR)/ $(COREDIR)/
OBJECTDIRSTRUCT := $(subst $(SOURCEDIR), $(OBJECTDIR), $(SOURCEDIRSTRUCT))

# Build type
BUILD_TYPE ?= debug

# Compillers & Linker
CC = arm-none-eabi-gcc
CXX = arm-none-eabi-g++
LD = arm-none-eabi-gcc
AS = arm-none-eabi-as
STRIP = arm-none-eabi-strip
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
GDB = arm-none-eabi-gdb

# Compillers & Linker flags
OPTIMIZATION = -O3
ASFLAGS = -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS = $(addprefix -I,$(INCLUDEDIRSTRUCT)) -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -nostdlib -nostartfiles -ffunction-sections -fdata-sections -ffreestanding $(OPTIMIZATION) -std=gnu99 -Wpointer-arith -Wundef -Werror -D$(MCU_TYPE) -DHFXO_VALUE=$(HFXO_VALUE) -DLFXO_VALUE=$(LFXO_VALUE)
CXXFLAGS = $(addprefix -I,$(INCLUDEDIRSTRUCT)) -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -nostdlib -nostartfiles -ffunction-sections -fdata-sections -ffreestanding -fno-rtti -fno-exceptions $(OPTIMIZATION) -std=c++17 -Wpointer-arith -Wundef -Werror -D$(MCU_TYPE) -DHFXO_VALUE=$(HFXO_VALUE) -DLFXO_VALUE=$(LFXO_VALUE) -DBUILD_VERSION=$(BUILD_VERSION)
LDFLAGS = -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs --specs=nosys.specs -nostdlib -nostartfiles -ffunction-sections -fdata-sections -ffreestanding -Wl,--gc-sections
LDLIBS = -lm -lc -lgcc -lnosys

ifeq ($(BUILD_TYPE), debug)
CFLAGS += -g
CXXFLAGS += -g
endif

## Linker scripts
LDSCRIPT = ld/efm32gg11bx20f2048_app.ld

# Target
TARGET = $(TARGETDIR)/$(APP_NAME)

# Sources & objects
SRCFILES := $(addsuffix /*, $(SOURCEDIRSTRUCT))
SRCFILES := $(wildcard $(SRCFILES))

ASSOURCES := $(filter %.s, $(SRCFILES))
ASOBJECTS := $(subst $(SOURCEDIR), $(OBJECTDIR), $(ASSOURCES:%.s=%.o))

CSOURCES := $(filter %.c, $(SRCFILES))
COBJECTS := $(subst $(SOURCEDIR), $(OBJECTDIR), $(CSOURCES:%.c=%.o))

CXXSOURCES := $(filter %.cpp, $(SRCFILES))
CXXOBJECTS := $(subst $(SOURCEDIR), $(OBJECTDIR), $(CXXSOURCES:%.cpp=%.o))

SOURCES = $(ASSOURCES) $(CSOURCES) $(CXXSOURCES)
OBJECTS = $(ASOBJECTS) $(COBJECTS) $(CXXOBJECTS)

all: clean-bin make-dir version compile mem-usage

compile:
	@$(MAKE) INC_VERSION=n --no-print-directory -j${MAX_PARALLEL} $(TARGET).elf
	@$(MAKE) INC_VERSION=n --no-print-directory -j${MAX_PARALLEL} $(TARGET).bin $(TARGET).hex $(TARGET).lss

$(TARGET).lss: $(TARGET).elf
	@echo Creating LSS file \'$@\'...
	@$(OBJDUMP) -S --disassemble $< > $@

$(TARGET).bin: $(TARGET).elf
	@echo Creating BIN file \'$@\'...
	@$(OBJCOPY) -O binary --only-section=.isr_vector --only-section=.text --only-section=.ARM --only-section=.iram0.text --only-section=.data $< $(TARGET).irom0.bin
	@$(OBJCOPY) -O binary --only-section=.irom1.text $< $(TARGET).irom1.bin
	@$(OBJCOPY) -O binary --only-section=.irom2.text $< $(TARGET).irom2.bin
	@$(OBJCOPY) -O binary --only-section=.drom0.data $< $(TARGET).drom0.bin
	@$(OBJCOPY) -O binary --only-section=.drom1.data $< $(TARGET).drom1.bin

$(TARGET).hex: $(TARGET).elf
	@echo Creating HEX file \'$@\'...
	@$(OBJCOPY) -O ihex --remove-section=.irom2.text --remove-section=.drom1.data $< $@
	@$(OBJCOPY) -O ihex --only-section=.isr_vector --only-section=.text --only-section=.ARM --only-section=.iram0.text --only-section=.data $< $(TARGET).flash.hex
	@$(OBJCOPY) -O ihex --only-section=.irom1.text $< $(TARGET).boot.hex
	@$(OBJCOPY) -O ihex --only-section=.drom0.data $< $(TARGET).userdata.hex
	@$(OBJCOPY) -O ihex --only-section=.irom2.text --only-section=.drom1.data $< $(TARGET).qspi.hex

$(TARGET).elf: $(OBJECTS)
	@echo ---------------------------------------------------------------------------
	@echo Creating ELF file \'$@\'...
	@$(LD) $(LDFLAGS) -o $@ $^ -T $(LDSCRIPT) $(LDLIBS) -Wl,-Map=$(TARGET).map
ifeq ($(BUILD_TYPE), release)
	@$(STRIP) -g $@
endif

$(OBJECTDIR)/%.o: $(SOURCEDIR)/%.s
	@echo Compilling ASM file \'$<\' \> \'$@\'...
	@$(AS) $(ASFLAGS) -MD -o $@ $<

$(OBJECTDIR)/%.o: $(SOURCEDIR)/%.c
	@echo Compilling C file \'$<\' \> \'$@\'...
	@$(CC) $(CFLAGS) -MD -c -o $@ $<

$(OBJECTDIR)/%.o: $(SOURCEDIR)/%.cpp
	@echo Compilling C++ file \'$<\' \> \'$@\'...
	@$(CXX) $(CXXFLAGS) -MD -c -o $@ $<

debug: $(TARGET).elf
	$(GDB) $(TARGET).elf

mem-usage: $(TARGET).elf
	@echo ---------------------------------------------------------------------------
	@armmem -l $(LDSCRIPT) -d -h $<

make-dir:
	@mkdir -p $(OBJECTDIRSTRUCT)

clean-bin:
	@rm -f $(TARGETDIR)/*.lss
	@rm -f $(TARGETDIR)/*.hex
	@rm -f $(TARGETDIR)/*.bin
	@rm -f $(TARGETDIR)/*.map
	@rm -f $(TARGETDIR)/*.elf

clean: clean-bin
	@rm -rf $(OBJECTDIR)/*

-include $(OBJECTS:.o=.d)

.PHONY: clean clean-bin make-dir mem-usage version dec-version inc-version debug compile all