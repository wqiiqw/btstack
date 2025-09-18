# Build Instructions

## Build Configuration

This project supports both Debug and Release build configurations.

### Debug Build (Default)
```bash
# Configure for Debug build
cmake -B build

# Build all targets
cmake --build build
```

### Release Build
```bash
# Configure for Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build
```

## Visual Studio Builds

For Visual Studio generator:

### Debug Build
```bash
# Configure
cmake -B build

# Build Debug configuration
cmake --build build --config Debug
```

### Release Build
```bash
# Configure
cmake -B build

# Build Release configuration
cmake --build build --config Release
```

## Creating Release Package

After building in Release mode, you can create a zip package containing all executables:

### For Make/Ninja generators
```bash
cmake --build build --target zip_release
```

### For Visual Studio
```bash
cmake --build build --target zip_release --config Release
```

This will create `btstack-windows-release.zip` in the build directory containing all example executables.

## Cleaning Build Files

### Clean Build Objects
```bash
# Clean build objects (keeps configuration)
cmake --build build --target clean
```

### Clean Everything
```bash
# Remove entire build directory (requires reconfiguration)
cmake --build build --target cleanall
```

After `cleanall`, you'll need to reconfigure:
```bash
cmake -B build
```

## Requirements

- CMake 3.12 or higher
- Visual Studio or compatible C compiler
- 7z or zip utility (for packaging)
- Python (for GATT file processing)

## Output

- **Debug builds**: Executables in `build/` or `build/Debug/`
- **Release builds**: Executables in `build/` or `build/Release/`
- **Release package**: `btstack-windows-release.zip` in `build/` directory