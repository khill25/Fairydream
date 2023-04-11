#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "nec_receive_library/nec_receive.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"    // for clock_get_hz()

// #include "nec_receive.h"

// import the assembled PIO state machine program
#include "nec_receive.pio.h"

// Claim an unused state machine on the specified PIO and configure it
// to receive NEC IR frames on the given GPIO pin.
//
// Returns: the state machine number on success, otherwise -1
int nec_rx_init(PIO pio, uint pin_num) {

    // disable pull-up and pull-down on gpio pin
    gpio_disable_pulls(pin_num);

    // install the program in the PIO shared instruction space
    uint offset;
    if (pio_can_add_program(pio, &nec_receive_program)) {
        offset = pio_add_program(pio, &nec_receive_program);
    } else {
        return -1;      // the program could not be added
    }

    // claim an unused state machine on this PIO
    int sm = pio_claim_unused_sm(pio, true);
    if (sm == -1) {
        return -1;      // we were unable to claim a state machine
    }

    // configure and enable the state machine
    nec_receive_program_init(pio, sm, offset, pin_num);

    return sm;
}


// Validate a 32-bit frame and store the address and data at the locations
// provided.
//
// Returns: `true` if the frame was valid, otherwise `false`
bool nec_decode_frame(uint32_t frame, uint8_t *p_address, uint8_t *p_data) {

    // access the frame data as four 8-bit fields
    //
    union {
        uint32_t raw;
        struct {
            uint8_t address;
            uint8_t inverted_address;
            uint8_t data;
            uint8_t inverted_data;
        };
    } f;

    f.raw = frame;

    // a valid (non-extended) 'NEC' frame should contain 8 bit
    // address, inverted address, data and inverted data
    if (f.address != (f.inverted_address ^ 0xff) ||
        f.data != (f.inverted_data ^ 0xff)) {
        return false;
    }

    // store the validated address and data
    *p_address = f.address;
    *p_data = f.data;

    return true;
}


int main() {
    stdio_init_all();

    // Setup Relay output
    gpio_init(6);
    gpio_set_dir(6, true);

    // IR Decode library, from the Pico examples git
    PIO pio = pio0;
    uint rx_sm = nec_rx_init(pio, 8);

    // ON: fc03ef00
    //OFF: fd02ef00
    uint32_t on_frame  = 0xfc03ef00;
    uint32_t off_frame = 0xfd02ef00;
    
    while (true) {
        // display any frames in the receive FIFO
        while (!pio_sm_is_rx_fifo_empty(pio, rx_sm)) {
            uint32_t rx_frame = pio_sm_get(pio, rx_sm);
            if (rx_frame == on_frame) {
                printf("Lights on!\n");
                gpio_put(6, 1); // turn relay on
            } else if (rx_frame == off_frame) {
                printf("Lights off!\n");
                gpio_put(6, 0); // turn relay off
            }
        }

        sleep_ms(1000);
    }

    return 0;
}
