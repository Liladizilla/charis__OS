#include <kernel/driver.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/pci.h>
#include <kernel/hda.h>

driver_t* driver_list = NULL;
int driver_count = 0;
int driver_capacity = 0;

static int hda_driver_probe(pci_device_t* dev) {
    return hda_init();
}

static driver_t hda_driver = {
    .name = "intel_hda",
    .probe = hda_driver_probe,
    .remove = NULL,
    .vendor_id = 0,
    .device_id = 0,
    .pci_class = 0, /* Match any */
    .pci_subclass = 0
};

void driver_init(void) {
    driver_list = (driver_t*)kmalloc(64 * sizeof(driver_t));
    if (driver_list) {
        driver_capacity = 64;
        driver_count = 0;
    }
    driver_register(&hda_driver);
    vga_puts("Driver framework initialized\n");
}

void driver_register(driver_t* drv) {
    if (!drv) return;
    if (driver_count >= driver_capacity) {
        /* Resize array */
        int new_cap = driver_capacity * 2;
        driver_t* new_list = (driver_t*)kmalloc(new_cap * sizeof(driver_t));
        if (!new_list) return;
        for (int i = 0; i < driver_count; i++) {
            new_list[i] = driver_list[i];
        }
        kfree(driver_list);
        driver_list = new_list;
        driver_capacity = new_cap;
    }
    
    driver_list[driver_count++] = *drv;
    kprintf("Registered driver: %s\n", drv->name);
}

void driver_scan_and_bind(void) {
    /* Walk all PCI devices and find matching drivers */
    for (int i = 0; i < pci_count; i++) {
        pci_device_t* dev = pci_get_device(i);
        if (!dev || !dev->present) continue;
        
        /* Find matching driver */
        for (int j = 0; j < driver_count; j++) {
            driver_t* drv = &driver_list[j];
            bool vendor_match = (drv->vendor_id == 0 || drv->vendor_id == dev->vendor_id);
            bool device_match = (drv->device_id == 0 || drv->device_id == dev->device_id);
            bool class_match = (drv->pci_class == 0 || drv->pci_class == dev->pci_class);
            
            if (vendor_match && device_match && class_match) {
                kprintf("Probing driver %s for PCI %x:%x\n", drv->name, 
                        dev->vendor_id, dev->device_id);
                if (drv->probe) {
                    int result = drv->probe(dev);
                    if (result == 0) {
                        kprintf("Driver %s bound successfully\n", drv->name);
                    } else {
                        kprintf("Driver %s probe failed: %d\n", drv->name, result);
                    }
                }
                break; /* One driver per device */
            }
        }
    }
}