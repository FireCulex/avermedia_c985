#!/bin/bash

# Required modules for V4L2 capture card driver
modules=(
    "videodev"
    "v4l2_common"
    "videobuf2_core"
    "videobuf2_v4l2"
    "videobuf2_vmalloc"
    "videobuf2_memops"
    "videobuf2-dma-contig"
)

echo "Loading V4L2 modules..."

for mod in "${modules[@]}"; do
    echo -n "Loading $mod... "
    if modprobe "$mod" 2>/dev/null; then
        echo "OK"
    else
        echo "FAILED (may already be loaded or built-in)"
    fi
done

echo ""
echo "Now try loading your module:"
echo "  insmod avermedia_c985_poc.ko"
