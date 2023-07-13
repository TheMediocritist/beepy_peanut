ifeq ($(HEAP_SIZE),)
HEAP_SIZE      = 8388208
endif
ifeq ($(STACK_SIZE),)
STACK_SIZE     = 61800
endif

ifeq ($(PRODUCT),)
PRODUCT = peanutGB_Linux
endif

SELF_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

# List C source files here
SRC += src/main.c \
	src/rom_list.c \
	src/minigb_apu/minigb_apu.c

# List all user directories here
UINCDIR += $(SELF_DIR)/src

# List all user C defines here, like -D_DEBUG=1
UDEFS += -DENABLE_SOUND -DENABLE_SOUND_MINIGB

# Define ASM defines here
UADEFS +=

# List the user directory to look for the libraries here
ULIBDIR +=

# List all user libraries here
ULIBS +=

CLANGFLAGS += -fsingle-precision-constant -Wdouble-promotion

CFLAGS += $(CLANGFLAGS)

$(PRODUCT): $(SRC)
	$(CC) $(CFLAGS) $(UDEFS) $(UINCDIR:%=-I%) $(LDFLAGS) -o $@ $^ $(ULIBDIR:%=-L%) $(ULIBS:%=-l%)

.PHONY: clean
clean:
	$(RM) $(PRODUCT)
