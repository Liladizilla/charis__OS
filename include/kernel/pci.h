/* pci.h - PCI enumeration for CharisOS */
#pragma once
#include <kernel/types.h>

#define PCI_MAX_BUS      256
#define PCI_MAX_DEV      32
#define PCI_MAX_FUNC     8

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u8 bus, device, function;
    u16 bar[6];
    u8 irq;
    bool present;
} pci_device_t;

int pci_scan(void);
pci_device_t* pci_get_device(int index);
pci_device_t* pci_find_device(u16 vendor, u16 device);
u32 pci_read_config_dword(u8 bus, u8 dev, u8 func, u8 offset);
u16 pci_read_config_word(u8 bus, u8 dev, u8 func, u8 offset);
void pci_write_config_dword(u8 bus, u8 dev, u8 func, u8 offset, u32 value);