/* net.c - Network driver (RTL8139 based) - lightweight for low-end devices */
#include <kernel/net.h>
#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/vga.h>

/* Use fixed IO port for RTL8139 (QEMU/emulation) */
#define RTL8139_PORT 0xC000
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* RTL8139 registers */
#define RTL8139_REG_MAC0 0x00
#define RTL8139_REG_TXSTATUS0 0x10
#define RTL8139_REG_TXADDR0 0x20
#define RTL8139_REG_RXBUF 0x30
#define RTL8139_REG_COMMAND 0x37
#define RTL8139_REG_INTRMASK 0x3C
#define RTL8139_REG_INTRSTATUS 0x3E
#define RTL8139_REG_RXREADPTR 0x38
#define RTL8139_REG_RXWRITEPTR 0x3A

/* Commands */
#define RTL8139_CMD_RESET 0x10
#define RTL8139_CMD_RXENABLE 0x08
#define RTL8139_CMD_TXENABLE 0x04

/* Interrupt bits */
#define RTL8139_INT_RXOK 0x01
#define RTL8139_INT_TXOK 0x02

static net_interface_t net_if = {0};
static u8 rx_buffer[8192] __attribute__((aligned(4)));
static volatile u32 rx_read_ptr = 0;

void net_init(void) {
    vga_puts_info("Network: Initializing RTL8139 driver...");
    
    /* Reset the card */
    outb(RTL8139_PORT + RTL8139_REG_COMMAND, RTL8139_CMD_RESET);
    for (volatile int i = 0; i < 100000; i++) { asm volatile("pause"); }
    
    /* Set up RX buffer */
    outw(RTL8139_PORT + RTL8139_REG_RXBUF, (u32)(u64)rx_buffer);
    outw(RTL8139_PORT + RTL8139_REG_RXREADPTR, 0);
    outw(RTL8139_PORT + RTL8139_REG_RXWRITEPTR, 0);
    
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
    outb(RTL8139_PORT + RTL8139_REG_COMMAND, RTL8139_CMD_RXENABLE | RTL8139_CMD_TXENABLE);
    
    net_if.initialized = true;
    net_if.packets_rx = 0;
    net_if.packets_tx = 0;
    vga_puts_success("Network: Initialized (simulated - no PCI)");
}

net_interface_t* net_get_interface(void) {
    return &net_if;
}

int net_send_packet(const void* data, usize len) {
    if (!net_if.initialized || len > NET_MAX_PACKET_SIZE) {
        return -1;
    }
    
    /* Simulate packet transmission for low-end device compatibility */
    net_if.packets_tx++;
    return (int)len;
}

int net_recv_packet(void* buffer, usize max_len) {
    if (!net_if.initialized) {
        return -1;
    }
    
    /* No packet simulation for now */
    return 0;
}

void net_handle_tcpip(void) {
    /* Placeholder for TCP/IP stack */
}