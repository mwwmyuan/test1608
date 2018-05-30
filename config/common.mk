
add_if_exists = $(foreach d,$(1),$(if $(wildcard $(srctree)/$(d)),$(d) ,))

# -------------------------------------------
#    # CHIP selection
# -------------------------------------------
#    
export CHIP
KBUILD_CPPFLAGS += -DCHIP_BEST1000
export CHIP_HAS_FPU := 1
export CHIP_HAS_USB := 1
export CHIP_HAS_SDMMC := 1
export CHIP_HAS_SDIO := 1
export CHIP_HAS_PSRAM := 1
export CHIP_HAS_SPI := 1
export CHIP_HAS_SPILCD := 1
export CHIP_HAS_SPIPHY := 0
export CHIP_HAS_UART := 2
export CHIP_HAS_DMA := 2
export CHIP_HAS_SPDIF := 1
export CHIP_HAS_TRANSQ := 0
export CHIP_HAS_AUDIO_CONST_ROM := 1

FLASH_SIZE ?= 0x100000
PSRAM_SIZE ?= 0x400000

ifneq ($(ROM_SIZE),)
CPPFLAGS_${LDS_FILE} += -DROM_SIZE=$(ROM_SIZE)
endif

ifneq ($(RAM_SIZE),)
CPPFLAGS_${LDS_FILE} += -DRAM_SIZE=$(RAM_SIZE)
endif

ifneq ($(FLASH_REGION_BASE),)
CPPFLAGS_${LDS_FILE} += -DFLASH_REGION_BASE=$(FLASH_REGION_BASE)
endif

ifneq ($(FLASH_REGION_SIZE),)
CPPFLAGS_${LDS_FILE} += -DFLASH_REGION_SIZE=$(FLASH_REGION_SIZE)
endif

AUD_SECTION_SIZE ?= 0

OTA_SEC_SIZE ?= 0x18000

USERDATA_SECTION_SIZE ?= 0x1000

FACTORY_SECTION_SIZE ?= 0x1000

CPPFLAGS_${LDS_FILE} += \
	-DLINKER_SCRIPT \
	-DBUILD_INFO_MAGIC=0xBE57341D \
	-DFLASH_SIZE=$(FLASH_SIZE) \
	-DPSRAM_SIZE=$(PSRAM_SIZE) \
	-DAUD_SECTION_SIZE=$(AUD_SECTION_SIZE) \
	-DUSERDATA_SECTION_SIZE=$(USERDATA_SECTION_SIZE) \
	-DFACTORY_SECTION_SIZE=$(FACTORY_SECTION_SIZE) \
	-Iplatform/hal

# -------------------------------------------
# Standard C library
# -------------------------------------------

export NOSTD

ifeq ($(NOSTD),1)

ifeq ($(MBED),1)
$(error Invalid configuratioin: MBED needs standard C library support)
endif
ifeq ($(RTOS),1)
$(error Invalid configuratioin: RTOS needs standard C library support)
endif

core-y += utils/libc/

SPECS_CFLAGS :=

LIB_LDFLAGS := $(filter-out -lstdc++ -lsupc++ -lm -lc -lgcc -lnosys,$(LIB_LDFLAGS))

KBUILD_CPPFLAGS += -nostdinc -ffreestanding -Iutils/libc/inc

CFLAGS_IMAGE += -nostdlib

CPPFLAGS_${LDS_FILE} += -DNOSTD

else # NOSTD != 1

SPECS_CFLAGS := --specs=nano.specs

LIB_LDFLAGS += -lm -lc -lgcc -lnosys

endif # NOSTD != 1

# -------------------------------------------
# RTOS library
# -------------------------------------------

export RTOS

ifeq ($(RTOS),1)

core-y += rtos/

KBUILD_CPPFLAGS += \
	-Irtos/rtos \
	-Irtos/rtx/TARGET_CORTEX_M \

OS_TASKCNT ?= 10
OS_SCHEDULERSTKSIZE ?= 512
OS_CLOCK ?= 32000
OS_FIFOSZ ?= 24

export OS_TASKCNT
export OS_SCHEDULERSTKSIZE
export OS_CLOCK
export OS_FIFOSZ

endif

# -------------------------------------------
# MBED library
# -------------------------------------------

export MBED

ifeq ($(MBED),1)

core-y += mbed/

KBUILD_CPPFLAGS += \
	-Imbed/api \
	-Imbed/common \
	-Imbed/targets/hal/TARGET_BEST/TARGET_BEST100X/TARGET_MBED_BEST1000 \
	-Imbed/targets/hal/TARGET_BEST/TARGET_BEST100X \
	-Imbed/hal \

endif

# -------------------------------------------
# DEBUG functions
# -------------------------------------------

export DEBUG

ifeq ($(DEBUG),1)

KBUILD_CFLAGS	+= -O2
KBUILD_CPPFLAGS	+= -DDEBUG

else

KBUILD_CFLAGS	+= -Os
KBUILD_CPPFLAGS	+= -DNDEBUG

endif

# -------------------------------------------
# SIMU functions
# -------------------------------------------

export SIMU

ifeq ($(SIMU),1)

KBUILD_CPPFLAGS += -DSIMU

endif

# -------------------------------------------
# FPGA functions
# -------------------------------------------

export FPGA

ifeq ($(FPGA),1)

KBUILD_CPPFLAGS += -DFPGA

endif

# -------------------------------------------
# General
# -------------------------------------------

export LIBC_ROM

core-y += $(call add_if_exists,utils/boot_struct/)

CPU_CFLAGS := -mcpu=cortex-m4 -mthumb

ifeq ($(NO_FPU),1)

CPU_CFLAGS += -mfloat-abi=soft

else

CPU_CFLAGS += -mfpu=fpv4-sp-d16 -mfloat-abi=softfp

endif

KBUILD_CPPFLAGS += $(CPU_CFLAGS) $(SPECS_CFLAGS) -DTOOLCHAIN_GCC -D__CORTEX_M4F -DTARGET_BEST1000
LINK_CFLAGS += $(CPU_CFLAGS) $(SPECS_CFLAGS)
CFLAGS_IMAGE += $(CPU_CFLAGS) $(SPECS_CFLAGS)
