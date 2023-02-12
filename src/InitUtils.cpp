#include "InitUtils.hpp"


#include "xerxes_rp2040.h"
#include "ClockUtils.hpp"
#include "Errors.h"
#include "UserFlash.hpp"
#include "Definitions.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/flash.h"
#include "hardware/rtc.h"
#include "pico/util/queue.h"


extern volatile uint8_t mainRegister[REGISTER_SIZE];
extern queue_t txFifo, rxFifo;


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
            uint64_t* error      = (uint64_t *)(mainRegister + ERROR_OFFSET);   // Error register, holds error codes
            *error |= ERROR_MASK_CPU_OVERLOAD;
        }
    }

    gpio_put(USR_LED_PIN, 0);
    irq_clear(UART0_IRQ);
}


void userInitUart()
{
    // Initialise UART 0 on 115200baud
    uart_init(uart0, DEFAULT_BAUDRATE);
 
    // Set the GPIO pin mux to the UART - 16 is TX, 17 is RX
    gpio_set_function(RS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RS_RX_PIN, GPIO_FUNC_UART);

    // initialise RS485 enable pin and set it to high, this will enable the transceiver
    gpio_init(RS_EN_PIN);
    gpio_set_dir(RS_EN_PIN, GPIO_OUT);
    gpio_put(RS_EN_PIN, true);

    // enable fifo for uart, each FIFO is 32 levels deep
    uart_set_fifo_enabled(uart0, true);	

    // disable stdio uart
    // stdio_set_driver_enabled(&stdio_uart, false);

    // set uart interrupt handler, must be done before enabling uart interrupt
    irq_set_exclusive_handler(UART0_IRQ, uart_interrupt_handler);
    // enable uart interrupt
    irq_set_enabled(UART0_IRQ, true);

    // enable uart interrupt for receiving and transmitting
    uart_set_irq_enables(uart0, true, false);
}


void userInitGpio()
{
    // initialize the user led and button pins
    gpio_init(USR_SW_PIN);
	gpio_init(USR_LED_PIN);
    gpio_init(USR_BTN_PIN);

    gpio_set_drive_strength(USR_LED_PIN, GPIO_DRIVE_STRENGTH_2MA);
    
    gpio_set_dir(USR_SW_PIN, GPIO_IN);
	gpio_set_dir(USR_LED_PIN, GPIO_OUT);
    gpio_set_dir(USR_BTN_PIN, GPIO_IN);

    gpio_pull_up(USR_SW_PIN);
    gpio_pull_up(USR_BTN_PIN);
}


void userLoadDefaultValues()
{

    for(uint i=0; i<REGISTER_SIZE; i++)
    {
        mainRegister[i] = 0;
    }

    float* gainPv0       = (float *)(mainRegister + GAIN_PV0_OFFSET);
    float* gainPv1       = (float *)(mainRegister + GAIN_PV1_OFFSET);
    float* gainPv2       = (float *)(mainRegister + GAIN_PV2_OFFSET);
    float* gainPv3       = (float *)(mainRegister + GAIN_PV3_OFFSET);

    uint32_t *desiredCycleTimeUs     = (uint32_t *)(mainRegister + OFFSET_DESIRED_CYCLE_TIME);  ///< Desired cycle time of sensor loop in microseconds

    ConfigBitsUnion *config          = (ConfigBitsUnion *)(mainRegister + OFFSET_CONFIG_BITS);  ///< Config bits of the device (1 byte)

    *gainPv0    = 1;
    *gainPv1    = 1;    
    *gainPv1    = 1;
    *gainPv1    = 1;

    *desiredCycleTimeUs = DEFAULT_CYCLE_TIME_US; 
    config->all = 0;
    updateFlash((uint8_t *)mainRegister);
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
    if(!userInitFlash((uint8_t *)mainRegister))
    {
        userLoadDefaultValues();
    }
}