
# Build EFI app source given by FILENAME variable.
# For details about usage, see README.md.

ifndef FILENAME
$(error FILENAME must be set)
endif

GNUEFIDIR = ./gnu-efi
OVMF_DISK_DIR = ./ovmf-disk
BIOS_BIN = ./bios.bin

CFLAGS = -I$(GNUEFIDIR)/inc -fpic -ffreestanding -fno-stack-protector \
	     -fno-stack-check -fshort-wchar -mno-red-zone \
		 -maccumulate-outgoing-args -c $(FILENAME).c -o $(FILENAME).o

LDFLAGS = -shared -Bsymbolic -L$(GNUEFIDIR)/x86_64/lib \
		  -L$(GNUEFIDIR)/x86_64/gnuefi \
		  -T$(GNUEFIDIR)/gnuefi/elf_x86_64_efi.lds \
		  $(GNUEFIDIR)/x86_64/gnuefi/crt0-efi-x86_64.o $(FILENAME).o \
		  -o $(FILENAME).so -lgnuefi -lefi

OBJCOPY_FLAGS = -j .text -j .sdata -j .data -j .rodata -j .dynamic -j \
				.dynsym  -j .rel -j .rela -j .rel.* -j .rela.* -j \
				.reloc --output-target efi-app-x86_64 --subsystem=10 \
				$(FILENAME).so $(FILENAME).efi

QEMU_FLAGS = -bios $(BIOS_BIN) -net none \
  			 -drive file=fat:rw:$(OVMF_DISK_DIR),format=raw,if=none,id=drive0 \
      		 -device ide-hd,drive=drive0,bus=ide.0,unit=0

all: $(FILENAME).o $(FILENAME).so $(FILENAME).efi

$(FILENAME).o: $(FILENAME).c
	gcc $(CFLAGS)

$(FILENAME).so: $(FILENAME).o
	ld $(LDFLAGS)

$(FILENAME).efi: $(FILENAME).so
	objcopy $(OBJCOPY_FLAGS)

emu:
	qemu-system-x86_64 $(QEMU_FLAGS)

clean:
	rm $(FILENAME).o
	rm $(FILENAME).so
	rm $(FILENAME).efi
