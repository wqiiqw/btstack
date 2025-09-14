# BTstack LE Audio Test Suite Analysis

## Overview

The `test/le_audio` directory provides comprehensive **integration tests and examples** that demonstrate real-world usage of the LE Audio implementation from `src/le-audio`. This test suite serves as both a validation framework and reference implementation for LE Audio functionality.

## Test Architecture Overview

The test directory serves multiple purposes:

1. **Integration Test Suite** - Complete end-to-end testing of LE Audio functionality
2. **Reference Implementation** - Working examples for different LE Audio roles
3. **PTS Compliance Testing** - Tests designed for Bluetooth SIG qualification
4. **Hardware-in-Loop Testing** - Real hardware testing using Nordic nRF5340DK or similar platforms

## Test Components and Their `src/le-audio` Usage

### 1. Broadcast Source Test (`le_audio_broadcast_source.c`)

**Purpose**: Tests the broadcast source role, creating and advertising LE Audio broadcast streams.

**Primary Usage of `src/le-audio`:**
```c
#include "le-audio/le_audio_base_builder.h"  // BASE advertisement construction

// Creates BASE (Broadcast Audio Source Endpoint) advertisements
le_audio_base_builder_t builder;
le_audio_base_builder_init(&builder, period_adv_data, sizeof(period_adv_data), 20000);

// Add subgroups with specific codec configurations
le_audio_base_builder_add_subgroup(&builder, codec_id,
                                  sizeof(codec_specific_configuration),
                                  codec_specific_configuration,
                                  sizeof(metadata), metadata);

// Add BIS (Broadcast Isochronous Streams) for each channel
le_audio_base_builder_add_bis(&builder, 1, sizeof(bis_codec_specific_configuration_1),
                             bis_codec_specific_configuration_1);
le_audio_base_builder_add_bis(&builder, 2, sizeof(bis_codec_specific_configuration_2), 
                             bis_codec_specific_configuration_2);

// Get final advertisement data size
period_adv_data_len = le_audio_base_builder_get_ad_data_size(&builder);
```

**Key Features Tested:**
- Periodic advertising with BASE structure
- Multi-BIS broadcast (stereo audio support)
- LC3 codec integration with Google's LC3 implementation
- Configurable presentation delay (20ms in example)
- Broadcast ID management (0x112233 in test)
- Integration with BTstack's advertising framework
- Interoperability with Nordic LE Audio demo (configurable)

### 2. Broadcast Sink Test (`le_audio_broadcast_sink.c`)

**Purpose**: Tests the broadcast sink role, discovering and receiving LE Audio broadcast streams.

**Primary Usage of `src/le-audio`:**
```c
#include "le-audio/le_audio_util.h"                              // Utility functions
#include "le-audio/le_audio_base_parser.h"                       // Parse BASE advertisements  
#include "le-audio/gatt-service/broadcast_audio_scan_service_server.h"  // BASS server

// Parse received BASE advertisements
le_audio_base_parser_t parser;
bool ok = le_audio_base_parser_init(&parser, adv_data, adv_size);

// Extract presentation delay and subgroup information
uint32_t presentation_delay = le_audio_base_parser_get_presentation_delay(&parser);
uint8_t num_subgroups = le_audio_base_parser_get_num_subgroups(&parser);

// Iterate through subgroups
for (each subgroup) {
    num_bis = le_audio_base_parser_subgroup_get_num_bis(&parser);
    const uint8_t * codec_config = le_audio_base_parser_subgroup_get_codec_specific_configuration(&parser);
    
    // Parse codec configuration to get frame duration
    frame_duration = le_audio_util_get_btstack_lc3_frame_duration(frame_duration_index);
    
    le_audio_base_parser_subgroup_next(&parser);
}

// BASS Server Operations
broadcast_audio_scan_service_server_init(BASS_NUM_SOURCES, bass_sources, 
                                         BASS_NUM_CLIENTS, bass_clients);
broadcast_audio_scan_service_server_add_source(&bass_source_new, &source_index);
broadcast_audio_scan_service_server_set_pa_sync_state(0, LE_AUDIO_PA_SYNC_STATE_SYNCHRONIZED_TO_PA);
```

**Key Features Tested:**
- BASE advertisement parsing and validation
- BIG (Broadcast Isochronous Group) synchronization
- BASS server implementation with multiple client support
- PA (Periodic Advertising) state management
- Audio decoding and playback pipeline
- Source lifecycle management (add/modify/remove)
- Multi-client BASS service handling

**State Machine Testing:**
```c
static enum {
    APP_W4_WORKING,
    APP_W4_BROADCAST_ADV,
    APP_W4_PA_AND_BIG_INFO,
    APP_W4_BROADCAST_CODE,
    APP_W4_BIG_SYNC_ESTABLISHED,
    APP_STREAMING,
    APP_IDLE
} app_state = APP_W4_WORKING;
```

### 3. Broadcast Assistant Test (`le_audio_broadcast_assistant.c`)

**Purpose**: Tests the broadcast assistant role, helping other devices discover and connect to broadcast sources.

**Primary Usage of `src/le-audio`:**
```c
#include "le-audio/gatt-service/broadcast_audio_scan_service_client.h"  // BASS client
#include "le-audio/le_audio_util.h"                                    // Utilities
#include "le-audio/le_audio_base_parser.h"                             // BASE parsing

// BASS Client Operations
broadcast_audio_scan_service_client_init(&bass_packet_handler);
broadcast_audio_scan_service_client_connect(&bass_connection, bass_sources, 
                                           MAX_SOURCES, con_handle, &bass_cid);

// Control broadcast sources on remote devices
broadcast_audio_scan_service_client_add_source(bass_cid, &bass_source_data);
broadcast_audio_scan_service_client_modify_source(bass_cid, bass_source_id, &bass_source_data);
broadcast_audio_scan_service_client_set_broadcast_code(bass_cid, bass_source_id, broadcast_code);
broadcast_audio_scan_service_client_remove_source(bass_cid, bass_source_id);

// Parse BASE advertisements found during scanning
le_audio_base_parser_t parser;
bool ok = le_audio_base_parser_init(&parser, adv_data, adv_size);

// Extract detailed codec and BIS information
for (each_subgroup) {
    uint8_t codec_specific_configuration_length = le_audio_base_parser_bis_get_codec_specific_configuration_length(&parser);
    const uint8_t * codec_specific_configuration = le_audio_base_parser_subgroup_get_codec_specific_configuration(&parser);
    
    for (each_bis) {
        uint8_t bis_index = le_audio_base_parser_bis_get_index(&parser);
        codec_specific_configuration = le_audio_base_bis_parser_get_codec_specific_configuration(&parser);
        le_audio_base_parser_bis_next(&parser);
    }
    le_audio_base_parser_subgroup_next(&parser);
}
```

**Key Features Tested:**
- BASS client role implementation
- Remote broadcast source management
- Broadcast code handling for encrypted streams
- Scan delegator discovery and connection
- Multi-step broadcast discovery and setup process
- Complex state management for assistant operations

### 4. LC3 Codec Test (`lc3_test.c`)

**Purpose**: Tests LC3 codec performance and load characteristics.

**Key Features Tested:**
- LC3 encoding performance measurement
- Various sampling rates and frame durations
- Memory usage analysis
- Integration with BTstack LC3 implementation

### 5. Unicast Tests (`le_audio_unicast_*.c`)

**Purpose**: Tests unicast LE Audio scenarios (point-to-point audio).

**Key Features Tested:**
- Unicast gateway role (audio source)
- Unicast headset role (audio sink)
- Audio Stream Control Service (ASCS) integration
- Call control and telephony integration via `.gatt` files

## Build System Integration (`CMakeLists.txt`)

The build system demonstrates comprehensive integration with the LE Audio stack:

```cmake
# Include all LE Audio sources
file(GLOB SOURCES_LE_AUDIO "../../src/le-audio/*.c" 
                           "../../src/le-audio/gatt-service/*.c" 
                           "../../example/le_audio_demo_util_*.c")

# Include paths for LE Audio headers
include_directories(../../src)                               # Main LE Audio headers
include_directories(../../3rd-party/lc3-google/include)     # LC3 codec
include_directories(../../example)                          # Demo utilities

# GATT DB generation from .gatt files
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EXAMPLE}.h
    DEPENDS ${CMAKE_SOURCE_DIR}/${EXAMPLE}.gatt
    COMMAND ${CMAKE_SOURCE_DIR}/../../tool/compile_gatt.py
    ARGS ${CMAKE_SOURCE_DIR}/${EXAMPLE}.gatt ${CMAKE_CURRENT_BINARY_DIR}/${EXAMPLE}.h
)

# Create targets for all LE Audio examples
file(GLOB EXAMPLES_C "le_audio_*.c" "lc3_*.c")
foreach(EXAMPLE_FILE ${EXAMPLES_C})
    get_filename_component(EXAMPLE ${EXAMPLE_FILE} NAME_WE)
    add_executable(${EXAMPLE} ${SOURCE_FILES})
    target_link_libraries(${EXAMPLE} btstack m pthread)
endforeach(EXAMPLE_FILE)
```

**Dependencies Included:**
- **LC3 Google**: Google's LC3 codec implementation
- **LC3Plus Fraunhofer**: Advanced LC3Plus codec (optional)
- **FDK-AAC**: AAC codec support (optional)  
- **LDAC**: Sony LDAC codec (optional)
- **OpenAPTX**: aptX codec support (optional)

## Hardware Testing Setup

The test suite is designed for real hardware testing with the Nordic nRF5340DK:

### Nordic nRF5340DK Configuration

**Dual-Core Architecture:**
- **Network Core**: Runs Packetcraft LL firmware for radio control
- **Application Core**: Runs Zephyr HCI UART sample over USB CDC

**Network Core Programming:**
```sh
nrfjprog --program ble5-ctr-rpmsg_<version>.hex --chiperase --coprocessor CP_NETWORK -r
```

**Application Core Build:**
```sh
west build -b nrf5340dk_nrf5340_cpuapp -- -DDTC_OVERLAY_FILE=usb.overlay -DOVERLAY_CONFIG=overlay-usb.conf
```

**USB Overlay Configuration (`usb.overlay`):**
```c
/ {
    chosen {
        zephyr,bt-c2h-uart = &cdc_acm_uart0;
    };
};

&zephyr_udc0 {
    cdc_acm_uart0: cdc_acm_uart0 {
        compatible = "zephyr,cdc-acm-uart";
    };
};
```

**USB Configuration (`overlay-usb.conf`):**
```makefile
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_DEVICE_PRODUCT="Zephyr HCI UART sample"
CONFIG_USB_CDC_ACM=y
CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=n
```

## Integration Patterns Demonstrated

### 1. Advertisement Handling Pattern
```c
// Source: Create BASE advertisements
le_audio_base_builder_init(&builder, buffer, size, presentation_delay);
le_audio_base_builder_add_subgroup(&builder, codec_id, config, config_len, metadata, meta_len);
le_audio_base_builder_add_bis(&builder, bis_index, bis_config, bis_config_len);

// Sink: Parse received advertisements  
le_audio_base_parser_init(&parser, adv_data, adv_size);
uint32_t delay = le_audio_base_parser_get_presentation_delay(&parser);
uint8_t subgroups = le_audio_base_parser_get_num_subgroups(&parser);
```

### 2. GATT Service Integration Pattern
```c
// Server role (sink device)
broadcast_audio_scan_service_server_init(num_sources, sources, num_clients, clients);
broadcast_audio_scan_service_server_register_packet_handler(&handler);
broadcast_audio_scan_service_server_add_source(&source_data, &source_index);

// Client role (assistant device)  
broadcast_audio_scan_service_client_init(&handler);
broadcast_audio_scan_service_client_connect(&connection, sources, num_sources, con_handle, &cid);
broadcast_audio_scan_service_client_add_source(cid, &source_data);
```

### 3. Codec Configuration Pattern
```c
// Parse codec configuration from BASE
const uint8_t * codec_config = le_audio_base_parser_subgroup_get_codec_specific_configuration(&parser);

// Extract specific parameters
for (uint8_t i = 0; i < codec_config_length; ) {
    uint8_t ltv_length = codec_config[i++];
    uint8_t ltv_type = codec_config[i++];
    
    switch (ltv_type) {
        case LE_AUDIO_CODEC_CONFIGURATION_TYPE_SAMPLING_FREQUENCY:
            sampling_frequency_index = codec_config[i];
            break;
        case LE_AUDIO_CODEC_CONFIGURATION_TYPE_FRAME_DURATION:
            frame_duration_index = codec_config[i];
            break;
    }
    i += ltv_length - 1;
}

// Convert to BTstack LC3 parameters
frame_duration = le_audio_util_get_btstack_lc3_frame_duration(frame_duration_index);
```

### 4. State Management Pattern
```c
// Complex state machines for managing LE Audio operations
static enum {
    APP_W4_WORKING,
    APP_W4_BROADCAST_ADV,
    APP_W4_PA_AND_BIG_INFO,
    APP_W4_BROADCAST_CODE,
    APP_W4_BIG_SYNC_ESTABLISHED,
    APP_STREAMING,
    APP_IDLE
} app_state = APP_W4_WORKING;

// State transitions based on HCI events
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_LE_META:
                    switch (hci_event_le_meta_get_subevent_code(packet)) {
                        case HCI_SUBEVENT_LE_PERIODIC_ADVERTISING_SYNC_ESTABLISHED:
                            if (app_state == APP_W4_PA_AND_BIG_INFO) {
                                app_state = APP_W4_BIG_SYNC_ESTABLISHED;
                                // Continue to BIG sync...
                            }
                            break;
                    }
                    break;
            }
            break;
    }
}
```

## Test Coverage Analysis

The test suite provides comprehensive coverage across multiple dimensions:

### Protocol Stack Integration
- **HCI Layer**: Direct integration with HCI events and commands
- **GAP Layer**: Extended and periodic advertising management
- **L2CAP Layer**: Isochronous channel management
- **GATT Layer**: BASS service implementation (client and server)

### Codec Integration  
- **Google LC3**: Primary codec with multiple quality settings
- **LC3Plus**: Advanced codec support (optional)
- **Multi-codec**: Support for fallback codecs (AAC, LDAC, aptX)

### Advertisement Management
- **Extended Advertising**: Broadcast announcements with broadcast ID
- **Periodic Advertising**: BASE structure advertisements
- **Scan Response**: Additional broadcast information

### GATT Services
- **BASS Client**: Broadcast assistant functionality
- **BASS Server**: Broadcast sink with multiple client support  
- **Service Discovery**: Automatic GATT service discovery
- **Characteristic Operations**: Read, write, notify operations

### State Management
- **Connection States**: Complex state machines for each role
- **Broadcast States**: PA sync, BIG sync, streaming states
- **Error Recovery**: Handling of various failure scenarios

### Multi-device Scenarios
- **Concurrent Connections**: Multiple BASS clients per server
- **Source Management**: Multiple broadcast sources per device
- **Role Combinations**: Devices acting in multiple roles simultaneously

## GATT Database Integration

Several tests include `.gatt` files that define GATT services:

### `le_audio_broadcast_sink.gatt`
```gatt
#import <broadcast_audio_scan_service.gatt>
```
Imports the complete BASS service definition, demonstrating integration with BTstack's GATT compilation system.

### `telephony.gatt`  
Defines telephony-related services for unicast scenarios, showing integration with call control functionality.

## Key Benefits of This Test Architecture

### 1. Real-world Validation
- Tests actual hardware implementations rather than just unit tests
- Validates complete signal path from advertisement to audio playback
- Tests with real RF characteristics and timing constraints

### 2. Interoperability Testing
- Nordic nRF5340 compatibility ensures broad ecosystem support
- PTS compliance testing for Bluetooth SIG qualification
- Cross-vendor interoperability validation

### 3. Comprehensive Coverage
- **All LE Audio Roles**: Source, sink, assistant
- **All Transport Types**: Broadcast and unicast
- **All GATT Services**: BASS, ASCS, telephony
- **All Codec Variants**: Different quality levels and configurations

### 4. Developer Documentation
- Working examples serve as implementation guides
- Demonstrates best practices for LE Audio development
- Shows integration patterns with BTstack framework

### 5. Regression Prevention
- Automated testing prevents regressions in complex scenarios
- Hardware-in-loop testing catches real-world issues
- Continuous integration with multiple hardware platforms

### 6. Performance Validation
- LC3 codec performance measurement
- Memory usage analysis
- Timing validation for synchronized playback

## Usage Instructions

### Building the Test Suite
```bash
mkdir build && cd build
cmake ..
make
```

### Running Individual Tests
```bash
# Broadcast source test
./le_audio_broadcast_source

# Broadcast sink test  
./le_audio_broadcast_sink

# Broadcast assistant test
./le_audio_broadcast_assistant

# LC3 performance test
./lc3_test
```

### Hardware Setup
1. Program Nordic nRF5340DK with appropriate firmware
2. Connect via USB for HCI UART transport
3. Run tests with real RF environment
4. Use multiple boards for multi-device scenarios

## Conclusion

The `test/le_audio` directory demonstrates how BTstack's `src/le-audio` implementation provides a complete, production-ready LE Audio stack. The comprehensive test suite validates:

- **Standards Compliance**: Full Bluetooth LE Audio specification support
- **Real-world Performance**: Hardware validation with actual RF characteristics  
- **Interoperability**: Cross-vendor compatibility testing
- **Robustness**: Error handling and recovery mechanisms
- **Scalability**: Multi-device and multi-stream scenarios

This test architecture serves as both a validation framework and a comprehensive reference implementation, making it easier for developers to integrate LE Audio functionality into their applications while ensuring compliance with Bluetooth specifications.