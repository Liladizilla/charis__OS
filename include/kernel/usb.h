/* usb.h - USB driver interface for CharisOS */
#pragma once
#include <kernel/types.h>

#define USB_MAX_DEVICES     16
#define USB_MAX_ENDPOINTS   4

typedef enum {
    USB_DEV_UNKNOWN,
    USB_DEV_KEYBOARD,
    USB_DEV_MOUSE,
    USB_DEV_STORAGE,
    USB_DEV_HID
} usb_device_type_t;

typedef enum {
    USB_CTRL_UNKNOWN,
    USB_CTRL_UHCI,
    USB_CTRL_OHCI,
    USB_CTRL_EHCI,
    USB_CTRL_XHCI
} usb_controller_type_t;

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u8 bus;
    u8 device;
    u8 function;
    u8 prog_if;
    usb_controller_type_t type;
} usb_controller_t;

typedef struct {
    u16 vendor_id;
    u16 product_id;
    usb_device_type_t type;
    u8 address;
    bool initialized;
} usb_device_t;

void usb_init(void);
int usb_poll(void);
usb_device_t* usb_get_device(int index);
int usb_get_device_count(void);
int usb_get_controller_count(void);
usb_controller_t* usb_get_controller(int index);