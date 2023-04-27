#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "nec_receive_library/nec_receive.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

#include "nec_receive.pio.h"

#define PIN_LIGHT_PWM 	(0)
#define PIN_IR_LED 		(1)

#define NUM_LIGHT_STEPS			(100) // total number of lightness steps from 0-100%, each button press is 1%
#define LIGHT_LEVEL_MULTIPLIER 	(65536)
#define LIGHT_LEVEL_STEP 		(LIGHT_LEVEL_MULTIPLIER / NUM_LIGHT_STEPS) // value of each the brightness change step

bool isOn = false;
uint16_t lightLevel = 0;
int lightStep = 0; //

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

void leds_set_light_level(uint16_t level) {
	pwm_set_gpio_level(PIN_LIGHT_PWM, level);
}

// Level is number between 0-100, this creates a percentage
void leds_set_light_level_percent(float level) {
	uint16_t newLightLevel = (level / 100.0f) * LIGHT_LEVEL_MULTIPLIER;
	leds_set_light_level(newLightLevel);
	printf("New light level=%u%%(%u)\n", (newLightLevel / (LIGHT_LEVEL_MULTIPLIER / 100)), newLightLevel);
	lightLevel = newLightLevel;

	// set the lightstep so changing the brightness with the up/down buttons
	// doesn't reset the brightness to the last used lightStep value
	lightStep = level;
}

// Change the led brightness one "step" at a time
void leds_change_brightness_step(int mag) {
	lightStep += mag;

	if (lightStep > 100) {
		lightStep = 100;
	} else if (lightStep < 1) {
		lightStep = 1; // don't allow brightness to go "off" just really low
	}

	uint16_t newLightLevel = (lightStep / 100.0f) * LIGHT_LEVEL_MULTIPLIER;
	leds_set_light_level(newLightLevel);
}

int main() {
	stdio_init_all();

	// Setup Relay output
	gpio_init(PIN_LIGHT_PWM);
	gpio_set_dir(PIN_LIGHT_PWM, true);
	gpio_set_function(PIN_LIGHT_PWM, GPIO_FUNC_PWM);

	// Figure out which slice we just connected to the LED pin
    uint slice_num = pwm_gpio_to_slice_num(PIN_LIGHT_PWM);
    pwm_config config = pwm_get_default_config();
    // Set divider, reduces counter clock to sysclock/this value
    pwm_config_set_clkdiv(&config, 4.f);
    // Load the configuration into our PWM slice, and set it running.
    pwm_init(slice_num, &config, false);
	leds_set_light_level_percent(10); // off, but set default "on" to be 10%, so from cold boot the lights will turn on at least a little

	// IR Decode library, from the Pico examples git
	PIO pio = pio0;
	uint rx_sm = nec_rx_init(pio, PIN_IR_LED);

	// ON: 0xFC03EF00
	//OFF: 0xFD02EF00
	uint32_t on_frame  = 0xfc03ef00;
	uint32_t off_frame = 0xfd02ef00;

	// brightness up: ff00ef00
	// brightness down: fe01ef00
	uint32_t brightness_up_frame = 0xff00ef00;
	uint32_t brightness_down_frame = 0xfe01ef00;

	// 100% 0xfb04ef00
	// 90%	0xf708ef00
	// 80%	0xf30cef00
	// 70%	0xef10ef00
	// 60%	0xeb14ef00
	// 50%	0xfa05ef00
	// 40%	0xf609ef00
	// 30%	0xf20def00
	// 20%	0xee11ef00
	// 10%	0xea15ef00
	// I could do something clever but I'll just make a switch case
	uint32_t on_100_frame = 0xfb04ef00;
	uint32_t on_90_frame = 0xf708ef00;
	uint32_t on_80_frame = 0xf30cef00;
	uint32_t on_70_frame = 0xef10ef00;
	uint32_t on_60_frame = 0xeb14ef00;
	uint32_t on_50_frame = 0xfa05ef00;
	uint32_t on_40_frame = 0xf609ef00;
	uint32_t on_30_frame = 0xf20def00;
	uint32_t on_20_frame = 0xee11ef00;
	uint32_t on_10_frame = 0xea15ef00;

	while (true) {
		// display any frames in the receive FIFO
		while (!pio_sm_is_rx_fifo_empty(pio, rx_sm)) {
			uint32_t rx_frame = pio_sm_get(pio, rx_sm);
			if (rx_frame == on_frame) {
				printf("Lights on!\n");
				isOn = true;
				pwm_set_enabled(slice_num, isOn);
			} else if (rx_frame == off_frame) {
				printf("Lights off!\n");
				isOn = false;
				pwm_set_enabled(slice_num, isOn);
			} else if (rx_frame == brightness_up_frame) {
				leds_change_brightness_step(1);
			} else if (rx_frame == brightness_down_frame) {
				leds_change_brightness_step(-1);
			} else if (rx_frame == on_100_frame) {
				leds_set_light_level_percent(100);
			} else if (rx_frame == on_90_frame) {
				leds_set_light_level_percent(90);
			} else if (rx_frame == on_80_frame) {
				leds_set_light_level_percent(80);
			} else if (rx_frame == on_70_frame) {
				leds_set_light_level_percent(70);
			} else if (rx_frame == on_60_frame) {
				leds_set_light_level_percent(60);
			} else if (rx_frame == on_50_frame) {
				leds_set_light_level_percent(50);
			} else if (rx_frame == on_40_frame) {
				leds_set_light_level_percent(40);
			} else if (rx_frame == on_30_frame) {
				leds_set_light_level_percent(30);
			} else if (rx_frame == on_20_frame) {
				leds_set_light_level_percent(20);
			} else if (rx_frame == on_10_frame) {
				leds_set_light_level_percent(10);
			} else {
				// Print unknown command
				printf("%08x\n", rx_frame);
			}
		}

		sleep_ms(500);
	}

	return 0;
}
