# BTstack Windows H4 Port - Build Instructions

This document provides comprehensive build instructions for the BTstack Windows H4 port, including debug builds, optimized release builds, and packaging.

## Prerequisites

### Visual Studio 2022
- Visual Studio 2022 (Community Edition or higher)
- CMake support in Visual Studio (installed by default)


## Build Types

### Debug Build (Default)
Debug builds include debugging symbols and no optimization:
```bash
cd port/windows-h4
mkdir build
cd build
cmake ..
make
```

### Release Build
Release builds are optimized for performance with `-O3 -DNDEBUG`:
```bash
cd port/windows-h4
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Command Line Options

All built executables support these command line options:

```bash
Usage: <executable> [options]
Options:
  -u <device>     COM port (COM6, COM15, or \\.\COM15 format)
  -b <baudrate>   UART baudrate (default: 500000)
  -f <0|1>        Flow control: 0=disabled, 1=enabled (default: 0)
  -d <path>       HCI dump file path (default: hci_dump.pklg)
  -h              Show this help message

COM Port Format:
  COM1-COM9:      Use 'COM6' format
  COM10+:         Use 'COM15' or '\\.\COM15' format

Examples:
  a2dp_sink_demo.exe -u COM6 -b 115200
  a2dp_sink_demo.exe -u COM15 -f 0
  a2dp_sink_demo.exe -u \\.\COM3 -d my_dump.pklg
```

## Packaging Release Builds

### Create Release Package
After building in Release mode, create a zip package:
```bash
cmake --build . --target zip_release
```

This creates `btstack-windows-h4-release.zip` containing all example executables.

### Using Visual Studio
1. Open `port/windows-h4/CMakeLists.txt` in Visual Studio
2. Select "Release" configuration
3. Build → Build All
4. Open Terminal in Visual Studio
5. Run: `cmake --build . --target zip_release`

### Using Command Line (Visual Studio Dev PowerShell)
```bash
cd port/windows-h4
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cmake --build . --target zip_release
```

## Build Targets

### Standard Targets
- `make` or `cmake --build .` - Build all examples
- `make <example>` - Build specific example (e.g., `make a2dp_sink_demo`)

### Special Targets
- `zip_release` - Package all executables into release zip file
- `cleanall` - Remove entire build directory (requires reconfigure)

```bash
# Package release build
cmake --build . --target zip_release

# Complete clean (removes build directory)
cmake --build . --target cleanall
```

## Example Usage

### Basic A2DP Sink Demo
```bash
# Default settings (COM15, 500000 baud, no flow control)
a2dp_sink_demo.exe

# Custom COM port and baudrate
a2dp_sink_demo.exe -u COM6 -b 115200

# High-speed connection with flow control
a2dp_sink_demo.exe -u COM15 -b 921600 -f 1
```

### Console Input Support

#### MSYS2 Shell
Console input doesn't work directly in MSYS2. Use WinPTY:
```bash
winpty ./a2dp_sink_demo.exe
```

#### CMD.exe or PowerShell
Console input works directly:
```cmd
a2dp_sink_demo.exe
```

## Output Files

- **Executables**: Built in `build/` directory (or `build/Release/` for Visual Studio)
- **HCI Logs**: `hci_dump.pklg` (or custom path with `-d` option)
- **Release Package**: `btstack-windows-h4-release.zip`

## Troubleshooting

### Build Issues
- **Missing conio.h**: Use 64-bit MSYS2 with 64-bit toolchain
- **CMake not found**: Install cmake package in MSYS2
- **Compiler errors**: Ensure mingw-w64-x86_64-toolchain is installed

### Runtime Issues
- **COM port access denied**: Check port is not in use by other applications
- **Connection failed**: Verify COM port number and baudrate settings
- **Console input not working**: Use WinPTY in MSYS2 or try CMD.exe

### COM Port Detection
- **COM1-COM9**: Use simple format `COM6`
- **COM10+**: Use extended format `COM15` or `\\.\COM15`
- **Check Device Manager**: Verify actual COM port assignment

## Advanced Configuration

### Custom Chipset Support
The build automatically detects and configures supported chipsets:
- Broadcom/Cypress/Infineon (BCM)
- Texas Instruments (CC256x)
- Cambridge Silicon Radio (CSR)
- And others...

### PortAudio Support
If PortAudio is detected, audio playback support is automatically enabled:
```bash
# In MSYS2
pacman -S mingw-w64-x86_64-portaudio
```

## File Structure
```
port/windows-h4/
├── CMakeLists.txt          # Main build configuration
├── README.md               # General port information
├── readme.build.md         # This build guide
├── main.c                  # Main entry point with command line parsing
├── btstack_config.h        # BTstack configuration
└── build/                  # Build output directory
    ├── *.exe               # Example executables
    ├── hci_dump.pklg      # HCI packet log
    └── btstack-windows-h4-release.zip  # Release package
```