#!/bin/bash
# Build script for VirtIO Serial application

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}======================================"
echo -e "  VirtIO Serial Build Script"
echo -e "======================================${NC}"
echo ""

# Default board
BOARD="${1:-sk_am62/am6254/m4}"

echo -e "${YELLOW}Building for board: ${BOARD}${NC}"
echo ""

# Source Zephyr environment if not already done
if [ -z "$ZEPHYR_BASE" ]; then
    echo -e "${YELLOW}ZEPHYR_BASE not set, attempting to source zephyr-env.sh...${NC}"

    if [ -f "$HOME/zephyrproject/zephyr/zephyr-env.sh" ]; then
        source "$HOME/zephyrproject/zephyr/zephyr-env.sh"
    elif [ -f "$HOME/.pyenv/shims/python3.12" ]; then
        export PATH="$HOME/.pyenv/shims:$PATH"
        export ZEPHYR_BASE="$HOME/src/zephyr"
    else
        echo -e "${RED}Error: Cannot find Zephyr environment${NC}"
        echo "Please source zephyr-env.sh manually or set ZEPHYR_BASE"
        exit 1
    fi
fi

echo -e "${GREEN}Zephyr environment: ${ZEPHYR_BASE}${NC}"
echo ""

# Clean previous build
if [ -d "build" ]; then
    echo -e "${YELLOW}Cleaning previous build...${NC}"
    rm -rf build
fi

# Build
echo -e "${GREEN}Building application...${NC}"
west build -b "${BOARD}"

# Check build status
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}======================================"
    echo -e "  Build Successful!"
    echo -e "======================================${NC}"
    echo ""
    echo "Output files:"
    echo "  ELF:  build/zephyr/zephyr.elf"
    echo "  BIN:  build/zephyr/zephyr.bin"
    echo "  HEX:  build/zephyr/zephyr.hex"
    echo ""

    # Check for resource table
    echo -e "${YELLOW}Checking resource table...${NC}"
    arm-none-eabi-readelf -S build/zephyr/zephyr.elf | grep -E "\.resource_table|\.text|\.data" || true

    echo ""
    echo -e "${GREEN}Next steps:${NC}"
    echo "1. Copy firmware to Linux:"
    echo "   sudo cp build/zephyr/zephyr.elf /lib/firmware/am62-m4-fw.elf"
    echo ""
    echo "2. Load firmware:"
    echo "   echo am62-m4-fw.elf | sudo tee /sys/class/remoteproc/remoteproc0/firmware"
    echo "   echo start | sudo tee /sys/class/remoteproc/remoteproc0/state"
    echo ""
    echo "3. Test communication:"
    echo "   echo 'Hello M4!' > /dev/vport0p0"
    echo ""
else
    echo ""
    echo -e "${RED}======================================"
    echo -e "  Build Failed!"
    echo -e "======================================${NC}"
    exit 1
fi
