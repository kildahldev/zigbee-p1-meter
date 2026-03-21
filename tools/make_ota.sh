#!/usr/bin/env bash
#
# Package ESP-IDF firmware binary into a Zigbee OTA file for Z2M.
# Reads OTA_FW_VERSION, OTA_MANUFACTURER_CODE, OTA_IMAGE_TYPE from zigbee_device.h
#
# Usage: ./tools/make_ota.sh [build_dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-${PROJECT_DIR}/build}"
BIN_FILE="${BUILD_DIR}/zigbee_p1_meter.bin"
HEADER_FILE="${PROJECT_DIR}/main/zigbee_device.h"
OTA_FILE="${BUILD_DIR}/zigbee_p1_meter.ota"

if [ ! -f "$BIN_FILE" ]; then
    echo "Error: $BIN_FILE not found. Run 'idf.py build' first."
    exit 1
fi

# Extract values from zigbee_device.h
MANUFACTURER_CODE=$(grep '#define OTA_MANUFACTURER_CODE' "$HEADER_FILE" | awk '{print $3}')
IMAGE_TYPE=$(grep '#define OTA_IMAGE_TYPE' "$HEADER_FILE" | awk '{print $3}')
FW_MAJOR=$(grep '#define FW_VERSION_MAJOR' "$HEADER_FILE" | awk '{print $3}')
FW_MINOR=$(grep '#define FW_VERSION_MINOR' "$HEADER_FILE" | awk '{print $3}')
FW_PATCH=$(grep '#define FW_VERSION_PATCH' "$HEADER_FILE" | awk '{print $3}')

echo "Manufacturer: $MANUFACTURER_CODE"
echo "Image type:   $IMAGE_TYPE"
echo "Version:      $FW_MAJOR.$FW_MINOR.$FW_PATCH"
echo "Firmware:     $BIN_FILE ($(stat -c%s "$BIN_FILE") bytes)"
echo ""

python3 -c "
import struct

fw_data = open('$BIN_FILE', 'rb').read()
fw_size = len(fw_data)

magic = 0x0BEEF11E
header_version = 0x0100
header_length = 56
field_control = 0x0000
manufacturer = $MANUFACTURER_CODE
image_type = $IMAGE_TYPE
file_version = ($FW_MAJOR << 24) | ($FW_MINOR << 16) | $FW_PATCH
zigbee_stack_version = 0x0002
header_string = b'Homey P1 Meter OTA'.ljust(32, b'\x00')
total_size = header_length + 6 + fw_size  # header + sub-element header + firmware

# ZCL OTA header (56 bytes, little-endian)
header  = struct.pack('<I', magic)                  # 4
header += struct.pack('<H', header_version)          # 2
header += struct.pack('<H', header_length)           # 2
header += struct.pack('<H', field_control)           # 2
header += struct.pack('<H', manufacturer)            # 2
header += struct.pack('<H', image_type)              # 2
header += struct.pack('<I', file_version)            # 4
header += struct.pack('<H', zigbee_stack_version)    # 2
header += header_string                              # 32
header += struct.pack('<I', total_size)              # 4

assert len(header) == header_length, f'Header size {len(header)} != {header_length}'

# Sub-element header: tag(2) + length(4)
sub_header = struct.pack('<HI', 0x0000, fw_size)

with open('$OTA_FILE', 'wb') as f:
    f.write(header)
    f.write(sub_header)
    f.write(fw_data)

print(f'OTA file created: $OTA_FILE ({total_size} bytes)')
"

echo "Done! Place $OTA_FILE in your Z2M data directory."
