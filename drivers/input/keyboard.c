// ============================
// GraceOS PS/2 Keyboard Driver
// ============================

#include "keyboard.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/log/klog.h"

/* Keyboard I/O Ports */
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_CMD_PORT     0x64

/* Keyboard buffer */
#define KB_BUFFER_SIZE  256
static unsigned char kb_buffer[KB_BUFFER_SIZE];
static size_t kb_read_pos = 0;
static size_t kb_write_pos = 0;

/* Keyboard state */
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int capslock_on = 0;
static int extended_key = 0;  /* E0 prefix detected */
static volatile int kb_irq_seen = 0;
static int kb_irq_logged = 0;
static int kb_poll_logged = 0;

/* US QWERTY Keyboard Layout - Normal (lowercase) */
static const char scancode_to_ascii[] = {
    0,    0,   '1',  '2',  '3',  '4',  '5',  '6',    // 0x00-0x07
    '7',  '8', '9',  '0',  '-',  '=',  '\b', '\t',   // 0x08-0x0F
    'q',  'w', 'e',  'r',  't',  'y',  'u',  'i',    // 0x10-0x17
    'o',  'p', '[',  ']',  '\n', 0,    'a',  's',    // 0x18-0x1F
    'd',  'f', 'g',  'h',  'j',  'k',  'l',  ';',    // 0x20-0x27
    '\'', '`', 0,    '\\', 'z',  'x',  'c',  'v',    // 0x28-0x2F
    'b',  'n', 'm',  ',',  '.',  '/',  0,    '*',    // 0x30-0x37
    0,    ' ', 0,    0,    0,    0,    0,    0,      // 0x38-0x3F
    0,    0,   0,    0,    0,    0,    0,    '7',    // 0x40-0x47
    '8',  '9', '-',  '4',  '5',  '6',  '+',  '1',    // 0x48-0x4F
    '2',  '3', '0',  '.',  0,    0,    0,    0,      // 0x50-0x57
    0,    0                                           // 0x58-0x59
};

/* US QWERTY Keyboard Layout - Shift (uppercase) */
static const char scancode_to_ascii_shift[] = {
    0,    0,   '!',  '@',  '#',  '$',  '%',  '^',    // 0x00-0x07
    '&',  '*', '(',  ')',  '_',  '+',  '\b', '\t',   // 0x08-0x0F
    'Q',  'W', 'E',  'R',  'T',  'Y',  'U',  'I',    // 0x10-0x17
    'O',  'P', '{',  '}',  '\n', 0,    'A',  'S',    // 0x18-0x1F
    'D',  'F', 'G',  'H',  'J',  'K',  'L',  ':',    // 0x20-0x27
    '"',  '~', 0,    '|',  'Z',  'X',  'C',  'V',    // 0x28-0x2F
    'B',  'N', 'M',  '<',  '>',  '?',  0,    '*',    // 0x30-0x37
    0,    ' ', 0,    0,    0,    0,    0,    0,      // 0x38-0x3F
    0,    0,   0,    0,    0,    0,    0,    '7',    // 0x40-0x47
    '8',  '9', '-',  '4',  '5',  '6',  '+',  '1',    // 0x48-0x4F
    '2',  '3', '0',  '.',  0,    0,    0,    0,      // 0x50-0x57
    0,    0                                           // 0x58-0x59
};


static int kb_wait_input_clear(uint32_t limit)
{
    for (uint32_t i = 0; i < limit; i++)
    {
        if ((inb(KB_STATUS_PORT) & 0x02) == 0)
            return 1;
    }
    return 0;
}

static int kb_wait_output_full(uint32_t limit)
{
    for (uint32_t i = 0; i < limit; i++)
    {
        if (inb(KB_STATUS_PORT) & 0x01)
            return 1;
    }
    return 0;
}

static void kb_flush_output(void)
{
    for (uint32_t i = 0; i < 32; i++)
    {
        if ((inb(KB_STATUS_PORT) & 0x01) == 0)
            break;
        (void)inb(KB_DATA_PORT);
    }
}

static void kb_write_cmd(uint8_t cmd)
{
    if (kb_wait_input_clear(100000))
        outb(KB_CMD_PORT, cmd);
}

static void kb_write_data(uint8_t data)
{
    if (kb_wait_input_clear(100000))
        outb(KB_DATA_PORT, data);
}

static uint8_t kb_read_data(void)
{
    if (!kb_wait_output_full(100000))
        return 0;
    return inb(KB_DATA_PORT);
}

/* Initialize keyboard */
void keyboard_init(void)
{
    kb_read_pos = 0;
    kb_write_pos = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    capslock_on = 0;
    extended_key = 0;
    kb_irq_seen = 0;

    /* Basic PS/2 controller init: enable IRQ1 and keyboard scanning. */
    kb_flush_output();

    kb_write_cmd(0xAD); /* Disable first PS/2 port */
    kb_write_cmd(0xA7); /* Disable second PS/2 port */

    kb_write_cmd(0x20); /* Read controller config */
    uint8_t cfg = kb_read_data();
    cfg |= 0x01;        /* Enable IRQ1 */
    cfg &= (uint8_t)~0x10; /* Enable first PS/2 port clock */
    kb_write_cmd(0x60); /* Write controller config */
    kb_write_data(cfg);

    kb_write_cmd(0xAE); /* Enable first PS/2 port */

    kb_write_data(0xF4); /* Enable keyboard scanning */
    (void)kb_read_data(); /* Read ACK if present */

    kb_flush_output();
}


/* Convert scancode to ASCII */
static char scancode_to_char(uint8_t scancode)
{
    if (scancode >= sizeof(scancode_to_ascii))
        return 0;

    char normal = scancode_to_ascii[scancode];
    char shifted = scancode_to_ascii_shift[scancode];

    /* Letters: CapsLock and Shift interact via XOR.
       Symbols/digits: only Shift selects the shifted table. */
    if (normal >= 'a' && normal <= 'z')
    {
        int upper = shift_pressed ^ capslock_on;
        return upper ? shifted : normal;
    }

    if (shift_pressed && shifted != 0)
        return shifted;

    return normal;
}


/* Add character to buffer */
static void kb_buffer_add(unsigned char c)
{
    size_t next_pos = (kb_write_pos + 1) % KB_BUFFER_SIZE;
    
    if (next_pos == kb_read_pos) {
        klog_warn("KB buffer full");
        return;
    }
    
    kb_buffer[kb_write_pos] = c;
    kb_write_pos = next_pos;
}


static void keyboard_process_scancode(uint8_t scancode)
{
    // Check for extended key prefix (E0)
    if (scancode == 0xE0)
    {
        extended_key = 1;
        return;
    }
    
    // Check if this is a key release (bit 7 set)
    if (scancode & 0x80)
    {
        // Key release
        scancode &= 0x7F;
        
        if (extended_key)
        {
            extended_key = 0;
            /* Handle extended key releases that affect modifier state */
            if (scancode == KEY_RCTRL)
                ctrl_pressed = 0;
            else if (scancode == KEY_RALT)
                alt_pressed = 0;
            return;
        }
        
        switch (scancode)
        {
            case KEY_LSHIFT:
            case KEY_RSHIFT:
                shift_pressed = 0;
                break;
            case KEY_LCTRL:
                ctrl_pressed = 0;
                break;
            case KEY_LALT:
                alt_pressed = 0;
                break;
        }
    }
    else
    {
        // Key press
        if (extended_key)
        {
            extended_key = 0;
            // Handle extended keys (arrow keys)
            switch (scancode)
            {
                case KEY_UP:
                    kb_buffer_add(KEY_SPECIAL_UP);
                    break;
                case KEY_DOWN:
                    kb_buffer_add(KEY_SPECIAL_DOWN);
                    break;
                case KEY_LEFT:
                    kb_buffer_add(KEY_SPECIAL_LEFT);
                    break;
                case KEY_RIGHT:
                    kb_buffer_add(KEY_SPECIAL_RIGHT);
                    break;
                case KEY_HOME:
                    kb_buffer_add(KEY_SPECIAL_HOME);
                    break;
                case KEY_END:
                    kb_buffer_add(KEY_SPECIAL_END);
                    break;
                case KEY_PGUP:
                    kb_buffer_add(KEY_SPECIAL_PGUP);
                    break;
                case KEY_PGDN:
                    kb_buffer_add(KEY_SPECIAL_PGDN);
                    break;
                case KEY_INS:
                    kb_buffer_add(KEY_SPECIAL_INS);
                    break;
                case KEY_DEL:
                    kb_buffer_add(KEY_SPECIAL_DEL);
                    break;
                case KEY_RCTRL:
                    ctrl_pressed = 1;
                    break;
                case KEY_RALT:
                    alt_pressed = 1;
                    break;
                case KEY_SPACE:
                    kb_buffer_add(' ');
                    break;
            }
            return;
        }
        switch (scancode)
        {
            case KEY_LSHIFT:
            case KEY_RSHIFT:
                shift_pressed = 1;
                break;
                
            case KEY_LCTRL:
                ctrl_pressed = 1;
                break;
                
            case KEY_LALT:
                alt_pressed = 1;
                break;
                
            case KEY_CAPSLOCK:
                capslock_on = !capslock_on;
                break;
                
            default:
                // Handle Ctrl key combinations (ASCII control codes)
                if (ctrl_pressed && scancode >= 0x10 && scancode <= 0x32)
                {
                    char c = 0;
                    switch (scancode)
                    {
                        case 0x2E:    // C key
                            c = 0x03; // ASCII ETX (Ctrl+C)
                            break;
                        case 0x1F:    // S key
                            c = 0x13; // ASCII DC3 (Ctrl+S)
                            break;
                        case 0x2D:    // X key
                            c = 0x18; // ASCII CAN (Ctrl+X)
                            break;
                    }
                    if (c != 0)
                    {
                        kb_buffer_add(c);
                        return;
                    }
                }
                else
                {
                    // Convert scancode to ASCII and add to buffer
                    char c = scancode_to_char(scancode);
                    if (c != 0)
                    {
                        kb_buffer_add(c);
                    }
                }
                break;
        }
    }
}

static void keyboard_poll_once(void)
{
    uint8_t status = inb(KB_STATUS_PORT);
    if (status & 0x01)
    {
        uint8_t scancode = inb(KB_DATA_PORT);
        if (!kb_poll_logged)
        {
            klog_warn("keyboard: polled scancode");
            kb_poll_logged = 1;
        }
        keyboard_process_scancode(scancode);
    }
}

/* Keyboard interrupt handler */
void keyboard_handler(void)
{
    uint8_t scancode = inb(KB_DATA_PORT);
    kb_irq_seen = 1;
    if (!kb_irq_logged)
    {
        klog_warn("keyboard: IRQ scancode");
        kb_irq_logged = 1;
    }
    keyboard_process_scancode(scancode);
}


/* Check if character is available */
int keyboard_haschar(void)
{
    return kb_read_pos != kb_write_pos;
}


/* Get character from buffer (blocking) */
unsigned char keyboard_getchar(void)
{
    while (!keyboard_haschar()) {
        __asm__ volatile ("sti; hlt");
    }
    
    unsigned char c = kb_buffer[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % KB_BUFFER_SIZE;
    
    return c;
}

/* Get character from buffer (non-blocking) - for shell/syscalls */
unsigned char keyboard_getchar_nonblocking(void)
{
    if (!keyboard_haschar()) {
        return 0;  // Return 0 if no character available
    }
    
    unsigned char c = kb_buffer[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % KB_BUFFER_SIZE;
    
    return c;
}

/* Keyboard polling service for minit */
void keyboard_service_run(void)
{
    for (;;)
    {
        if (!kb_irq_seen)
            keyboard_poll_once();
        __asm__ volatile ("sti; hlt");
    }
}
