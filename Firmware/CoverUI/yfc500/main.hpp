/**
 * @file main.hpp
 * @author Apehaenger (joerg@ebeling.ws)
 * @brief YardForce Classic 500 CoverUI main header for OpenMower https://github.com/ClemensElflein/OpenMower
 * @version 0.2
 * @date 2023-04-30
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef __YFC500_MAIN_H
#define __YFC500_MAIN_H

#include <Arduino.h> // Stock CoverUI is build now via Arduino framework (instead of HAL), which is ATM the only framework with STM32F030R8 and GD32F330R8 support
#include "hwtimer.hpp"
#include "LEDcontrol.h"
#include "Buttons.h"
#include "Rain.hpp"

#define FIRMWARE_VERSION 200 // FIXME: Should go into a common header

// STM32/GD32 are single cores without threads.
// Send mutex calls of main.cpp to nirvana. Dangerous? // FIXME: Does Arduino has/need mutexes so that we can honor mutex calls (even if only one core)?
#define auto_init_mutex(name)
#define mutex_enter_blocking(ptr)
#define mutex_exit(ptr)

extern void core1();
extern void getDataFromBuffer();

uint8_t bit_getbutton(uint32_t press_timeout, bool &still_pressed);
void timer_slow_callback_wrapper();
void timer_fast_callback_wrapper();
void timer_quick_callback_wrapper();

// OM names. Could also use those, but I prefer logic names instead of physical ones
#define uart1 &Serial_LL

// Some dummy Pico-SDK definitions. Not used but by this we'll NOT pollution original code to much with #ifdefs
#define pio0 NULL
#define pio1 NULL
typedef bool *PIO;
#define buzzer_SM_CYCLE 10800

// YFC500 implementation specific
LEDcontrol LedControl;      // Main LED controller object
Buttons Btns;               // Main Buttons object
Rain rain;
HardwareTimer *Timer_slow;  // Used for blink-slow LEDs and magic buttons
HardwareTimer *Timer_fast;  // Used for blink-fast LEDs
HardwareTimer *Timer_quick; // Button debouncer and LED sequences

#ifdef MCU_STM32
HardwareSerial Serial_LL(PA3, PA2); // Serial connection to LowLevel MCU, J6/JP2 Pin 1+3
#else                               // MCU_GD32
HardwareSerial Serial_LL((uint8_t)PA3, (uint8_t)PA2, 1); // Serial connection to LowLevel MCU, J6/JP2 Pin 1+3
#endif

#define PIN_HALL_STOP_WHITE PC5
#define PIN_HALL_WHEEL_RED PB7
#define PIN_HALL_STOP_YELLOW PB6
#define PIN_HALL_WHEEL_BLUE PB8

void setup()
{
    LedControl.setup();
    Btns.setup();
    rain.setup();

    // We've hardware timer on mass, let's use them.
    Timer_slow = hwtimer(TIM_SLOW, 2, timer_slow_callback_wrapper);      //   2Hz (500ms) timer, used for LED-blink-slow and magic buttons
    Timer_fast = hwtimer(TIM_FAST, 10, timer_fast_callback_wrapper);     //  10Hz (100ms) timer, used for LED-blink-fast
    Timer_quick = hwtimer(TIM_QUICK, 200, timer_quick_callback_wrapper); // 200Hz   (5ms) timer, used for Buttons debouncer and LED- sequences

    Serial_LL.begin(115200);

    LedControl.set(LED_NUM_REAR, LED_state::LED_blink_slow); // We're alive -> blink // FIXME: Should become a simple delay or similar as a timer might walk on even if main crashed

    delay(100); // Some required stupid delay, dunno why :-/

    // "Hi there" and jammed button mounting detection
    bool tmp;
    do
    {
        // LED blink to say it's alive
        // (this processing delay is also required to get the debouncer filled with a consistent state (NUM_BUTTON_STATES * 5ms)
        LedControl.sequence_start(&LEDcontrol::sequence_animate_handler);

    } while (bit_getbutton(500, tmp));
    delay((NUM_LEDS * 15 * 2) + 500); // Anim get played async + 1/2 sec. extra delay

    // Dev test LEDs
    LedControl.set(LED_NUM_LIFTED, LED_state::LED_on);
    LedControl.set(LED_NUM_WIRE, LED_state::LED_blink_slow);
    LedControl.set(LED_NUM_BAT, LED_state::LED_blink_fast);

    pinMode(PIN_HALL_STOP_WHITE, INPUT);
}

uint32_t next_emergency = millis();
const uint16_t interval_emergency = 200;

void loop() // This loop() doesn't loop!
{
    if (millis() >= next_emergency)
    {
        /*if (digitalRead(PIN_HALL_STOP_WHITE))
            LedControl.set(LED_NUM_MON, LED_state::LED_on);
        else
            LedControl.set(LED_NUM_MON, LED_state::LED_off);

        if (digitalRead(PIN_HALL_WHEEL_RED))
            LedControl.set(LED_NUM_MON - 1, LED_state::LED_on);
        else
            LedControl.set(LED_NUM_MON - 1, LED_state::LED_off);

        if (digitalRead(PIN_HALL_STOP_YELLOW))
            LedControl.set(LED_NUM_MON - 2, LED_state::LED_on);
        else
            LedControl.set(LED_NUM_MON - 2, LED_state::LED_off);

        if (digitalRead(PIN_HALL_WHEEL_BLUE))
            LedControl.set(LED_NUM_MON - 3, LED_state::LED_on);
        else
            LedControl.set(LED_NUM_MON - 3, LED_state::LED_off);*/

        next_emergency += interval_emergency;
    }

    // Drop off into infinite core1() at main.cpp, for button processing (waste (one more?) stack entry!)
    // core1();
}

/**
 * @brief Check if one of the "magic buttons" got pressed and do his function.
 * At the moment the following magic buttons exists:
 * OK + Clock = Display FW version
 * OK + Sun   = LED animation
 */
void magic_buttons()
{
    if (!Btns.is_pressed(BTN_NUM_OK))
        return;

    if (Btns.is_pressed(BTN_NUM_SUN))
        LedControl.sequence_start(&LEDcontrol::sequence_animate_handler);
    else if (Btns.is_pressed(BTN_NUM_CLK))
        LedControl.show_num(FIRMWARE_VERSION);
    return;
}

/**
 * @brief Stupid timer callback wrapper to work-around callback_function_t and timerCallback_t framework differences.
 *   Also, framework-arduinogd32 implementation doesn't support callback arguments nor std::bind and thus no ptr to member functionality!
 */
void timer_slow_callback_wrapper()
{
    LedControl.blink_timer_elapsed(LED_state::LED_blink_slow);
    magic_buttons();
    rain.process();
}

void timer_fast_callback_wrapper()
{
    LedControl.blink_timer_elapsed(LED_state::LED_blink_fast);
}

void timer_quick_callback_wrapper()
{
    getDataFromBuffer();
    Btns.process_states();
    LedControl.process_sequence();
}

/****************************************************************
 * Some dump OM wrapper for not polluting original code to much *
 ****************************************************************/

bool uart_is_readable(HardwareSerial Serial)
{
    return Serial.available();
}

void uart_putc(HardwareSerial *Serial, uint8_t c)
{
    Serial->write(c);
}

void Force_LED_off(uint8_t led_num, bool force)
{
    LedControl.force_off(led_num, force); // This only effect blink states
}

void Blink_LED(PIO dummy, int dummy2, int led_num)
{
    LedControl.identify(led_num);
}

void buzzer_program_put_words(PIO pio, uint sm, uint32_t repeat, uint32_t duration, uint32_t gap)
{
    // YFC500 doesn't has (not yet?) a buzzer on CoverUI
}

uint8_t bit_getbutton(uint32_t press_timeout, bool &still_pressed)
{
    still_pressed = false;

    // As it's not clear ATM how the implementation will become, let's scan the buttons in the same order as original OM FW does
    const uint8_t scan_order[] = {0, 1, 2, 3, 4, 5, 6, 13, 7, 8, 9, 10, 11, 12}; // Attention: Ours = OM's -1. See Buttons.h: FYC500_Button_Def for index number
    for (uint8_t i : scan_order)
    {
        uint32_t start = millis(); // start press_timeout measurement
        if (Btns.is_pressed(i))
        {
            // wait for button released
            while (Btns.is_pressed(i) && (press_timeout == 0 || (millis() - start) < press_timeout))
                ;
            if (Btns.is_pressed(i))
                still_pressed = true;
            else
                still_pressed = false;
            return (i + 1); // OM's button numbering is n > 0
        }
    }
    return 0;
}

#endif /* __YFC500_MAIN_H */