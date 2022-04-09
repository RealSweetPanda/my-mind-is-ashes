#include "bootparam.h"
#define PIC1		0x20		/* IO base address for master PIC */
#define PIC2		0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)
#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x11		/* Initialization - required! */

#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x11		/* Special fully nested (not) */


static inline void port_out_b(unsigned short port, unsigned char val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline unsigned char port_in_b(unsigned short port)
{
    unsigned char ret;
    asm volatile ( "inb %1, %0"
    : "=a"(ret)
    : "Nd"(port) );
    return ret;
}
static inline void io_wait(void)
{
    port_out_b(0x80, 0);
}
#define PIC_EOI		0x20		/* End-of-interrupt command code */

void pic_eoi(unsigned char irq)
{
    if(irq >= 8)
        port_out_b(PIC2_COMMAND,PIC_EOI);

    port_out_b(PIC1_COMMAND,PIC_EOI);
}
#define PIC1_CMD                    0x20
#define PIC2_CMD                    0xA0
#define PIC_READ_IRR                0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR                0x0b    /* OCW3 irq service next CMD read */

/* Helper func */
static unsigned short __pic_get_irq_reg(int ocw3)
{
    /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
     * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
    port_out_b(PIC1_CMD, ocw3);
    port_out_b(PIC2_CMD, ocw3);
    return (port_in_b(PIC2_CMD) << 8) | port_in_b(PIC1_CMD);
}

/* Returns the combined value of the cascaded PICs irq request register */
unsigned short pic_get_irr(void)
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}

/* Returns the combined value of the cascaded PICs in-service register */
unsigned short pic_get_isr(void)
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}
typedef struct {
    long long bitmap[16 * 16];
} cursor_t;

#define Y 0x007a7a7a
#define X 0x00858585
#define t 0x00808080
#define c 0x00696969
#define b 0x00616161
#define a 0x00545454
#define w 0x007d7d7d
#define o (-1)
// cool
static cursor_t cursor = {
        {
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, t, X, o, o, o, o, o, o, o,
                w, w, w, w, w, w, w, w, X, Y, Y, Y, Y, Y, Y, Y,
                a, a, a, a, a, a, a, a, b, b, b, b, b, b, b, b,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
                o, o, o, o, o, o, o, a, c, o, o, o, o, o, o, o,
        }
};
#undef Y
#undef X
#undef t
#undef c
#undef b
#undef a
#undef w
#undef o
static int c_needs_refresh = 0;
static unsigned long *c_framebuffer;
static int c_screen_width;
static int c_screen_height;
static int c_screen_pitch;
static unsigned long long c_fb_size;
static int c_mouse_x = 0;
static int c_mouse_y = 0;
static int last_mouse_x = 0;
static int last_mouse_y = 0;
static unsigned long *antibuffer;
static unsigned long *prevbuffer;
static unsigned long long bump_allocator_base = 0x1000000;

static void *balloc_aligned(unsigned long long count, unsigned long long alignment) {
    unsigned long long new_base = bump_allocator_base;
    if (new_base & (alignment - 1)) {
        new_base &= ~(alignment - 1);
        new_base += alignment;
    }
    void *ret = (void *)new_base;
    new_base += count;
    bump_allocator_base = new_base;
    return ret;
}

void *c_malloc(unsigned long long count) {
    return balloc_aligned(count, 4);
}

void c_free(void *ptr) {
    (void)ptr;
}

static void *c_alloc(unsigned long long size) {
    unsigned char *ptr = c_malloc(size);

    if (!ptr)
        return (void *)0;

    for (unsigned long long i = 0; i < size; i++)
        ptr[i] = 0;

    return (void *)ptr;
}

static void plot_px_direct(int x, int y, unsigned long hex) {
    if (x >= c_screen_width || y >= c_screen_height || x < 0 || y < 0)
        return;

    unsigned long long fb_i = x + (c_screen_pitch / sizeof(unsigned long)) * y;

    c_framebuffer[fb_i] = hex;

    return;
}

static unsigned long get_px(int x, int y) {
    if (x >= c_screen_width || y >= c_screen_height || x < 0 || y < 0)
        return 0;

    unsigned long long fb_i = x + (c_screen_pitch / sizeof(unsigned long)) * y;

    return antibuffer[fb_i];
}

static void c_update_cursor(void) {
    for (unsigned long long x = 0; x < 16; x++) {
        for (unsigned long long y = 0; y < 16; y++) {
            if (cursor.bitmap[x * 16 + y] != -1) {
                unsigned long px = get_px(last_mouse_x + x, last_mouse_y + y);
                plot_px_direct(last_mouse_x + x, last_mouse_y + y, px);
            }
        }
    }
    for (unsigned long long x = 0; x < 16; x++) {
        for (unsigned long long y = 0; y < 16; y++) {
            if (cursor.bitmap[x * 16 + y] != -1) {
                plot_px_direct(c_mouse_x + x, c_mouse_y + y, cursor.bitmap[x * 16 + y]);
            }
        }
    }
    last_mouse_x = c_mouse_x;
    last_mouse_y = c_mouse_y;
    return;
}

void c_refresh(void) {
    if (!c_needs_refresh)
        return;

    c_needs_refresh = 0;

    unsigned long *tmpbufptr = prevbuffer;
    prevbuffer = antibuffer;
    antibuffer = tmpbufptr;

    /* copy over the buffer */
    for (unsigned long long i = 0; i < c_fb_size / sizeof(unsigned long); i++) {
        if (antibuffer[i] != prevbuffer[i])
            c_framebuffer[i] = antibuffer[i];
    }

    c_update_cursor();

    return;
}

int c_init(unsigned long *fb, int scrn_width, int scrn_height, int scrn_pitch) {
    c_framebuffer = fb;
    c_screen_width = scrn_width;
    c_screen_height = scrn_height;
    c_screen_pitch = scrn_pitch;

    c_mouse_x = c_screen_width / 2;
    c_mouse_y = c_screen_height / 2;

    c_fb_size = (c_screen_pitch / sizeof(unsigned long)) * c_screen_height * sizeof(unsigned long);

    antibuffer = c_alloc(c_fb_size);

    if (!antibuffer)
        return -1;

    prevbuffer = c_alloc(c_fb_size);

    if (!prevbuffer) {
        c_free(antibuffer);
        return -1;
    }

    c_needs_refresh = 1;
    c_refresh();

    return 0;
}

void c_set_cursor_pos(int x, int y) {
    if (c_mouse_x + x < 0) {
        c_mouse_x = 0;
    } else if (c_mouse_x + x >= c_screen_width) {
        c_mouse_x = c_screen_width - 1;
    } else {
        c_mouse_x += x;
    }

    if (c_mouse_y + y < 0) {
        c_mouse_y = 0;
    } else if (c_mouse_y + y >= c_screen_height) {
        c_mouse_y = c_screen_height - 1;
    } else {
        c_mouse_y += y;
    }

    c_update_cursor();

    return;
}

void c_get_cursor_pos(int *x, int *y) {
    *x = c_mouse_x;
    *y = c_mouse_y;

    return;
}
static unsigned long long ticks = 0;

#define PIT_FREQUENCY_HZ 1000

static void init_pit(void) {
    unsigned short xb = 1193182 / PIT_FREQUENCY_HZ;
    if ((1193182 % PIT_FREQUENCY_HZ) > (PIT_FREQUENCY_HZ / 2))
        xb++;

    port_out_b(0x40, (unsigned char)(xb & 0x00ff));
    port_out_b(0x40, (unsigned char)((xb & 0xff00) >> 8));
}

void pic_set_mask(unsigned char IRQline) {
    unsigned short port;
    unsigned char value;

    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = port_in_b(port) | (1 << IRQline);
    port_out_b(port, value);
}

void pic_remap(int offset1, int offset2)
{
    unsigned char a1, a2;

    a1 = port_in_b(PIC1_DATA);                        // save masks
    a2 = port_in_b(PIC2_DATA);

    port_out_b(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
    io_wait();
    port_out_b(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    port_out_b(PIC1_DATA, offset1);                 // ICW2: Master PIC vector offset
    io_wait();
    port_out_b(PIC2_DATA, offset2);                 // ICW2: Slave PIC vector offset
    io_wait();
    port_out_b(PIC1_DATA, 4);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    io_wait();
    port_out_b(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    port_out_b(PIC1_DATA, ICW4_8086);
    io_wait();
    port_out_b(PIC2_DATA, ICW4_8086);
    io_wait();

    port_out_b(PIC1_DATA, a1);   // restore saved masks.
    port_out_b(PIC2_DATA, a2);
}

static inline void mouse_wait(int type) {
    int timeout = 100000;

    if (type == 0) {
        while (timeout--) {
            if (port_in_b(0x64) & (1 << 0)) {
                return;
            }
        }
    } else {
        while (timeout--) {
            if (!(port_in_b(0x64) & (1 << 1))) {
                return;
            }
        }
    }
}

static inline void mouse_write(unsigned char val) {
    mouse_wait(1);
    port_out_b(0x64, 0xd4);
    mouse_wait(1);
    port_out_b(0x60, val);
}

static inline unsigned char mouse_read(void) {
    mouse_wait(0);
    return port_in_b(0x60);
}

static void init_mouse(void) {
    mouse_wait(1);
    port_out_b(0x64, 0xa8);

    mouse_wait(1);
    port_out_b(0x64, 0x20);
    unsigned char status = mouse_read();
    mouse_read();
    status |= (1 << 1);
    status &= ~(1 << 5);
    mouse_wait(1);
    port_out_b(0x64, 0x60);
    mouse_wait(1);
    port_out_b(0x60, status);
    mouse_read();

    mouse_write(0xff);
    mouse_read();

    mouse_write(0xf6);
    mouse_read();

    mouse_write(0xf4);
    mouse_read();
}

typedef struct {
    unsigned char flags;
    unsigned char x_mov;
    unsigned char y_mov;
} mouse_packet_t;

static int handler_cycle = 0;
static mouse_packet_t current_packet;
static int discard_packet = 0;

__attribute__((interrupt)) static void mouse_handler(void *p) {
    (void)p;

    // we will get some spurious packets at the beginning and they will fuck
    // up the alignment of the handler cycle so just ignore everything in
    // the first 250 milliseconds after boot
    if (ticks < 250) {
        port_in_b(0x60);
        goto out;
    }

    switch (handler_cycle) {
        case 0:
            current_packet.flags = port_in_b(0x60);
            handler_cycle++;
            if (current_packet.flags & (1 << 6) || current_packet.flags & (1 << 7))
                discard_packet = 1;     // discard rest of packet
            if (!(current_packet.flags & (1 << 3)))
                discard_packet = 1;     // discard rest of packet
            break;
        case 1:
            current_packet.x_mov = port_in_b(0x60);
            handler_cycle++;
            break;
        case 2: {
            current_packet.y_mov = port_in_b(0x60);
            handler_cycle = 0;

            if (discard_packet) {
                discard_packet = 0;
                break;
            }

            // process packet
            long long x_mov, y_mov;

            if (current_packet.flags & (1 << 4)) {
                x_mov = (char)current_packet.x_mov;
            } else
                x_mov = current_packet.x_mov;

            if (current_packet.flags & (1 << 5)) {
                y_mov = (char)current_packet.y_mov;
            } else
                y_mov = current_packet.y_mov;

            int last_x, last_y;

            c_get_cursor_pos(&last_x, &last_y);
            c_set_cursor_pos(x_mov, -y_mov);

            break;
        }
    }

    out:
    pic_eoi(12);
}

struct idt_entry_t {
    unsigned short offset_lo;
    unsigned short selector;
    unsigned char  ist;
    unsigned char  type_attr;
    unsigned short offset_mid;
    unsigned long offset_hi;
    unsigned long zero;
} __attribute__((packed));

struct idt_ptr_t {
    unsigned short size;
    unsigned long long address;
} __attribute__((packed));

static struct idt_entry_t idt[256] = {0};

static void register_interrupt_handler(unsigned long long vec, void *handler, unsigned char ist, unsigned char type) {
    unsigned long long p = (unsigned long long)handler;

    idt[vec].offset_lo  = (unsigned short)p;
    idt[vec].selector   = 0x28;
    idt[vec].ist        = ist;
    idt[vec].type_attr  = type;
    idt[vec].offset_mid = (unsigned short)(p >> 16);
    idt[vec].offset_hi  = (unsigned long)(p >> 32);
    idt[vec].zero       = 0;
}

__attribute__((interrupt)) static void unhandled_interrupt(void *p) {
    (void)p;
    asm volatile ("cli");
    asm volatile ("1: hlt");
    asm volatile ("jmp 1b");
}
__attribute__((interrupt)) static void pit_handler(void *p) {
    (void)p;

    ticks++;

    // refresh wm at 30 hz
    if (!(ticks % (PIT_FREQUENCY_HZ / 30))) {
        c_refresh();
    }

    pic_eoi(0);
}

static void init_idt(void) {

    register_interrupt_handler(0x20 + 0 , pit_handler,   0, 0x8e);
    register_interrupt_handler(0x20 + 12, mouse_handler, 0, 0x8e);
    for (unsigned long long i = 0; i < 256; i++)
        register_interrupt_handler(i, unhandled_interrupt, 0, 0x8e);


    struct idt_ptr_t idt_ptr = {
            sizeof(idt) - 1,
            (unsigned long long)idt
    };
    asm volatile ("lidt %0" : : "m"(idt_ptr));
}

bootparam_t *bootp;

void setPixel(unsigned int xx, unsigned int yy, unsigned int color) {
    *((unsigned int *)(bootp->framebuffer + bootp->width * yy + xx)) = color;
}

void _start(bootparam_t *bootparam) {
    bootp = bootparam;
    pic_remap(0x20, 0x28);
    init_idt();

    // enable cascade
    pic_set_mask(2);

    init_pit();
    pic_set_mask(0);

    init_mouse();
    pic_set_mask(12);

    c_init(bootp->framebuffer,
                bootp->width,
                bootp->height,
                bootp->pitch);
    for (int x = 0; x < 256; x++) {
        for (int y = 0; y < 256; y++) {
            setPixel(x, y, 0xFF0000);
//            setPixel(x, y, (x / 16 % 2 == 0 ^ y / 16 % 2 == 1) ? rgb(200, 0, 255) : rgb(0, 164, 255));
        }
    }
    asm volatile ("sti");
    while(1);
}
