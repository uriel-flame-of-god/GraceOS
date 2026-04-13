// ============================
// GraceOS PS/2 Mouse Driver
// ============================

#include "mouse.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../video/serial.h"

/* Global mouse state */
static mouse_state_t mouse_state = {0};

/* Mouse event buffer */
static mouse_event_t mouse_buffer[MOUSE_BUFFER_SIZE];
static size_t mouse_read_pos = 0;
static size_t mouse_write_pos = 0;

/* Forward declarations */
static int mouse_wait_read(void);
static int mouse_wait_write(void);
static void mouse_write_cmd(uint8_t cmd);
static uint8_t mouse_read_data(void);
static void mouse_write_to_aux(uint8_t data);
static uint8_t mouse_read_aux(void);
static uint8_t mouse_send_command(uint8_t cmd);
static uint8_t mouse_send_command_with_param(uint8_t cmd, uint8_t param);
static void mouse_detect_type(void);

/* ============================
   PS/2 Controller Helper Functions
   ============================ */

/* Wait for controller to be ready for reading */
static int mouse_wait_read(void)
{
    int timeout = 100000;
    while (timeout--) {
        if (inb(MOUSE_STATUS_PORT) & 1) {
            return 1;
        }
    }
    SERIAL_WARN("Mouse: read timeout");
    return 0;
}

/* Wait for controller to be ready for writing */
static int mouse_wait_write(void)
{
    int timeout = 100000;
    while (timeout--) {
        if (!(inb(MOUSE_STATUS_PORT) & 2)) {
            return 1;
        }
    }
    SERIAL_WARN("Mouse: write timeout");
    return 0;
}

/* Write command to PS/2 controller */
static void mouse_write_cmd(uint8_t cmd)
{
    if (mouse_wait_write()) {
        outb(MOUSE_CMD_PORT, cmd);
    }
}

/* Read data from PS/2 controller */
static uint8_t mouse_read_data(void)
{
    if (mouse_wait_read()) {
        return inb(MOUSE_DATA_PORT);
    }
    return 0;
}

/* Write data to AUX (mouse) device */
static void mouse_write_to_aux(uint8_t data)
{
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0xD4);  /* Write to AUX device */
    mouse_wait_write();
    outb(MOUSE_DATA_PORT, data);
}

/* Read data from AUX device */
static uint8_t mouse_read_aux(void)
{
    if (mouse_wait_read()) {
        uint8_t status = inb(MOUSE_STATUS_PORT);
        if (status & 0x20) {  /* AUX data available */
            return inb(MOUSE_DATA_PORT);
        }
    }
    return 0;
}

/* Send command to mouse and wait for ACK */
static uint8_t mouse_send_command(uint8_t cmd)
{
    uint8_t response;
    int retries = 3;
    
    while (retries--) {
        mouse_write_to_aux(cmd);
        response = mouse_read_data();
        
        if (response == MOUSE_RESP_ACK) {
            return MOUSE_RESP_ACK;
        }
        
        SERIAL_WARN("Mouse: command retry");
    }
    
    SERIAL_ERROR("Mouse: command failed");
    return MOUSE_RESP_ERROR;
}

/* Send command with parameter */
static uint8_t mouse_send_command_with_param(uint8_t cmd, uint8_t param)
{
    uint8_t response;
    
    response = mouse_send_command(cmd);
    if (response != MOUSE_RESP_ACK) {
        return response;
    }
    
    mouse_write_to_aux(param);
    return mouse_read_data();
}

/* ============================
   Mouse Detection & Initialization
   ============================ */

/* Detect mouse type (standard, scroll wheel, 5-button) */
static void mouse_detect_type(void)
{
    uint8_t device_id;
    
    /* Reset to defaults first */
    mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    
    /* Get device ID */
    mouse_send_command(MOUSE_CMD_GET_DEVICE_ID);
    device_id = mouse_read_data();
    
    mouse_state.device_id = device_id;
    
    if (device_id == MOUSE_ID_STANDARD) {
        /* Check for scroll wheel mouse using sequence method */
        mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 200);
        mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 100);
        mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 80);
        
        mouse_send_command(MOUSE_CMD_GET_DEVICE_ID);
        device_id = mouse_read_data();
        
        if (device_id == MOUSE_ID_SCROLL) {
            mouse_state.has_scroll = 1;
            mouse_state.device_id = MOUSE_ID_SCROLL;
            SERIAL_LOG("Mouse: Scroll wheel detected");
            
            /* Check for 5-button mouse */
            mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 200);
            mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 200);
            mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, 80);
            
            mouse_send_command(MOUSE_CMD_GET_DEVICE_ID);
            device_id = mouse_read_data();
            
            if (device_id == MOUSE_ID_5BUTTON) {
                mouse_state.has_5buttons = 1;
                mouse_state.device_id = MOUSE_ID_5BUTTON;
                SERIAL_LOG("Mouse: 5-button mouse detected");
            }
        } else {
            SERIAL_LOG("Mouse: Standard PS/2 mouse detected");
        }
    } else if (device_id == MOUSE_ID_SCROLL) {
        mouse_state.has_scroll = 1;
        SERIAL_LOG("Mouse: Scroll wheel mouse detected");
    } else if (device_id == MOUSE_ID_5BUTTON) {
        mouse_state.has_scroll = 1;
        mouse_state.has_5buttons = 1;
        SERIAL_LOG("Mouse: 5-button mouse detected");
    }
}

/* Initialize PS/2 mouse */
void mouse_init(void)
{
    SERIAL_LOG("Mouse: Initializing PS/2 mouse...");
    
    /* Reset state */
    mouse_state.present = 0;
    mouse_state.packet_phase = 0;
    mouse_state.device_id = MOUSE_ID_STANDARD;
    mouse_state.has_scroll = 0;
    mouse_state.has_5buttons = 0;
    mouse_read_pos = 0;
    mouse_write_pos = 0;
    
    /* Disable devices during initialization */
    mouse_write_cmd(PS2_CMD_DISABLE_PORT1);
    mouse_write_cmd(PS2_CMD_DISABLE_PORT2);
    
    /* Flush output buffer */
    while (inb(MOUSE_STATUS_PORT) & 1) {
        inb(MOUSE_DATA_PORT);
    }
    
    /* Enable second PS/2 port (mouse) */
    mouse_write_cmd(PS2_CMD_ENABLE_PORT2);
    
    /* Reset mouse */
    SERIAL_LOG("Mouse: Resetting...");
    uint8_t response = mouse_send_command(MOUSE_CMD_SET_RESET);
    
    if (response == MOUSE_RESP_ACK) {
        /* Read self-test result */
        uint8_t self_test = mouse_read_data();
        if (self_test == MOUSE_RESP_SELF_TEST_PASS) {
            SERIAL_LOG("Mouse: Self-test passed");
        } else {
            SERIAL_WARN("Mouse: Self-test failed");
        }
        
        /* Read device ID */
        mouse_state.device_id = mouse_read_data();
    } else {
        SERIAL_ERROR("Mouse: No response to reset command");
        mouse_write_cmd(PS2_CMD_ENABLE_PORT1);
        return;
    }
    
    /* Set default settings */
    mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    
    /* Configure mouse */
    mouse_set_sample_rate(MOUSE_SAMPLE_RATE);
    mouse_set_resolution(MOUSE_RESOLUTION);
    
    /* Detect extended features */
    mouse_detect_type();
    
    /* Enable data reporting */
    response = mouse_send_command(MOUSE_CMD_ENABLE_REPORT);
    if (response == MOUSE_RESP_ACK) {
        mouse_state.present = 1;
        SERIAL_LOG("Mouse: Initialized successfully");
    } else {
        SERIAL_ERROR("Mouse: Failed to enable reporting");
    }
    
    /* Re-enable keyboard */
    mouse_write_cmd(PS2_CMD_ENABLE_PORT1);
}

/* ============================
   Mouse Configuration
   ============================ */

/* Set mouse sample rate */
void mouse_set_sample_rate(uint8_t rate)
{
    if (mouse_send_command_with_param(MOUSE_CMD_SET_SAMPLE_RATE, rate) == MOUSE_RESP_ACK) {
        mouse_state.sample_rate = rate;
    }
}

/* Set mouse resolution */
void mouse_set_resolution(uint8_t resolution)
{
    if (resolution > 3) resolution = 3;
    if (mouse_send_command_with_param(MOUSE_CMD_SET_RESOLUTION, resolution) == MOUSE_RESP_ACK) {
        mouse_state.resolution = resolution;
    }
}

/* Enable mouse */
void mouse_enable(void)
{
    if (mouse_state.present) {
        mouse_send_command(MOUSE_CMD_ENABLE_REPORT);
    }
}

/* Disable mouse */
void mouse_disable(void)
{
    if (mouse_state.present) {
        mouse_send_command(MOUSE_CMD_DISABLE_REPORT);
    }
}

/* ============================
   Event Buffer Management
   ============================ */

/* Add mouse event to buffer */
static void mouse_buffer_add(mouse_event_t* event)
{
    size_t next_pos = (mouse_write_pos + 1) % MOUSE_BUFFER_SIZE;
    
    if (next_pos == mouse_read_pos) {
        SERIAL_WARN("Mouse: Event buffer full");
        return;
    }
    
    mouse_buffer[mouse_write_pos] = *event;
    mouse_write_pos = next_pos;
}

/* Check if mouse event is available */
int mouse_has_event(void)
{
    return mouse_read_pos != mouse_write_pos;
}

/* Get mouse event from buffer (non-blocking) */
void mouse_get_event(mouse_event_t* event)
{
    if (event && mouse_has_event()) {
        *event = mouse_buffer[mouse_read_pos];
        mouse_read_pos = (mouse_read_pos + 1) % MOUSE_BUFFER_SIZE;
    }
}

/* Check if mouse is present */
int mouse_present(void)
{
    return mouse_state.present;
}

/* ============================
   Mouse Packet Processing
   ============================ */

/* Process complete 3-byte mouse packet */
static void mouse_process_packet(uint8_t packet[3])
{
    mouse_event_t event;
    int16_t x_movement, y_movement;
    
    /* Extract button states */
    event.buttons = packet[0] & 0x07;  /* First 3 bits: Left, Right, Middle */
    event.scroll_delta = 0;
    event.device_id = mouse_state.device_id;
    
    /* Calculate X movement with sign */
    x_movement = packet[1];
    if (packet[0] & MOUSE_X_SIGN) {
        x_movement -= 256;
    }
    event.x_delta = (int8_t)x_movement;
    
    /* Calculate Y movement with sign (Y is inverted in PS/2) */
    y_movement = packet[2];
    if (packet[0] & MOUSE_Y_SIGN) {
        y_movement -= 256;
    }
    event.y_delta = -(int8_t)y_movement;  /* Invert Y for natural movement */
    
    /* Check for overflow */
    if (packet[0] & MOUSE_X_OVERFLOW) {
        event.x_delta = 0;  /* Ignore overflowed movement */
    }
    if (packet[0] & MOUSE_Y_OVERFLOW) {
        event.y_delta = 0;
    }
    
    /* Handle scroll wheel for IntelliMouse */
    if (mouse_state.has_scroll && mouse_state.device_id == MOUSE_ID_SCROLL) {
        /* For scroll wheel mice, we need 4-byte packets */
        /* This will be handled in extended packet processing */
    }
    
    /* Store last event */
    mouse_state.last_event = event;
    
    /* Add to buffer */
    mouse_buffer_add(&event);
    
    /* Log movement for debugging (can be commented out) */
    /*
    serial_write("Mouse: X=");
    serial_int(event.x_delta);
    serial_write(" Y=");
    serial_int(event.y_delta);
    serial_write(" Buttons=");
    serial_hex(event.buttons);
    serial_write("\n");
    */
}

/* Process extended 4-byte packet (for scroll wheel) */
static void mouse_process_extended_packet(uint8_t packet[4])
{
    mouse_event_t event;
    int16_t x_movement, y_movement;
    
    /* Standard 3-byte part */
    event.buttons = packet[0] & 0x07;
    event.device_id = mouse_state.device_id;
    
    /* Calculate X movement */
    x_movement = packet[1];
    if (packet[0] & MOUSE_X_SIGN) {
        x_movement -= 256;
    }
    event.x_delta = (int8_t)x_movement;
    
    /* Calculate Y movement (inverted) */
    y_movement = packet[2];
    if (packet[0] & MOUSE_Y_SIGN) {
        y_movement -= 256;
    }
    event.y_delta = -(int8_t)y_movement;
    
    /* Scroll wheel delta (4th byte) */
    event.scroll_delta = (int8_t)packet[3];
    
    /* Check for overflow */
    if (packet[0] & MOUSE_X_OVERFLOW) {
        event.x_delta = 0;
    }
    if (packet[0] & MOUSE_Y_OVERFLOW) {
        event.y_delta = 0;
    }
    
    /* Store last event */
    mouse_state.last_event = event;
    
    /* Add to buffer */
    mouse_buffer_add(&event);
}

/* ============================
   Mouse Interrupt Handler
   ============================ */

/* Mouse interrupt handler */
void mouse_handler(void)
{
    uint8_t status;
    uint8_t data;
    
    /* Read status to verify this is a mouse interrupt */
    status = inb(MOUSE_STATUS_PORT);
    
    /* Check if data is available and it's from mouse */
    if (!(status & 0x20)) {  /* Not mouse data */
        return;
    }
    
    /* Read mouse data */
    data = inb(MOUSE_DATA_PORT);
    
    /* Packet assembly */
    mouse_state.packet[mouse_state.packet_phase] = data;
    mouse_state.packet_phase++;
    
    /* Check packet size based on mouse type */
    uint8_t expected_packet_size = MOUSE_PACKET_SIZE;
    if (mouse_state.has_scroll && mouse_state.device_id == MOUSE_ID_SCROLL) {
        expected_packet_size = 4;  /* 4-byte packets for scroll wheel */
    }
    
    /* Process complete packet */
    if (mouse_state.packet_phase >= expected_packet_size) {
        mouse_state.packet_phase = 0;
        
        if (expected_packet_size == 4) {
            mouse_process_extended_packet(mouse_state.packet);
        } else {
            mouse_process_packet(mouse_state.packet);
        }
    }
}

/* ============================
   Utility Functions
   ============================ */

/* Wait for mouse event (blocking) - useful for testing */
void mouse_wait_event(mouse_event_t* event)
{
    if (!event) return;
    
    while (!mouse_has_event()) {
        __asm__ volatile ("hlt");
    }
    
    mouse_get_event(event);
}