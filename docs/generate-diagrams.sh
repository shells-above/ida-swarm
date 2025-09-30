#!/bin/bash

# IDA Swarm - Architecture Diagram Generator
# Converts all .mmd (Mermaid) files to SVG and PNG formats

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║         IDA Swarm Architecture Diagram Generator          ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if mmdc is installed
if ! command -v mmdc &> /dev/null; then
    echo -e "${RED}✗ Error: mmdc (mermaid-cli) is not installed${NC}"
    echo ""
    echo "Install it with:"
    echo -e "  ${YELLOW}npm install -g @mermaid-js/mermaid-cli${NC}"
    echo ""
    exit 1
fi

echo -e "${GREEN}✓ Found mmdc:${NC} $(which mmdc)"
echo ""

# Create output directory
OUTPUT_DIR="generated"
mkdir -p "$OUTPUT_DIR"
echo -e "${GREEN}✓ Output directory:${NC} $OUTPUT_DIR/"
echo ""

# Find all .mmd files
MMD_FILES=($(find . -maxdepth 1 -name "*.mmd" -type f | sort))

if [ ${#MMD_FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}⚠ No .mmd files found in current directory${NC}"
    exit 0
fi

echo -e "${BLUE}Found ${#MMD_FILES[@]} diagram(s) to generate:${NC}"
for file in "${MMD_FILES[@]}"; do
    echo "  • $(basename "$file")"
done
echo ""

# Counter for statistics
TOTAL=0
SUCCESS=0
FAILED=0

# Process each file
for mmd_file in "${MMD_FILES[@]}"; do
    TOTAL=$((TOTAL + 1))

    # Get base filename without extension
    BASENAME=$(basename "$mmd_file" .mmd)

    echo -e "${BLUE}──────────────────────────────────────────────────────────${NC}"
    echo -e "${BLUE}Processing:${NC} $BASENAME.mmd"
    echo ""

    # Generate SVG with transparent background
    SVG_OUTPUT="$OUTPUT_DIR/${BASENAME}.svg"
    echo -n "  Generating SVG... "
    if mmdc -i "$mmd_file" -o "$SVG_OUTPUT" -b transparent 2>/dev/null; then
        echo -e "${GREEN}✓${NC}"
        SUCCESS=$((SUCCESS + 1))
    else
        echo -e "${RED}✗ Failed${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    # Generate PNG (high resolution) with transparent background
    PNG_OUTPUT="$OUTPUT_DIR/${BASENAME}.png"
    echo -n "  Generating PNG... "
    if mmdc -i "$mmd_file" -o "$PNG_OUTPUT" -b transparent -s 3 2>/dev/null; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗ Failed${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    # Get file sizes
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        SVG_SIZE=$(ls -lh "$SVG_OUTPUT" | awk '{print $5}')
        PNG_SIZE=$(ls -lh "$PNG_OUTPUT" | awk '{print $5}')
    else
        # Linux
        SVG_SIZE=$(du -h "$SVG_OUTPUT" | cut -f1)
        PNG_SIZE=$(du -h "$PNG_OUTPUT" | cut -f1)
    fi

    echo -e "  ${GREEN}✓ SVG:${NC} $SVG_SIZE"
    echo -e "  ${GREEN}✓ PNG:${NC} $PNG_SIZE (3x scale)"
    echo ""
done

# Summary
echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Summary${NC}"
echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"
echo -e "  Total files:    $TOTAL"
echo -e "  ${GREEN}Success:${NC}        $SUCCESS"
if [ $FAILED -gt 0 ]; then
    echo -e "  ${RED}Failed:${NC}         $FAILED"
fi
echo ""

if [ $SUCCESS -gt 0 ]; then
    echo -e "${GREEN}✓ Diagrams generated successfully in:${NC}"
    echo -e "  ${YELLOW}$SCRIPT_DIR/$OUTPUT_DIR/${NC}"
    echo ""
    echo -e "${BLUE}Generated files:${NC}"
    ls -lh "$OUTPUT_DIR" | tail -n +2 | awk '{printf "  • %-30s %5s\n", $9, $5}'
    echo ""
fi

if [ $FAILED -gt 0 ]; then
    exit 1
fi

exit 0