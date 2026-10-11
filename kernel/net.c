/* net.c - Polling RTL8139 Ethernet driver.
 *
 * This file provides Ethernet frame I/O only. DHCP, ARP, IPv4, TCP, DNS,
 * TLS and HTTP are separate layers and are not claimed to exist here.
 */
#include <kernel/net.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/pci.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define RTL_REG_MAC0       0x00
#define RTL_REG_TXSTATUS0  0x10
#define RTL_REG_TXADDR0    0x20
#define RTL_REG_RXBUF      0x30
#define RTL_REG_COMMAND    0x37
#define RTL_REG_CAPR       0x38
#define RTL_REG_CBR        0x3A
#define RTL_REG_IMR        0x3C
#define RTL_REG_ISR        0x3E
#define RTL_REG_RCR        0x44
#define RTL_REG_CONFIG1    0x52

#define RTL_CMD_RESET      0x10
#define RTL_CMD_RX_ENABLE  0x08
#define RTL_CMD_TX_ENABLE  0x04
#define RTL_CMD_RX_EMPTY   0x01
#define RTL_TX_OK          (1u << 15)
#define RTL_RX_OK          0x0001u
#define RTL_RX_RING_SIZE   8192u
#define RTL_RX_RING_MASK   (RTL_RX_RING_SIZE - 1u)
#define RTL_RX_RING_EXTRA  16u
#define RTL_RX_HEADER_SIZE 4u
#define RTL_ETH_CRC_SIZE   4u
#define RTL_ETH_MIN_FRAME  60u

static net_interface_t net_if = {0};
static u8 rx_buffer[RTL_RX_RING_SIZE + RTL_RX_RING_EXTRA]
    __attribute__((aligned(256)));
static u8 tx_buffers[4][NET_MAX_PACKET_SIZE] __attribute__((aligned(256)));
static u16 rtl8139_port;
static u16 rx_offset;
static u8 tx_slot;

static u16 ring_read16(u16 offset) {
    u16 lo = rx_buffer[offset & RTL_RX_RING_MASK];
    u16 hi = rx_buffer[(offset + 1u) & RTL_RX_RING_MASK];
    return (u16)(lo | (hi << 8));
}

static void ring_copy_out(u8* dst, u16 offset, usize length) {
    for (usize i = 0; i < length; i++)
        dst[i] = rx_buffer[(offset + (u16)i) & RTL_RX_RING_MASK];
}

void net_init(void) {
    net_if.initialized = false;
    rtl8139_port = 0;
    rx_offset = 0;
    tx_slot = 0;

    pci_device_t* dev = pci_find_device(0x10EC, 0x8139);
    if (!dev || !(dev->bar[0] & 1u)) {
        vga_puts("Network: RTL8139 I/O device not found; network disabled\n");
        return;
    }

    rtl8139_port = (u16)(dev->bar[0] & 0xFFFCu);
    if (!rtl8139_port) {
        vga_puts("Network: RTL8139 has an invalid I/O BAR\n");
        return;
    }

    /* Enable PCI I/O-space access and bus mastering for DMA. Write zero to
     * the status half because PCI status bits are write-one-to-clear. */
    u16 command = pci_read_config_word(dev->bus, dev->device, dev->function, 0x04);
    pci_write_config_dword(dev->bus, dev->device, dev->function, 0x04,
                           (u32)(command | 0x0005u));

    outb(rtl8139_port + RTL_REG_CONFIG1, 0x00);
    outb(rtl8139_port + RTL_REG_COMMAND, RTL_CMD_RESET);
    bool reset_done = false;
    for (volatile u32 i = 0; i < 1000000u; i++) {
        if (!(inb(rtl8139_port + RTL_REG_COMMAND) & RTL_CMD_RESET)) {
            reset_done = true;
            break;
        }
        asm volatile("pause");
    }
    if (!reset_done) {
        vga_puts("Network: RTL8139 reset timed out\n");
        return;
    }

    /* DMA uses physical addresses. This kernel currently identity-maps its
     * static image/buffers; reject addresses the 32-bit device cannot DMA. */
    u64 rx_addr = (u64)(uintptr_t)rx_buffer;
    if (rx_addr > 0xFFFFFFFFULL) {

        vga_puts("Network: RX DMA buffer is above 4 GiB\n");
        return;
    }

    kmemset(rx_buffer, 0, sizeof(rx_buffer));
    kmemset(tx_buffers, 0, sizeof(tx_buffers));
    outl(rtl8139_port + RTL_REG_RXBUF, (u32)rx_addr);
    outw(rtl8139_port + RTL_REG_CAPR, 0xFFF0u);
    outw(rtl8139_port + RTL_REG_IMR, 0x0000u); /* polling, no IRQ dependency */
    outw(rtl8139_port + RTL_REG_ISR, 0xFFFFu); /* clear pending status */
    /* 8 KiB receive ring, accept broadcast and physical-address frames. */
    outl(rtl8139_port + RTL_REG_RCR, 0x180Au);

    for (int i = 0; i < NET_ETHERNET_ADDR_LEN; i++)
        net_if.mac_addr[i] = inb(rtl8139_port + RTL_REG_MAC0 + (u16)i);

    /* Do not invent a MAC address or pretend a static IP is configured.
     * IPv4 configuration belongs to a future DHCP/static-config layer. */
    kmemset(net_if.ip_addr, 0, sizeof(net_if.ip_addr));
    net_if.packets_rx = 0;
    net_if.packets_tx = 0;
    rx_offset = 0;
    outb(rtl8139_port + RTL_REG_COMMAND,
         RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);

    net_if.initialized = true;
    vga_puts_success("Network: RTL8139 initialized (Ethernet polling)");
}

net_interface_t* net_get_interface(void) {
    return &net_if;
}

int net_send_packet(const void* data, usize len) {
    if (!net_if.initialized || !data || len == 0 || len > NET_MAX_PACKET_SIZE)
        return -1;

    /* Four device-owned DMA buffers avoid passing arbitrary caller memory to
     * the NIC and allow short frames to be padded to the Ethernet minimum. */
    u8 slot = tx_slot;
    tx_slot = (u8)((tx_slot + 1u) & 3u);
    usize frame_len = len < RTL_ETH_MIN_FRAME ? RTL_ETH_MIN_FRAME : len;
    kmemset(tx_buffers[slot], 0, frame_len);
    kmemcpy(tx_buffers[slot], data, len);

    u64 dma_addr = (u64)(uintptr_t)tx_buffers[slot];
    if (dma_addr > 0xFFFFFFFFULL)
        return -1;

    u16 status_reg = (u16)(rtl8139_port + RTL_REG_TXSTATUS0 + (u16)slot * 4u);
    u16 address_reg = (u16)(rtl8139_port + RTL_REG_TXADDR0 + (u16)slot * 4u);
    outl(address_reg, (u32)dma_addr);
    outl(status_reg, (u32)frame_len);

    for (volatile u32 i = 0; i < 1000000u; i++) {
        u32 status = inl(status_reg);
        if (status & RTL_TX_OK) {
            net_if.packets_tx++;
            return (int)len;
        }
        asm volatile("pause");
    }
    return -1;
}

int net_recv_packet(void* buffer, usize max_len) {
    if (!net_if.initialized || !buffer || max_len == 0)
        return -1;

    if (inb(rtl8139_port + RTL_REG_COMMAND) & RTL_CMD_RX_EMPTY)
        return 0;

    u16 current = (u16)(inw(rtl8139_port + RTL_REG_CBR) & RTL_RX_RING_MASK);
    if (rx_offset == current)
        return 0;

    u16 rx_status = ring_read16(rx_offset);
    u16 packet_len = ring_read16((u16)(rx_offset + 2u));
    if (!(rx_status & RTL_RX_OK) ||
        packet_len < RTL_ETH_CRC_SIZE ||
        packet_len > NET_MAX_PACKET_SIZE) {
        /* Corrupt ring entry: resynchronize to the device's current write
         * cursor rather than repeatedly parsing the same broken header. */
        rx_offset = (u16)(current & ~3u);
        outw(rtl8139_port + RTL_REG_CAPR,
             (u16)((rx_offset - 16u) & RTL_RX_RING_MASK));
        return 0;
    }

    usize frame_len = (usize)packet_len - RTL_ETH_CRC_SIZE;
    usize copy_len = frame_len < max_len ? frame_len : max_len;
    ring_copy_out((u8*)buffer,
                  (u16)((rx_offset + RTL_RX_HEADER_SIZE) & RTL_RX_RING_MASK),
                  copy_len);

    rx_offset = (u16)((rx_offset + RTL_RX_HEADER_SIZE + packet_len + 3u)
                      & ~3u);
    rx_offset &= RTL_RX_RING_MASK;
    outw(rtl8139_port + RTL_REG_CAPR,
         (u16)((rx_offset - 16u) & RTL_RX_RING_MASK));
    net_if.packets_rx++;
    return (int)copy_len;
}

void net_handle_tcpip(void) {
    /* Intentionally empty until an actual protocol stack is implemented.
     * Ethernet polling is available through net_recv_packet(). */
}
