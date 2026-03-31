#ifndef INTERRUPTS_H
#define INTERRUPTS_H

struct c985_poc;

/*
 * PED_BAR0_REGISTERS layout (from Ghidra structure)
 *
 * 0x0000 - 0x3FFF: PED_DMA_ENGINE[64] (each 0x100 bytes)
 * 0x4000 - 0x4007: PED_DMA_COMMON
 */

/*
 * PED_DMA_ENGINE offsets (from Ghidra: PED_DMA_ENGINE structure)
 * Each DMA engine is 0x100 bytes
 */
#define PED_DMA_ENGINE_SIZE              0x100   /* sizeof(PED_DMA_ENGINE) */
#define PED_DMA_ENGINE_CAPABILITIES      0x00    /* PED_DMA_ENGINE.Capabilities */
#define PED_DMA_ENGINE_CONTROL_STATUS    0x04    /* PED_DMA_ENGINE.ControlStatus */
#define PED_DMA_ENGINE_DESCRIPTOR        0x08    /* PED_DMA_ENGINE.Descriptor */
#define PED_DMA_ENGINE_HARDWARE_TIME     0x10    /* PED_DMA_ENGINE.HardwareTime */

/*
 * PED_DMA_COMMON offsets (from Ghidra: PED_BAR0_REGISTERS.Common at 0x4000)
 */
#define PED_DMA_COMMON_BASE              0x4000  /* PED_BAR0_REGISTERS.Common */
#define PED_DMA_COMMON_CONTROL_STATUS    0x4000  /* PED_DMA_COMMON.ControlStatus */
#define PED_DMA_COMMON_FPGA_VERSION      0x4004  /* PED_DMA_COMMON.FpgaVersion */

/*
 * PED_DMA_ENGINE.ControlStatus bits (from PciInterruptService decompile)
 *
 * Check: if ((reg & 0x01) && (reg & 0x02))
 * Clear: write 0x03
 */
#define PED_INT_PENDING                  0x01    /* Bit 0: interrupt pending */
#define PED_INT_ENABLED                  0x02    /* Bit 1: interrupt enabled/complete */
#define PED_INT_CLEAR                    0x03    /* Write to clear both bits */

/*
 * PED_DMA_COMMON.ControlStatus bits (from CPCIeCntl_EnableInterrupts decompile)
 *
 * Enable:  |= 0x01
 * Disable: &= 0xFFFFFFFE
 */
#define PED_GLOBAL_INT_ENABLE            0x01    /* Bit 0: global interrupt enable */
#define PED_GLOBAL_INT_DISABLE_MASK      0xFFFFFFFE

/*
 * _PCIE_DEVICE_EXTENSION offsets (from Ghidra structure)
 */
#define PCIE_EXT_INTERRUPT_STATUS        0x0B0   /* _PCIE_DEVICE_EXTENSION.InterruptStatus */
#define PCIE_EXT_INTERRUPT_ENABLE        0x0B4   /* _PCIE_DEVICE_EXTENSION.InterruptEnable */
#define PCIE_EXT_DMA_DEVICES             0x0F8   /* _PCIE_DEVICE_EXTENSION.DmaDevices[64] */
#define PCIE_EXT_NUM_DMA_AVAILABLE       0x1328  /* _PCIE_DEVICE_EXTENSION.m_NumDmaAvailable */
#define PCIE_EXT_REGISTERS               0x1758  /* _PCIE_DEVICE_EXTENSION.pRegisters */
#define PCIE_EXT_REGISTERS_EX            0x1760  /* _PCIE_DEVICE_EXTENSION.pRegistersEx */

/*
 * DEVICE_TRANSFER size (from Ghidra: DmaDevices[64] spans 0x1200 bytes)
 * 0x1200 / 64 = 0x48 bytes per entry
 */
#define DEVICE_TRANSFER_SIZE             0x48    /* sizeof(DEVICE_TRANSFER) */

/*
 * Function prototypes
 */
int pci_interrupt_service_register(struct c985_poc *d);
void pci_interrupt_service_unregister(struct c985_poc *d);
void cpciectl_enable_interrupts(struct c985_poc *d);
void cpciectl_disable_interrupts(struct c985_poc *d);

#endif /* INTERRUPTS_H */
