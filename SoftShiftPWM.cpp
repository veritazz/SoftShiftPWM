#include "SoftShiftPWM.h"
#include <digitalWriteFast.h>

static SoftShiftPWM *instance = 0;

#define TIMER_PRESCALER                8
#define ON_BRIGHTNESS                 (0)
#define OFF_BRIGHTNESS                (STEPS_PER_FRAME - 1)
#define BRIGHTNESS_LEVELS             (STEPS_PER_FRAME - 1)

#define TIMER_TICKS \
	(F_CPU / TIMER_PRESCALER / (FRAMES_PER_SECOND * STEPS_PER_FRAME) - 1)

#define SHIFT_BIT(m, v)		\
	digitalWriteFast2(DATA_PIN, 0); \
	digitalWriteFast2(CLK_PIN, 0); \
	if ((v) & (m)) {\
		digitalWriteFast2(DATA_PIN, 1); \
	} \
	digitalWriteFast2(CLK_PIN, 1);

SoftShiftPWM& SoftShiftPWM::getInstance(void)
{
	static SoftShiftPWM instance;
	return instance;
}

SoftShiftPWM::SoftShiftPWM()
{
	uint8_t i;

	pinMode(LATCH_PIN, OUTPUT);
	pinMode(DATA_PIN, OUTPUT);
	pinMode(CLK_PIN, OUTPUT);

	pwm_cnt = 0;
	isr_leds = led_output1;
	isr_ocrs = ocr_setting1;
	usr_leds = led_output2;
	usr_ocrs = ocr_setting2;

	for (i = 0; i < NR_OF_LEDS; i++)
		led_pins[i] = i;

	memset(brightness, OFF_BRIGHTNESS, NR_OF_LEDS);
}

void SoftShiftPWM::begin(void)
{
	uint8_t max;

	instance = this;

	/* calculate initial values */
	max = recalc();

	/*
	 * initialize latch-/clock-pin to low
	 */
	digitalWriteFast2(LATCH_PIN, LOW);
	digitalWriteFast2(CLK_PIN, LOW);

	/*
	 * configure timer 1 (16 bit) to CTC mode
	 */
	bitClear(TCCR1B, WGM13);
	bitSet(TCCR1B, WGM12);
	bitClear(TCCR1A, WGM11);
	bitClear(TCCR1A, WGM10);

	/* prescaler */
#if TIMER_PRESCALER == 1
	bitSet(TCCR1B, CS10);
	bitClear(TCCR1B, CS11);
	bitClear(TCCR1B, CS12);
#elif TIMER_PRESCALER == 8
	bitClear(TCCR1B, CS10);
	bitSet(TCCR1B, CS11);
	bitClear(TCCR1B, CS12);
#endif

	isr_leds = usr_leds;
	isr_ocrs = usr_ocrs;

	active_leds = max;
	set_timer(max);
}

void SoftShiftPWM::setLED(uint8_t led, uint8_t _brightness)
{
	uint8_t i;

	if (led == ALL_LEDS) {
		for (i = 0; i < NR_OF_LEDS; i++)
			brightness[i] = _brightness;
	} else {
		brightness[pin_index(led)] = _brightness;
	}
}

void SoftShiftPWM::setLEDSync(uint8_t led, uint8_t brightness)
{
	setLED(led, brightness);
	sync();
}

void SoftShiftPWM::sync(void)
{
	uint8_t value;
	uint8_t sreg;

	value = recalc();

	sreg = SREG;
	cli();
	while (pwm_cnt < active_leds) {
		SREG = sreg;
		sreg = SREG;
		cli();
	}
	isr_leds = usr_leds;
	isr_ocrs = usr_ocrs;
	active_leds = value;
	SREG = sreg;
	set_timer(value);
	if (usr_leds == led_output1) {
		usr_leds = led_output2;
		usr_ocrs = ocr_setting2;
	} else {
		usr_leds = led_output1;
		usr_ocrs = ocr_setting1;
	}
}

void SoftShiftPWM::set_timer(uint8_t max_leds)
{
	/* disable timer if all LEDs off or on */
	if (max_leds == 0) {
		bitClear(TIMSK1, OCIE1A);
		digitalWriteFast2(LATCH_PIN, 0);
		/* write always on section */
		shift_out_data(&isr_leds[NR_OF_LEDS], NR_OF_LEDS);
		digitalWriteFast2(LATCH_PIN, 1);
		return;
	}

	if (bitRead(TIMSK1, OCIE1A) == 0) {
		OCR1A = isr_ocrs[0];
		digitalWriteFast2(LATCH_PIN, 0);
		/* write always on section */
		shift_out_data(&isr_leds[NR_OF_LEDS], NR_OF_LEDS);
		digitalWriteFast2(LATCH_PIN, 1);
		bitSet(TIMSK1, OCIE1A);
	}
}

uint8_t SoftShiftPWM::recalc(void)
{
	uint8_t i;
	uint8_t *pwm = brightness;
	uint8_t *leds = led_pins;
	uint8_t min_set = 0;
	uint8_t ocrs = 0;
	uint8_t min_ocr;

	sort(pwm, leds, NR_OF_LEDS);

	/* DEBUG */
//	memset(usr_ocrs, 0, (NR_OF_LEDS + 1) * sizeof(usr_ocrs[0]));

	/* time is off time */
	/* disable all leds */
	memset(usr_leds, 0, LED_OUTPUT_BUFFER * (NR_OF_LEDS + 1));

	/* check if all off */
	if (pwm[0] == OFF_BRIGHTNESS) {
		return ocrs;
	}

	/* check if all on */
	if (pwm[NR_OF_LEDS - 1] == ON_BRIGHTNESS) {
		/* enable all leds */
		memset(&usr_leds[NR_OF_LEDS], 0xff, LED_OUTPUT_BUFFER);
		return ocrs;
	}

	/* check if all are the same */
	if (pwm[0] == pwm[NR_OF_LEDS - 1]) {
		/* enable all leds */
		memset(usr_leds, 0xff, LED_OUTPUT_BUFFER);
		usr_ocrs[ocrs] = pwm[0] * TIMER_TICKS;
		ocrs = inc_ocr(usr_leds, ocrs);
		usr_ocrs[ocrs] = (BRIGHTNESS_LEVELS - pwm[0]) * TIMER_TICKS;
		return ocrs;
	}

	for (i = 0; i < NR_OF_LEDS; i++) {
		if (pwm[i] == ON_BRIGHTNESS) {
			/* always on */
			set_led(&usr_leds[ocrs * LED_OUTPUT_BUFFER], leds[i]);
			/* always on section */
			set_led(&usr_leds[NR_OF_LEDS * LED_OUTPUT_BUFFER],
				leds[i]);
			continue;
		}
		if (pwm[i] == OFF_BRIGHTNESS) {
			/* always off from now */
			break;
		}
		if (i == 0 || !min_set) {
			usr_ocrs[ocrs] = pwm[i] * TIMER_TICKS;
			min_set++;
			min_ocr = i;
			/* enable led */
			set_led(&usr_leds[ocrs * LED_OUTPUT_BUFFER], leds[i]);
			ocrs = inc_ocr(usr_leds, ocrs);
			continue;
		}
		if (pwm[i] == pwm[min_ocr]) {
			/* enable led */
			set_led(&usr_leds[(ocrs - 1) * LED_OUTPUT_BUFFER],
				leds[i]);
			inc_ocr(usr_leds, ocrs - 1);
			continue;
		}
		usr_ocrs[ocrs] = (pwm[i] - pwm[min_ocr]) * TIMER_TICKS;
		min_ocr = i;
		/* enable led */
		set_led(&usr_leds[ocrs * LED_OUTPUT_BUFFER], leds[i]);
		ocrs = inc_ocr(usr_leds, ocrs);
	}
	/* for the last period calculate remaining time to stay on */
	i--;
	usr_ocrs[ocrs] = (BRIGHTNESS_LEVELS - pwm[i]) * TIMER_TICKS;

	return ocrs;
}

uint8_t SoftShiftPWM::pin_index(uint8_t pin)
{
	uint8_t i;

	for (i = 0; i < NR_OF_LEDS; i++) {
		if (led_pins[i] == pin)
			break;
	}
	return i;
}

void SoftShiftPWM::set_led(uint8_t *buffer, uint8_t led)
{
	uint8_t byte_i = led / 8;
	uint8_t bit_i = led % 8;

	buffer[byte_i] |= (1 << bit_i);
}

uint8_t SoftShiftPWM::inc_ocr(uint8_t *buffer, uint8_t ocr)
{
	/* do not copy into the always on section */
	if (ocr + 1 == NR_OF_LEDS)
		return NR_OF_LEDS;

	memcpy(&buffer[(ocr + 1) * LED_OUTPUT_BUFFER],
	       &buffer[ocr * LED_OUTPUT_BUFFER],
	       LED_OUTPUT_BUFFER);
	return ocr + 1;
}

uint8_t SoftShiftPWM::sort(uint8_t *a, uint8_t *b, uint8_t size)
{
	uint8_t i;
	uint8_t o;
	uint8_t t;
	uint8_t min_count = 0;

	if (!size)
		return min_count;

	for (i = 0; i < (size - 1); i++) {
		if (a[i] == 0)
			min_count++;
		for (o = 0; o < (size - (i + 1)); o++) {
			if (a[o] > a[o + 1]) {
				t = a[o];
				a[o] = a[o + 1];
				a[o + 1] = t;
				t = b[o];
				b[o] = b[o + 1];
				b[o + 1] = t;
			}
		}
	}
	if (a[i] == 0)
		min_count++;
	return min_count;
}

void SoftShiftPWM::shift_out_data(uint8_t *v, uint8_t bits)
{
	uint8_t byte;

	while (bits) {
		byte = *v++;
		switch (bits) {
		default:
		case 8:
			SHIFT_BIT(0x80, byte);
		case 7:
			SHIFT_BIT(0x40, byte);
		case 6:
			SHIFT_BIT(0x20, byte);
		case 5:
			SHIFT_BIT(0x10, byte);
		case 4:
			SHIFT_BIT(0x08, byte);
		case 3:
			SHIFT_BIT(0x04, byte);
		case 2:
			SHIFT_BIT(0x02, byte);
		case 1:
			SHIFT_BIT(0x01, byte);
		}
		bits = bits > 8? bits - 8: 0;
	}
}

void SoftShiftPWM::timer_isr(void)
{
	uint8_t *leds;
	uint16_t ocr;

	if (pwm_cnt >= active_leds) {
		pwm_cnt = 0;
		/* always on section */
		leds = &isr_leds[LED_OUTPUT_BUFFER * NR_OF_LEDS];
	} else {
		leds = &isr_leds[LED_OUTPUT_BUFFER * pwm_cnt];
		pwm_cnt++;
	}
	ocr = isr_ocrs[pwm_cnt];
	OCR1A = ocr;

	digitalWriteFast2(LATCH_PIN, 0);
	shift_out_data(leds, NR_OF_LEDS);
	digitalWriteFast2(LATCH_PIN, 1);
}

ISR(TIMER1_COMPA_vect) {
	instance->timer_isr();
}
