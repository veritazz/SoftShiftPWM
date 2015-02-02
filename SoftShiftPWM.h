#ifndef SOFTSHIFTPWM_H
#define SOFTSHIFTPWM_H

#include <Arduino.h>

/* pin of the latch signal of the shift register */
#define LATCH_PIN                         7
/* pin of the clock signal of the shift register */
#define CLK_PIN                           8
/* pin of the data signal of the shift register */
#define DATA_PIN                          9

/*
 * number of ticks/interrupts per frame
 */
#define STEPS_PER_FRAME                   256
/*
 * number of led updates per second (framerate in Hz)
 */
#define FRAMES_PER_SECOND                 60
/*
 * number of LEDs connected to the shift registers
 */
#define NR_OF_LEDS                        8

/*
 * Buffer size to hold all pin values for all leds to update the
 * entire shift register chain. This value is calculated and must
 * no be altered.
 */
#define LED_OUTPUT_BUFFER                 ((NR_OF_LEDS + (NR_OF_LEDS / 2)) / 8)

class SoftShiftPWM {
public:
	static SoftShiftPWM& getInstance(void);
	void begin(void);
	void setLED(uint8_t led, uint8_t brightness);
	void setLEDSync(uint8_t led, uint8_t brightness);
	void sync(void);
	void timer_isr(void);

	static const uint8_t ALL_LEDS = 255;
private:
	SoftShiftPWM();
	void set_timer(uint8_t max_leds);
	uint8_t recalc(void);
	uint8_t pin_index(uint8_t pin);
	void set_led(uint8_t *buffer, uint8_t led);
	uint8_t inc_ocr(uint8_t *buffer, uint8_t ocr);
	uint8_t sort(uint8_t *a, uint8_t *b, uint8_t size);
	void shift_out_data(uint8_t *v, uint8_t bits);

	uint8_t active_leds;
	uint8_t brightness[NR_OF_LEDS];
	uint8_t led_pins[NR_OF_LEDS];
	uint8_t led_output1[LED_OUTPUT_BUFFER * (NR_OF_LEDS + 1)];
	uint8_t led_output2[LED_OUTPUT_BUFFER * (NR_OF_LEDS + 1)];
	uint16_t ocr_setting1[NR_OF_LEDS + 1];
	uint16_t ocr_setting2[NR_OF_LEDS + 1];
	volatile uint8_t pwm_cnt;
	uint8_t *isr_leds;
	uint16_t *isr_ocrs;
	uint8_t *usr_leds;
	uint16_t *usr_ocrs;
};

#endif
