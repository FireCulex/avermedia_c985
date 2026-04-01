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
#define PED_DMA_ENGINE_SIZE              0x100
#define PED_DMA_ENGINE_CAPABILITIES      0x00
#define PED_DMA_ENGINE_CONTROL_STATUS    0x04
#define PED_DMA_ENGINE_DESCRIPTOR_LO     0x08
#define PED_DMA_ENGINE_DESCRIPTOR_HI     0x0C
#define PED_DMA_ENGINE_HARDWARE_TIME     0x10
#define PED_DMA_ENGINE_CHAIN_CMPL        0x14

/*
 * PED_DMA_COMMON offsets (at BAR0 + 0x4000)
 */
#define PED_DMA_COMMON_BASE              0x4000
#define PED_DMA_COMMON_CONTROL_STATUS    0x4000
#define PED_DMA_COMMON_FPGA_VERSION      0x4004

/*
 * PED_DMA_ENGINE.Capabilities bits (from PedDmaInit)
 *   Bit 0: Engine present
 *   Bit 1: Direction (1=C2S/read, 0=S2C/write)
 *   Bits 16+: Max transfer size shift / IRQ line
 */
#define PED_DMA_CAP_PRESENT              0x01
#define PED_DMA_CAP_C2S                  0x02    /* Card-to-System (read) */

/*
 * PED_DMA_ENGINE.ControlStatus bits (from PciInterruptService)
 *
 * ISR check: if ((status & 1) && (status & 2))
 * Acknowledge: write 0x03
 */
#define PED_DMA_CTRL_RUNNING             0x01    /* Bit 0: engine running */
#define PED_DMA_CTRL_INT_PENDING         0x02    /* Bit 1: interrupt/complete */
#define PED_DMA_CTRL_ACK                 0x03    /* Write to acknowledge */

/*
 * PED_DMA_COMMON.ControlStatus bits
 */
#define PED_GLOBAL_INT_ENABLE            0x01
#define PED_GLOBAL_INT_DISABLE_MASK      0xFFFFFFFE

/*
 * HCI interrupt register (BAR1 + 0x30)
 * From PciInterruptService: *(pRegistersEx + 0x30) & 0x40000000
 */
#define HCI_INT_STATUS_REG               0x30
#define HCI_INT_STATUS_BIT               0x40000000

/*
 * Function prototypes
 */
int pci_interrupt_service_register(struct c985_poc *d);
void pci_interrupt_service_unregister(struct c985_poc *d);
void cpciectl_enable_interrupts(struct c985_poc *d);
void cpciectl_disable_interrupts(struct c985_poc *d);

#endif /* INTERRUPTS_H */
