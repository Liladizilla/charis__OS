#include <kernel/pci.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* 0x04030001 = class 0x04 (multimedia), subclass 0x03 (HDA audio),
     * programming interface 0, revision 1. */
    const u32 hda = 0x04030001U;
    assert(PCI_CLASS_CODE(hda) == 0x04);
    assert(PCI_SUBCLASS_CODE(hda) == 0x03);

    /* 0x02000010 = class 0x02 (network), subclass 0x00 (Ethernet). */
    const u32 ethernet = 0x02000010U;
    assert(PCI_CLASS_CODE(ethernet) == 0x02);
    assert(PCI_SUBCLASS_CODE(ethernet) == 0x00);

    /* Verify 32-bit BAR values are not narrowed to 16 bits. */
    pci_device_t device = {0};
    device.bar[0] = 0xFEBF0000U;
    device.bar[1] = 0x0000C001U;
    assert(device.bar[0] == 0xFEBF0000U);
    assert(device.bar[1] == 0x0000C001U);

    puts("PCI configuration tests passed");
    return 0;
}
