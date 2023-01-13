#ifndef __INITUTILS_H
#define __INITUTILS_H

#include "xerxes_rp2040.h"
#include "Memory.h"
#include "ClockUtils.h"

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/uart.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/flash.h"
#include "hardware/rtc.h"


// queue for incoming and outgoing data
queue_t txFifo;
queue_t rxFifo;


void userInitQueue()
{
    queue_init(&txFifo, 1, RX_TX_QUEUE_SIZE);
    queue_init(&rxFifo, 1, RX_TX_QUEUE_SIZE);
}


void uart_interrupt_handler()
{
    gpio_put(USR_LED_PIN, 1);
    if(uart_is_readable(uart0))
    {
        unsigned char rcvd = uart_getc(uart0);
        auto success = queue_try_add(&rxFifo, &rcvd);

        if(!success)
        {
            // set cpu overload flag
            *error |= ERROR_CPU_OVERLOAD;
        }
    }
    gpio_put(USR_LED_PIN, 0);
    irq_clear(UART0_IRQ);
}


void userInitUart(void)
{
    // Initialise UART 0 on 115200baud
    uart_init(uart0, DEFAULT_BAUDRATE);
 
    // Set the GPIO pin mux to the UART - 16 is TX, 17 is RX
    gpio_set_function(RS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RS_RX_PIN, GPIO_FUNC_UART);

    gpio_init(RS_EN_PIN);
    gpio_set_dir(RS_EN_PIN, GPIO_OUT);
    gpio_put(RS_EN_PIN, true);

    uart_set_fifo_enabled(uart0, true);	
    // stdio_set_driver_enabled(&stdio_uart, false);

    irq_set_exclusive_handler(UART0_IRQ, uart_interrupt_handler);
    irq_set_enabled(UART0_IRQ, true);

    // enable uart interrupt for receiving
    uart_set_irq_enables(uart0, true, false);
}


void userInitGpio()
{
    gpio_init(USR_SW_PIN);
	gpio_init(USR_LED_PIN);
    gpio_init(USR_BTN_PIN);
    
    gpio_set_dir(USR_SW_PIN, GPIO_IN);
	gpio_set_dir(USR_LED_PIN, GPIO_OUT);
    gpio_set_dir(USR_BTN_PIN, GPIO_IN);

    gpio_pull_up(USR_SW_PIN);
    gpio_pull_up(USR_BTN_PIN);

    gpio_put(RS_EN_PIN, true);
}


void userLoadDefaultValues()
{

    for(uint i=0; i<REGISTER_SIZE; i++)
    {
        mainRegister[i] = 0;
    }

    *gainPv0    = 1;
    *gainPv1    = 1;    
    *gainPv1    = 1;
    *gainPv1    = 1;

    *desiredCycleTimeUs = DEFAULT_CYCLE_TIME_US; 
    config->all = 0;
    updateFlash();
}


void userInit()
{
    // initialize the clocks
    userInitClocks();

    // initialize the gpios
    userInitGpio();

    // initialize the queues for uart communication
    userInitQueue();

    // initialize the flash memory and load the default values
    userInitFlash();
}

#endif // !__INITUTILS_H