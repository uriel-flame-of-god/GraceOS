#ifndef GRACEOS_MOUSE_H
#define GRACEOS_MOUSE_H

#include "../../lib/libc/int.h"

/* ============================
   PS/2 Mouse Driver
   ============================ */

/* Mouse I/O Ports (uses same ports as keyboard) */
#define MOUSE_DATA_PORT    0x60
#define MOUSE_STATUS_PORT  0x64
#define MOUSE_CMD_PORT     0x64

/* PS/2 Controller Commands */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_TEST_CONTROLLER 0xAA
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_ENABLE_PORT1    0xAE

/* Mouse Commands */
#define MOUSE_CMD_SET_RESET         0xFF
#define MOUSE_CMD_RESEND           0xFE
#define MOUSE_CMD_SET_DEFAULTS     0xF6
#define MOUSE_CMD_DISABLE_REPORT   0xF5
#define MOUSE_CMD_ENABLE_REPORT    0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE  0xF3
#define MOUSE_CMD_GET_DEVICE_ID    0xF2
#define MOUSE_CMD_SET_REMOTE_MODE  0xF0
#define MOUSE_CMD_SET_WRAP_MODE    0xEE
#define MOUSE_CMD_RESET_WRAP_MODE  0xEC
#define MOUSE_CMD_READ_DATA        0xEB
#define MOUSE_CMD_SET_STREAM_MODE  0xEA
#define MOUSE_CMD_STATUS_REQUEST   0xE9
#define MOUSE_CMD_SET_RESOLUTION   0xE8

/* Mouse Response Codes */
#define MOUSE_RESP_ACK             0xFA
#define MOUSE_RESP_NACK            0xFE
#define MOUSE_RESP_ERROR           0xFC
#define MOUSE_RESP_SELF_TEST_PASS  0xAA
#define MOUSE_RESP_SELF_TEST_FAIL  0xFC

/* Mouse Packet Structure (3-byte standard) */
typedef struct {
    uint8_t flags;      /* Button flags and sign bits */
    uint8_t x_movement; /* X movement delta */
    uint8_t y_movement; /* Y movement delta */
} mouse_packet_t;

/* Mouse Button Flags */
#define MOUSE_BTN_LEFT      0x01
#define MOUSE_BTN_RIGHT     0x02
#define MOUSE_BTN_MIDDLE    0x04
#define MOUSE_BTN_4         0x08  /* Often used for scroll click */
#define MOUSE_BTN_5         0x10  /* Often used for extra button */

#define MOUSE_X_SIGN        0x10  /* X movement negative */
#define MOUSE_Y_SIGN        0x20  /* Y movement negative */
#define MOUSE_X_OVERFLOW    0x40  /* X overflow */
#define MOUSE_Y_OVERFLOW    0x80  /* Y overflow */

/* Mouse Configuration */
#define MOUSE_SAMPLE_RATE   100   /* Default sample rate (Hz) */
#define MOUSE_RESOLUTION    3     /* 4 counts/mm (0=1,1=2,2=4,3=8) */
#define MOUSE_SCALING       1     /* 1:1 scaling */

/* Extended Mouse Support (Scroll wheel) */
#define MOUSE_ID_STANDARD   0x00
#define MOUSE_ID_SCROLL     0x03
#define MOUSE_ID_5BUTTON    0x04

/* Mouse buffer */
#define MOUSE_BUFFER_SIZE   64
#define MOUSE_PACKET_SIZE   3

/* Mouse event structure */
typedef struct {
    uint8_t buttons;      /* Button state */
    int8_t  x_delta;      /* X movement delta */
    int8_t  y_delta;      /* Y movement delta */
    int8_t  scroll_delta; /* Scroll wheel delta (if available) */
    uint8_t device_id;    /* Mouse device ID */
} mouse_event_t;

/* Mouse state */
typedef struct {
    uint8_t present;           /* Mouse is present and initialized */
    uint8_t device_id;         /* Device ID (0=std, 3=scroll, 4=5btn) */
    uint8_t has_scroll;        /* Has scroll wheel */
    uint8_t has_5buttons;      /* Has 5 buttons */
    uint8_t sample_rate;       /* Current sample rate */
    uint8_t resolution;        /* Current resolution */
    uint8_t scaling;           /* Current scaling */
    uint8_t packet_phase;      /* Packet assembly phase (0-2) */
    uint8_t packet[3];         /* Current packet being assembled */
    mouse_event_t last_event;  /* Last processed event */
} mouse_state_t;

/* API Functions */
void mouse_init(void);
void mouse_handler(void);
int mouse_present(void);
void mouse_get_event(mouse_event_t* event);
int mouse_has_event(void);
void mouse_set_sample_rate(uint8_t rate);
void mouse_set_resolution(uint8_t resolution);
void mouse_enable(void);
void mouse_disable(void);

/* Convenience inline functions */
static inline uint8_t mouse_btn_left(uint8_t buttons) {
    return buttons & MOUSE_BTN_LEFT;
}

static inline uint8_t mouse_btn_right(uint8_t buttons) {
    return buttons & MOUSE_BTN_RIGHT;
}

static inline uint8_t mouse_btn_middle(uint8_t buttons) {
    return buttons & MOUSE_BTN_MIDDLE;
}

#endif /* GRACEOS_MOUSE_H */