CC = gcc
ASM = nasm
LD = ld

CFLAGS = -ffreestanding -nostdlib -mno-red-zone -m64 -g -I /usr/include/efi/ -I /usr/include/efi/x86_64/ -DGNU_EFI_USE_MS_ABI

LDFLAGS = -nostdlib -static -T linker.ld

kernel.bin: boot.o kernel.o
	$(LD) $(LDFLAGS) $^ -o $@

boot.o: boot.asm
	$(ASM) -f elf64 $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o kernel.bin
