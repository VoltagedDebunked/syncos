# Nuke built-in rules and variables.
MAKEFLAGS += -rR
.SUFFIXES:

# Convenience macro to reliably declare user overridable variables.
override USER_VARIABLE = $(if $(filter $(origin $(1)),default undefined),$(eval override $(1) := $(2)))

# Default user QEMU flags. These are appended to the QEMU command calls.
$(call USER_VARIABLE,QEMUFLAGS,-m 2G -serial stdio)

override IMAGE_NAME := SyncOS-x86_64

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: all-hdd
all-hdd: $(IMAGE_NAME).hdd

.PHONY: run
run: run-x86_64

.PHONY: run-hdd
run-hdd: run-hdd-x86_64

.PHONY: run-x86_64
run-x86_64: ovmf/ovmf-code-x86_64.fd $(IMAGE_NAME).iso
	qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		-device nvme,id=nvme0,serial=nvme-1 \
		-drive file=img/nvme.qcow2,if=none,id=nvmedisk0,format=raw \
		-device nvme-ns,drive=nvmedisk0,nsid=1 \
		$(QEMUFLAGS)

run-sata: ovmf/ovmf-code-x86_64.fd $(IMAGE_NAME).iso
	qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
	    -drive file=img/sata.qcow2,if=none,id=sata_disk \
    	-device ahci,id=ahci0 \
    	-device ide-hd,drive=sata_disk,bus=ahci0.0 \
		$(QEMUFLAGS)

run-debug: ovmf/ovmf-code-x86_64.fd $(IMAGE_NAME).iso
	qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		-device nvme,id=nvme0,serial=nvme-1 \
		-drive file=img/nvme.qcow2,if=none,id=nvmedisk0,format=raw \
		-device nvme-ns,drive=nvmedisk0,nsid=1 \
		-s -S \
		$(QEMUFLAGS)

.PHONY: run-hdd-x86_64
run-hdd-x86_64: ovmf/ovmf-code-x86_64.fd $(IMAGE_NAME).hdd
	qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		-device nvme,id=nvme0,serial=nvme-1 \
		-drive file=img/nvme.qcow2,if=none,id=nvmedisk0,format=raw \
		-device nvme-ns,drive=nvmedisk0,nsid=1 \
		$(QEMUFLAGS)

.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	qemu-system-x86_64 \
		-M q35 \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		-device nvme,id=nvme0,serial=nvme-1 \
		-drive file=img/nvme.qcow2,if=none,id=nvmedisk0,format=raw \
		-device nvme-ns,drive=nvmedisk0,nsid=1 \
		$(QEMUFLAGS)

.PHONY: run-hdd-bios
run-hdd-bios: $(IMAGE_NAME).hdd
	qemu-system-x86_64 \
		-M q35 \
		-hda $(IMAGE_NAME).hdd \
		-device nvme,id=nvme0,serial=nvme-1 \
		-drive file=img/nvme.qcow2,if=none,id=nvmedisk0,format=raw \
		-device nvme-ns,drive=nvmedisk0,nsid=1 \
		$(QEMUFLAGS)

ovmf/ovmf-code-x86_64.fd:
	mkdir -p ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-x86_64.fd

limine/limine:
	rm -rf limine
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	$(MAKE) -C limine

kernel-deps:
	./src/get-deps
	touch kernel-deps

.PHONY: kernel
kernel: kernel-deps
	$(MAKE) -C src

$(IMAGE_NAME).iso: limine/limine kernel
	rm -rf iso_root
	mkdir -p iso_root/boot
	cp -v src/bin-x86_64/kernel iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v config/limine.conf iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./limine/limine bios-install $(IMAGE_NAME).iso
	rm -rf iso_root

$(IMAGE_NAME).hdd: limine/limine kernel
	rm -f $(IMAGE_NAME).hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).hdd
	sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00
	./limine/limine bios-install $(IMAGE_NAME).hdd
	mformat -i $(IMAGE_NAME).hdd@@1M
	mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M src/bin-x86_64/kernel ::/boot
	mcopy -i $(IMAGE_NAME).hdd@@1M config/limine.conf ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/limine-bios.sys ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: clean
clean:
	$(MAKE) -C src clean
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	$(MAKE) -C src distclean
	rm -rf iso_root *.iso *.hdd kernel-deps limine ovmf