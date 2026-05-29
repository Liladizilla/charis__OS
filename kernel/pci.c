#include <kernel/pci.h>
#include <kernel/io.h>
#include <kernel/vga.h>
#include <kernel/memory.h>

static pci_device_t pci_devices[256];
static int pci_count = 0;

int pci_scan(void) {
    pci_count = 0;
    
    for (u8 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 addr = (bus << 16) | (dev << 11) | (func << 8) | 0x80000000;
                outl(0xCF8, addr);
                u32 ids = inl(0xCFC);
                
                u16 vendor = ids & 0xFFFF;
                u16 device = (ids >> 16) & 0xFFFF;
                
                if (vendor == 0xFFFF) continue;
                
                if (pci_count < 256) {
                    pci_device_t* d = &pci_devices[pci_count];
                    d->vendor_id = vendor;
                    d->device_id = device;
                    d->bus = bus;
                    d->device = dev;
                    d->function = func;
                    d->present = true;
                    
                    // Read BARs
                    for (int i = 0; i < 6; i++) {
                        outl(0xCF8, addr | (0x10 + i * 4));
                        d->bar[i] = inl(0xCFC);
                    }
                    
                    // Read IRQ
                    outl(0xCF8, addr | 0x3C);
                    d->irq = (inl(0xCFC) >> 8) & 0xFF;
                    
                    pci_count++;
                }
            }
        }
    }
    
    kprintf("PCI: Found %d devices\n", pci_count);
    return pci_count;
}

pci_device_t* pci_get_device(int index) {
    if (index < 0 || index >= pci_count) return NULL;
    return &pci_devices[index];
}

pci_device_t* pci_find_device(u16 vendor, u16 device) {
    for (int i = 0; i < pci_count; i++) {
        if (pci_devices[i].vendor_id == vendor && 
            pci_devices[i].device_id == device) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

u32 pci_read_config_dword(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 addr = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000;
    outl(0xCF8, addr);
    return inl(0xCFC);
}

u16 pci_read_config_word(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 val = pci_read_config_dword(bus, dev, func, offset);
    return (offset & 2) ? (val >> 16) : (val & 0xFFFF);
}

void pci_write_config_dword(u8 bus, u8 dev, u8 func, u8 offset, u32 value) {
    u32 addr = (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000;
    outl(0xCF8, addr);
    outl(0xCFC, value);
}