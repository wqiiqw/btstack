
```
cd btstack\port\windows-h4-zephyr\
```
  1. Configure for Release build:
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  2. Build the project:
  cmake --build build --config Release
  3. Create the release zip:
  cmake --build build --target release_zip --config Release

```
dir .\build\*.zip

btstack-windows-h4-zephyr-release.zip

```