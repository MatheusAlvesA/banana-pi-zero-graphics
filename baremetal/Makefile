CROSS_COMPILE ?= arm-linux-gnueabihf-
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
PYTHON ?= python3

.PHONY: all check disassemble
all: blink.bin

blink.o: blink.S
	$(AS) -mcpu=cortex-a7 -o $@ $<
blink.elf: blink.o link.ld
	$(LD) --build-id=none -T link.ld -Map=blink.map -o $@ blink.o
blink.raw: blink.elf
	$(OBJCOPY) -O binary $< $@
blink.bin: blink.raw image.py
	$(PYTHON) image.py $< $@
check: blink.bin
	$(PYTHON) image.py blink.bin
disassemble: blink.elf
	$(OBJDUMP) -d -m armv7 $<
