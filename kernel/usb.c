#include <kernel/usb.h>
#include <kernel/pci.h>
#include <kernel/vga.h>
#include <kernel/memory.h>

static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;
static usb_controller_t usb_controllers[USB_MAX_DEVICES];
static int usb_controller_count = 0;

static usb_controller_type_t usb_controller_type(u8 prog_if) {
    switch (prog_if) {
    case 0x00: return USB_CTRL_UHCI;
    case 0x10: return USB_CTRL_OHCI;
    case 0x20: return USB_CTRL_EHCI;
    case 0x30: return USB_CTRL_XHCI;
    default: return USB_CTRL_UNKNOWN;
    }
}

void usb_init(void) {
    usb_device_count = 0;
    usb_controller_count = 0;
    kmemset(usb_devices, 0, sizeof(usb_devices));
    kmemset(usb_controllers, 0, sizeof(usb_controllers));

    /* Inventory host controllers from PCI class 0x0C, subclass 0x03.
     * This is discovery only: no controller BAR is mapped, no bus mastering
     * is enabled, and no USB device enumeration is claimed here. */
    for (int i = 0; i < pci_count && usb_controller_count < USB_MAX_DEVICES; i++) {
        pci_device_t* pci = pci_get_device(i);
        if (!pci || !pci->present ||
            pci->pci_class != 0x0C || pci->pci_subclass != 0x03) continue;

        usb_controller_t* controller = &usb_controllers[usb_controller_count++];
        controller->vendor_id = pci->vendor_id;
        controller->device_id = pci->device_id;
        controller->bus = pci->bus;
        controller->device = pci->device;
        controller->function = pci->function;
        controller->prog_if = pci->prog_if;
        controller->type = usb_controller_type(pci->prog_if);

        const char* name = controller->type == USB_CTRL_UHCI ? "UHCI" :
                           controller->type == USB_CTRL_OHCI ? "OHCI" :
                           controller->type == USB_CTRL_EHCI ? "EHCI" :
                           controller->type == USB_CTRL_XHCI ? "xHCI" : "unknown";
        vga_printf("USB: PCI host controller %s %04x:%04x at %u:%u.%u\n",
                   name, controller->vendor_id, controller->device_id,
                   controller->bus, controller->device, controller->function);
    }

    if (usb_controller_count) {
        vga_printf("USB: inventoried %d host controller(s); enumeration and HID are not implemented\n",
                   usb_controller_count);
    } else {
        vga_puts("USB: no PCI host controller found; USB enumeration unavailable\n");
    }
}

int usb_poll(void) {
    /* No host-controller driver has been bound yet, so no transfer ring or
     * endpoint polling can occur. Keep this explicitly inert. */
    return usb_device_count;
}

int usb_get_controller_count(void) {
    return usb_controller_count;
}

usb_controller_t* usb_get_controller(int index) {
    if (index < 0 || index >= usb_controller_count) return NULL;
    return &usb_controllers[index];
}

usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= USB_MAX_DEVICES) return NULL;
    return &usb_devices[index];
}

int usb_get_device_count(void) {
    return usb_device_count;
}
