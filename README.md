# SyncOS

A modern, robust operating system kernel for x86-64 built with reliability and synchronization in mind.

![License: GPL-3.0](https://img.shields.io/badge/License-GPL%203.0-blue.svg)

## Overview

SyncOS is a minimalist kernel designed specifically for the x86-64 architecture. It provides basic kernel functionality with a focus on proper synchronization primitives and hardware abstraction.

## Features

- **x86-64 architecture**: Optimized for modern 64-bit Intel/AMD processors
- **Modular design**: Clean separation of kernel components
- **UEFI boot**: Uses Limine bootloader for UEFI booting
- **BIOS fallback**: Optional legacy BIOS boot support
- **Robust memory management**: Properly aligned and protected kernel structures
- **Interrupt handling**: Complete IDT setup with exception handlers
- **Serial I/O**: Fully functional serial port communication
- **Standard library**: Basic kernel-level standard library implementation

## Project Structure

- `/src`: Contains the kernel source code
  - `/src/core`: Core kernel functionality
  - `/src/syncos`: OS-specific components (GDT, IDT, serial, etc.)
  - `/src/kstd`: Kernel standard library implementation
- `/config`: Configuration files for building and booting
- `/limine`: Limine bootloader files (downloaded during build)
- `/ovmf`: OVMF firmware files for UEFI (downloaded during build)

## Building

### Prerequisites

To build SyncOS, you'll need:

- GCC or Clang with target architecture support
- NASM (for x86_64 assembly)
- xorriso
- mtools
- curl
- git
- make

### Build Commands

Build the kernel:

```bash
make
```

### Build Targets

- `make all`: Build an ISO image
- `make all-hdd`: Build a hard disk image
- `make clean`: Remove build artifacts
- `make distclean`: Remove all generated files, including dependencies

## Running

### Using QEMU

Run with default settings:

```bash
make run
```

Run with custom QEMU flags:

```bash
make run QEMUFLAGS="-m 4G -smp 4"
```

### Debug Mode

Start QEMU in debug mode and wait for GDB connection:

```bash
make run-debug
```

Then connect with GDB:

```bash
gdb -ex "target remote localhost:1234" src/bin-x86_64/kernel
```

## Development

### Adding New Features

1. Implement your feature in the appropriate directory
2. Update the main kernel to initialize your feature
3. Make sure to follow x86-64 calling conventions and memory layout

## License

SyncOS is licensed under the GNU General Public License v3.0 (GPL-3.0). See the LICENSE file for details.

## Contact

Maintainer: Voltaged  
Repository: https://github.com/voltageddebunked/syncos