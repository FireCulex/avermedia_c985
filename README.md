# AVerMedia C985 Linux Driver (Reverse Engineering POC)

A work-in-progress open-source Linux kernel driver for the **AVerMedia Live Gamer HD (C985)** HDMI capture card, created through reverse engineering of the Windows driver.

⚠️ **EXPERIMENTAL**: This driver is in active development and not production-ready.

---

## 📋 Overview

The AVerMedia C985 is a PCIe HDMI capture card that can record video up to 1080p60. While AVerMedia provides Windows drivers, no official Linux support exists. This project aims to create a fully functional V4L2 driver through careful analysis of the Windows driver behavior.

### Hardware Details
- **Vendor ID**: `0x1AF2`
- **Device ID**: `0xA001`
- **Chipset**: Pixelworks/Trident QP‑series family (OEM‑branded by AVerMedia)
- **Interface**: PCIe x1
- **Input**: HDMI 1.4
- **Max Resolution**: 1920x1080 @ 60fps
- **Output Format**: H.264 compressed video

---

## ✨ Current Status

### Working ✅
- PCIe device initialization and BAR mapping
- Memory controller configuration (DDR initialization)
- ARM firmware upload (video + audio)
- GPIO control via bit-bang and firmware mailbox
- I2C communication (hardware and software engines)
- NUC100 MCU communication (HDMI signal detection/timing)
- TI3101 audio codec initialization
- HDMI input detection and timing analysis
- V4L2 device registration
- Basic videobuf2 queue management

### In Progress 🚧
- **ARM mailbox communication**: Messages sent but ACKs not received
  - Encoder commands (SystemOpen, SystemLink, UpdateConfig, Start) implemented
  - Interrupt handling present but ARM not responding to mailbox
  - Likely needs additional HCI initialization sequence
- **Register mapping**: Encoder config register offsets defined but untested
- **DMA transfer**: Not yet implemented for video frames

### Not Yet Implemented ❌
- Video frame capture from DMA
- Audio capture
- Multiple input format support (currently hardcoded to 1080p60)
- Power management
- Module parameters for configuration

---

### Key Components

| File | Purpose |
|------|---------|
| `avermedia_c985.c` | PCIe driver entry point, device probe/remove |
| `cqlcodec.c` | Device init, memory setup, firmware download |
| `qphci.c` | HCI (Host Controller Interface) initialization |
| `cpr.c` | CPR (Card Processor Register) access for DDR RAM |
| `mailbox.c` | ARM firmware mailbox for GPIO/I2C (post-boot) |
| `qpfwapi.c` | Low-level mailbox message framing |
| `qpfwencapi.c` | High-level encoder control commands |
| `nuc100.c` | NUC100 MCU driver (HDMI signal detection) |
| `ti3101.c` | TI3101 audio codec driver |
| `i2c_bitbang.c` | GPIO-based I2C implementation |
| `v4l2.c` | V4L2 video device interface |

---

### SoC Details
The AVerMedia C985 uses an ARM9‑based hardware H.264 encoder SoC from the Pixelworks/Trident QP‑series family (OEM‑branded by AVerMedia).
The SoC includes:
- ARM9 CPU core
- Hardware H.264 encoder engine
- CPR memory controller (DDR2)
- HCI (Host Controller Interface) block
- Mailbox interface for firmware communication
- GPIO and I²C controllers (pre‑boot and post‑boot modes)
Firmware is uploaded by the host driver at runtime as a raw ARM9 binary.

## 🧪 Testing
### Basic Device Check
```
# List V4L2 devices
v4l2-ctl --list-devices

# Query capabilities
v4l2-ctl -d /dev/video0 --all

# Check HDMI signal detection
dmesg | grep -i hdmi
```

### Capture Test (when DMA works)
```
# Raw H.264 stream
ffmpeg -f v4l2 -i /dev/video0 -c copy output.h264

# Convert to MP4
ffmpeg -f v4l2 -i /dev/video0 -c copy -f mp4 output.mp4
```

## 🐛 Known Issues
### Critical Blockers
1. Mailbox ACK timeout: ARM receives interrupt but doesn't process mailbox
   - Symptom: FWAPI: mailbox timeout (0x6CC=0x01xxxxxx)
   - Likely cause: Missing HCI initialization step or incorrect interrupt routing
   - Debug: 0x800=0x00000000 suggests HCI interrupts not enabled
2. No video frames: Even if encoder starts, DMA not configured
   - Need to implement DMA descriptor setup
   - Need to handle DMA completion interrupts
   - Need to copy frame data to V4L2 buffers

### Minor Issues
- GPIO control uses bit-bang pre-boot, mailbox post-boot (works but complex)
- Hardcoded encoder settings (1080p60 H.264 only)
- No error recovery if HDMI signal lost during capture
- Verbose debug logging (will be configurable later)

## ⚖️ Legal Notice
This project is not affiliated with or endorsed by AVerMedia Technologies.
- Firmware files (qpvidfwpcie.bin, qpaudfw.bin) are proprietary and must be extracted from the official Windows driver

## 🔗 References
- [Official Windows Driver](https://www.avermedia.com/uk/support/download?model_number=C985)
- [V4L2 Driver Development Guide](https://www.kernel.org/doc/html/latest/driver-api/media/v4l2-core.html)
- [VideoBuf2 Framework](https://www.kernel.org/doc/html/latest/driver-api/media/v4l2-videobuf2.html)
- [TI TLV320AIC3101 Datasheet](https://www.ti.com/product/TLV320AIC3101)
- [Nuvoton NUC100 Series Manual](https://www.nuvoton-tech.com/pdf-71/nuc100rd1bn.pdf)
