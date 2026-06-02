#include <kernel/hda.h>
#include <kernel/memory.h>
#include <kernel/io.h>
#include <kernel/vga.h>

static hda_state_t g_hda_state = {0};
static u32* g_hda_buffer = NULL;

/* HDA register offsets */
#define HDA_REG_GCAP      0x00
#define HDA_REG_GCTL      0x08
#define HDA_REG_GSTS      0x010
#define HDA_REG_CORBL     0x040
#define HDA_REG_CORBU     0x044
#define HDA_REG_CORBL_E   0x048
#define HDA_REG_CORBU_E   0x04C
#define HDA_REG_RIRBL     0x050
#define HDA_REG_RIRBU     0x054
#define HDA_REG_RIRBL_E   0x058
#define HDA_REG_RIRBU_E   0x05C
#define HDA_REG_BASE        0x1000

/* Stream register offsets */
#define HDA_STREAM_SDCTL    0x00
#define HDA_STREAM_SDSTS    0x04
#define HDA_STREAM_SDIID    0x08
#define HDA_STREAM_SDLPIB   0x0C
#define HDA_STREAM_SDFIF    0x10
#define HDA_STREAM_SDPMIN   0x14
#define HDA_STREAM_SDPSP    0x18
#define HDA_STREAM_SDNXTT   0x20
#define HDA_STREAM_SDDP     0x28
#define HDA_STREAM_SDLPIBA  0x30
#define HDA_STREAM_SDDL     0x30 /* Descriptor list lower - same as LPIBA */

/* GCTL bits */
#define HDA_GCTL_RESET    (1 << 0)
#define HDA_GCTL_UNSOL_ENABLE (1 << 8)

/* SDCTL bits */
#define HDA_SDCTL_RUN     (1 << 1)
#define HDA_SDCTL_DESPER  (1 << 4)

static u32 hda_read(u32 offset) {
    volatile u32* regs = (volatile u32*)(u64)g_hda_state.mmio_base;
    return regs[offset / 4];
}

static void hda_write(u32 offset, u32 value) {
    volatile u32* regs = (volatile u32*)(u64)g_hda_state.mmio_base;
    regs[offset / 4] = value;
}

static int hda_get_response(u32* response) {
    u32 rior = hda_read(HDA_REG_RIRBU);
    u32 rirb = hda_read(HDA_REG_RIRBL);
    if (rior & (1 << 4)) return -1; /* Unsol */
    *response = (rior << 32) | rirb;
    return 0;
}

/* Send command to codec and get response */
static u32 hda_send_cmd(u8 codec, u8 verb, u16 payload) {
    u32 cmd = (codec << 28) | (verb << 20) | payload;
    hda_write(HDA_REG_CORBL, cmd & 0xFFFFFFFF);
    hda_write(HDA_REG_CORBU, cmd >> 32);
    
    /* Wait for completion */
    for (int i = 0; i < 1000; i++) {
        if (hda_read(HDA_REG_GSTS) & (1 << 1)) break;
    }
    
    u32 response;
    hda_get_response(&response);
    return response;
}

int hda_init(void) {
    /* Find HDA controller via PCI */
    pci_device_t* dev = pci_find_device(0x8086, 0x2668); /* Intel ICH */
    if (!dev) {
        dev = pci_find_device(0x8086, 0x27D8); /* Intel HD Audio */
    }
    if (!dev) {
        dev = pci_find_device(0x1002, 0x4383); /* AMD */
    }
    
    if (!dev) {
        vga_puts("No HDA controller found\n");
        return -1;
    }
    
    g_hda_state.mmio_base = dev->bar[0];
    g_hda_state.codec_mask = 0;
    
    /* Reset controller */
    hda_write(HDA_REG_GCTL, 0);
    for (int i = 0; i < 100000; i++) {
        if (!(hda_read(HDA_REG_GSTS) & (1 << 0))) break;
    }
    
    hda_write(HDA_REG_GCTL, HDA_GCTL_RESET);
    for (int i = 0; i < 100000; i++) {
        if (hda_read(HDA_REG_GSTS) & (1 << 0)) break;
    }
    
    /* Enumerate codecs */
    u32 gcap = hda_read(HDA_REG_GCAP);
    int num_codecs = (gcap >> 8) & 0x0F;
    
    for (int c = 0; c < num_codecs && c < HDA_MAX_CODECS; c++) {
        u32 resp = hda_send_cmd(c, 0xF0000, 0); /* Get parameter */
        if (resp) {
            g_hda_state.codec_mask |= (1 << c);
        }
    }
    
    g_hda_state.stream_mask = 0;
    g_hda_state.buffer_addr = 0;
    g_hda_state.buffer_size = 0;
    g_hda_state.buffer_position = 0;
    
    vga_puts("HDA initialized\n");
    return 0;
}

void hda_init_buffer(usize size) {
    if (!g_hda_buffer) {
        g_hda_buffer = (u32*)kmalloc(size);
    }
}

int hda_setup_stream(u32 physical_buffer_addr, u32 buffer_size, int sample_rate, int channels, int bits) {
    int stream = 0; /* Use stream 0 for playback */
    u32 stream_base = HDA_REG_BASE + stream * 0x20;
    
    /* Reset stream */
    hda_write(stream_base + HDA_STREAM_SDCTL, 0);
    
    /* Set up descriptor list */
    hda_write(stream_base + HDA_STREAM_SDDL, physical_buffer_addr & 0xFFFFF0);
    hda_write(stream_base + HDA_STREAM_SDDL, (physical_buffer_addr >> 32) & 0xFFFFFFFF);
    
    /* Configure stream */
    u32 ctl = (sample_rate << 16) | (channels << 4) | ((bits == 16) ? 2 : 0);
    hda_write(stream_base + HDA_STREAM_SDCTL, ctl);
    
    g_hda_state.buffer_addr = physical_buffer_addr;
    g_hda_state.buffer_size = buffer_size;
    g_hda_state.sample_rate = sample_rate;
    g_hda_state.channels = channels;
    g_hda_state.bits_per_sample = bits;
    
    return 0;
}

int hda_queue_audio(const void* data, usize size) {
    if (!g_hda_buffer || !data) return -1;
    
    /* Copy data and start stream if not running */
    for (usize i = 0; i < size && i < g_hda_state.buffer_size; i += 4) {
        g_hda_buffer[g_hda_state.buffer_position + i / 4] = ((u32*)data)[i / 4];
    }
    
    /* Start stream */
    hda_write(HDA_REG_BASE + HDA_STREAM_SDCTL, 
              hda_read(HDA_REG_BASE + HDA_STREAM_SDCTL) | HDA_SDCTL_RUN);
    
    return 0;
}

void hda_stop(void) {
    hda_write(HDA_REG_BASE + HDA_STREAM_SDCTL, 
              hda_read(HDA_REG_BASE + HDA_STREAM_SDCTL) & ~HDA_SDCTL_RUN);
}