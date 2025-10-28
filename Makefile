ARCH = x86_64
CC = gcc
LD = ld
OBJCOPY = objcopy

EFIINC = /usr/include/efi
EFILIB = /usr/lib
BUILD_DIR = build
BOOT_DIR = $(BUILD_DIR)/EFI/BOOT

TARGET = BOOTX64.EFI
BOOTLOADER_SRC = bootloader/main.c

EFIINCS = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
CFLAGS = $(EFIINCS) -fno-stack-protector -fpic -fshort-wchar \
         -mno-red-zone -Wall -DEFI_FUNCTION_WRAPPER -O2

EFI_CRT_OBJS = $(EFILIB)/crt0-efi-$(ARCH).o
EFI_LDS = $(EFILIB)/elf_$(ARCH)_efi.lds

LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
          -Bsymbolic -L $(EFILIB) $(EFI_CRT_OBJS)

all: $(BUILD_DIR)/$(TARGET) disk

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BOOT_DIR)

$(BUILD_DIR)/bootloader.o: $(BOOTLOADER_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bootx64.so: $(BUILD_DIR)/bootloader.o
	$(LD) $(LDFLAGS) $< -o $@ -lefi -lgnuefi

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/bootx64.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic \
	           -j .dynsym -j .rel -j .rela -j .reloc \
	           --target=efi-app-$(ARCH) $< $@
	cp $@ $(BOOT_DIR)/$(TARGET)

disk: $(BUILD_DIR)/$(TARGET)
	dd if=/dev/zero of=$(BUILD_DIR)/rgos.img bs=1M count=128
	mkfs.fat -F 32 $(BUILD_DIR)/rgos.img
	mmd -i $(BUILD_DIR)/rgos.img ::/EFI
	mmd -i $(BUILD_DIR)/rgos.img ::/EFI/BOOT
	mcopy -i $(BUILD_DIR)/rgos.img $(BOOT_DIR)/$(TARGET) ::/EFI/BOOT/

run: disk
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
	                   -drive file=$(BUILD_DIR)/rgos.img,format=raw \
	                   -m 512M
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean disk