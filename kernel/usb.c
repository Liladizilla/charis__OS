#include <kernel/usb.h>
#include <kernel/io.h>
#include <kernel/vga.h>
#include <kernel/memory.h>

static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;

void usb_init(void) {
    usb_device_count = 0;
    // Reset USB controller
    /* USB enumeration is intentionally deferred until PCI binding is active. */
    
    vga_puts("USB: OHCI/EHCI controller detected (simulated)\n");
}

int usb_poll(void) {
    // Check for new devices (would enumerate PCI for real implementation)
    return usb_device_count;
}

usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= USB_MAX_DEVICES) return NULL;
    return &usb_devices[index];
}

int usb_get_device_count(void) {
    return usb_device_count;
}