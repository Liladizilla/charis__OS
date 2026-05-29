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