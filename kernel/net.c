/* net.c - Network driver (RTL8139 based) - lightweight for low-end devices */
#include <kernel/net.h>
#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/pci.h>
#include <kernel/printf.h>

/* RTL8139 registers */
#define RTL8139_REG_MAC0       0x00
#define RTL8139_REG_TXSTATUS0  0x10
#define RTL8139_REG_TXADDR0    0x20
#define RTL8139_REG_RXBUF      0x30
#define RTL8139_REG_COMMAND    0x37
#define RTL8139_REG_INTRMASK   0x3C
#define RTL8139_REG_INTRSTATUS 0x3E
#define RTL8139_REG_RXREADPTR  0x38
#define RTL8139_REG_RXWRITEPTR 0x3A

/* Commands */
#define RTL8139_CMD_RESET      0x10
#define RTL8139_CMD_RXENABLE   0x08
#define RTL8139_CMD_TXENABLE   0x04

static net_interface_t net_if = {0};
static u8 rx_buffer[8192] __attribute__((aligned(4)));
static u16 rtl8139_port = 0;
static bool rtl8139_found = false;

void net_init(void) {
    /* Try to find RTL8139 via PCI */
    pci_device_t* dev = pci_find_device(0x10EC, 0x8139);
    if (dev && dev->bar[0]) {
        rtl8139_port = dev->bar[0] & ~0x3;  // Mask off I/O indicator bits
        rtl8139_found = true;
    }

    if (!rtl8139_found) {
        vga_puts("Network: RTL8139 not found, network disabled\n");
        return;
    }

    vga_puts_info("Network: Initializing RTL8139 driver...");

    /* Reset the card */
    outb(rtl8139_port + RTL8139_REG_COMMAND, RTL8139_CMD_RESET);
    
    /* Wait for reset to complete (with timeout) */
    for (volatile int i = 0; i < 100000; i++) { 
        asm volatile("pause"); 
        u8 status = inb(rtl8139_port + RTL8139_REG_COMMAND);
        if (!(status & RTL8139_CMD_RESET)) break;
    }

    /* Set up RX buffer */
    outl(rtl8139_port + RTL8139_REG_RXBUF, (u32)(u64)rx_buffer);
    outw(rtl8139_port + RTL8139_REG_RXREADPTR, 0);
    outw(rtl8139_port + RTL8139_REG_RXWRITEPTR, 0);

    /* Set MAC address (example hardcoded for low-end devices) */
    net_if.mac_addr[0] = 0x52;
    net_if.mac_addr[1] = 0x54;
    net_if.mac_addr[2] = 0x00;
    net_if.mac_addr[3] = 0x12;
    net_if.mac_addr[4] = 0x34;
    net_if.mac_addr[5] = 0x56;

    /* Default IP */
    net_if.ip_addr[0] = 192;
    net_if.ip_addr[1] = 168;
    net_if.ip_addr[2] = 1;
    net_if.ip_addr[3] = 100;

    /* Enable RX and TX */
    outb(rtl8139_port + RTL8139_REG_COMMAND, RTL8139_CMD_RXENABLE | RTL8139_CMD_TXENABLE);

    net_if.initialized = true;
    net_if.packets_rx = 0;
    net_if.packets_tx = 0;
    vga_puts_success("Network: Initialized");
}

net_interface_t* net_get_interface(void) {
    return &net_if;
}

int net_send_packet(const void* data, usize len) {
    if (!net_if.initialized || len > NET_MAX_PACKET_SIZE) {
        return -1;
    }

    /* Send packet via RTL8139 TX buffer 0 */
    if (len < 64) len = 64; // Minimum Ethernet frame size

    // Use physical address for TX buffer - in a real implementation this would be a DMA buffer
    // For now, we'll just write to the TX descriptor
    outl(rtl8139_port + RTL8139_REG_TXADDR0, (u32)(u64)data);
    outl(rtl8139_port + RTL8139_REG_TXSTATUS0, (u32)len);

    // Wait for transmission to complete (with timeout)
    for (int i = 0; i < 100000; i++) {
        u32 status = inl(rtl8139_port + RTL8139_REG_TXSTATUS0);
        if (status & (1 << 15)) { // TX OK bit
            net_if.packets_tx++;
            return (int)len;
        }
    }

    return -1; // Timeout
}

int net_recv_packet(void* buffer, usize max_len) {
    (void)buffer; (void)max_len;
    if (!net_if.initialized) {
        return -1;
    }

    /* No packet simulation for now */
    return 0;
}

void net_handle_tcpip(void) {
    /* Placeholder for TCP/IP stack */
}