/* driver.h - Driver Framework for CharisOS */
#pragma once
#include <kernel/types.h>
#include <kernel/pci.h>

typedef struct {
    const char* name;
    int (*probe)(pci_device_t* dev);    /* Called when matching device found */
    void (*remove)(pci_device_t* dev);  /* Called on device removal */
    uint32_t vendor_id;                 /* 0 = match any */
    uint32_t device_id;                 /* 0 = match any */
    uint8_t pci_class;
    uint8_t pci_subclass;
} driver_t;

void driver_init(void);
void driver_register(driver_t* drv);
void driver_scan_and_bind(void);

/* Driver list is exported for use by subsystems */
extern driver_t* driver_list;
extern int driver_count;
extern int driver_capacity;

/* Helper for registering built-in drivers */
#define DRIVER_REGISTER(drv) __attribute__((constructor)) static void _register_##drv(void) { driver_register(&drv); }

/* PCI class codes */
#define PCI_CLASS_STORAGE     0x01
#define PCI_CLASS_NETWORK     0x02
#define PCI_CLASS_DISPLAY     0x03
#define PCI_CLASS_INPUT       0x09
#define PCI_CLASS_SERIAL      0x07

/* Storage subclasses */
#define PCI_SUBCLASS_IDE      0x01
#define PCI_SUBCLASS_AHCI     0x06
#define PCI_SUBCLASS_NVME     0x08

/* Network subclasses */
#define PCI_SUBCLASS_ETHERNET 0x00
#define PCI_SUBCLASS_USB      0x00  /* USB controllers */

/* Display subclasses */
#define PCI_SUBCLASS_VGA      0x00
#define PCI_SUBCLASS_XHCI     0x03  /* USB 3.0 controller */

/* Input subclasses */
#define PCI_SUBCLASS_PS2      0x00
#define PCI_SUBCLASS_USB_HID  0x00  /* HID on USB */