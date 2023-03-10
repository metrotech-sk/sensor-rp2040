#include <bitset>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "pico/util/queue.h"

#include "Core/Errors.h"
#include "Core/BindWrapper.hpp"
#include "Core/Slave.hpp"
#include "Core/Register.hpp"
#include "Communication/Callbacks.hpp"
#include "Hardware/Board/xerxes_rp2040.h"
#include "Hardware/ClockUtils.hpp"
#include "Hardware/InitUtils.hpp"
#include "Hardware/Sleep.hpp"
#include "Sensors/all.hpp"
#include "Communication/RS485.hpp"


using namespace std;
using namespace Xerxes;


// forward declaration
__SENSOR_CLASS sensor;


Register _reg;  // main register

/// @brief transmit FIFO queue for UART
queue_t txFifo;
/// @brief receive FIFO queue for UART
queue_t rxFifo;

RS485 xn(&txFifo, &rxFifo);     // RS485 interface
Protocol xp(&xn);               // Xerxes protocol implementation
Slave xs(&xp, *_reg.devAddress);   ///< Xerxes slave implementation

volatile bool usrSwitchOn;                // user switch state
volatile bool core1idle = true;  // core1 idle flag
volatile bool useUsb = false;    // use usb uart flag
volatile bool awake = true;

/**
 * @brief Core 1 entry point, runs in background
 */
void core1Entry();  


int main(void)
{
    // enable watchdog for 200ms, pause on debug = true
    watchdog_enable(DEFAULT_WATCHDOG_DELAY, true);
    
    // init system
    userInit();  // 374us

    // blink led for 1 ms - we are alive
    gpio_put(USR_LED_PIN, 1);
    sleep_ms(1);
    gpio_put(USR_LED_PIN, 0);
        
    // clear error register
    _reg.errorClear(0xFFFFFFFF);

    //determine reason for restart:
    if (watchdog_caused_reboot())
    {
        _reg.errorSet(ERROR_MASK_WATCHDOG_TIMEOUT);
    }
    
    // check if user switch is on, if so, use usb uart
    useUsb = gpio_get(USR_SW_PIN);

    
    // if user button is pressed, load default values a.k.a. FACTORY RESET
    if(!gpio_get(USR_BTN_PIN)) userLoadDefaultValues();
    

    watchdog_update();
    sensor = __SENSOR_CLASS(&_reg);

    #ifdef SHIELD_AI
    sensor.init(2, 3);
    #else
    sensor.init();
    #endif // !SHIELD_AI
    watchdog_update();
    
    if(useUsb)
    {
        // init usb uart
        stdio_usb_init();
        userInitUartDisabled();
        
        // wait for usb to be ready
        sleep_hp(2'000'000);
        // print out error register
        cout << "error register: " << bitset<32>(*_reg.error) << endl;
        // cout sampling speed in Hz
        cout << "sampling speed: " << (1000000.0f / (float)(*_reg.desiredCycleTimeUs)) << "Hz" << endl;
        
        // set to free running mode and calculate statistics for usb uart mode so we can see the values
        _reg.config->bits.freeRun = 1;
        _reg.config->bits.calcStat = 1;
    }
    else
    {
        // init uart over RS485
        userInitUart();
    }


    // bind callbacks, ~204us
    xs.bind(MSGID_PING,         unicast(    pingCallback));
    xs.bind(MSGID_WRITE,        unicast(    writeRegCallback));
    xs.bind(MSGID_READ,         unicast(    readRegCallback));
    xs.bind(MSGID_SYNC,         broadcast(  syncCallback));
    xs.bind(MSGID_SLEEP,        broadcast(  sleepCallback));
    xs.bind(MSGID_RESET_SOFT,   broadcast(  softResetCallback));
    xs.bind(MSGID_RESET_HARD,   unicast(    factoryResetCallback));

    // drain uart fifos, just in case there is something in there
    while(!queue_is_empty(&txFifo)) queue_remove_blocking(&txFifo, NULL);
    while(!queue_is_empty(&rxFifo)) queue_remove_blocking(&rxFifo, NULL);

    // start core1
    multicore_launch_core1(core1Entry);

    // main loop, runs forever, handles all communication in this loop
    while(1)
    {    
        // update watchdog
         watchdog_update();

        if(useUsb)
        {
            constexpr uint32_t printFrequencyHz = 10;
            constexpr uint64_t printIntervalUs = 1000000 / printFrequencyHz;

            // cout timestamp and net cycle time in json format
            auto timestamp = time_us_64();
            cout << "{" << endl;
            cout << "\"timestamp\":" << timestamp << "," << endl;
            cout << "\"samplingSpeedHz\":" << (1000000.0f / (float)(*_reg.desiredCycleTimeUs)) << "," << endl;
            cout << "\"netCycleTimeUs\":" << *_reg.netCycleTimeUs << "," << endl;
            cout << "\"errors\":" << (*_reg.error) << "," << endl;
                        
            // cout sensor values in json format
            cout << "\"sensor\":" << sensor.getJson() << endl;
            cout << "}" << endl << endl;

            auto end = time_us_64();
            // calculate remaining sleep time in us - 10us for calculation overhead
            auto remainingSleepTime = printIntervalUs - (end - timestamp) - 10;

            // sleep in high speed mode for 1 second, watchdog friendly
            sleep_hp(remainingSleepTime);
        }
        else
        {
            // running on RS485, sync for incoming messages from master, timeout = 5ms
            xs.sync(5000);
            
            // send char if tx queue is not empty and uart is writable
            if(!queue_is_empty(&txFifo))
            {   
                uint txLen = queue_get_level(&txFifo);
                assert(txLen <= RX_TX_QUEUE_SIZE);

                uint8_t toSend[txLen];

                // drain queue
                for(uint i = 0; i < txLen; i++)
                {
                    queue_remove_blocking(&txFifo, &toSend[i]);
                }

                // write char to bus, this will clear the interrupt
                uart_write_blocking(uart0, toSend, txLen);
            }
        
            if(queue_is_full(&txFifo) || queue_is_full(&rxFifo))
            {
                // rx fifo is full, set the cpu_overload error flag
                _reg.errorSet(ERROR_MASK_UART_OVERLOAD);
            }

            // save power in release mode
            #ifdef NDEBUG
                if(core1idle)
                {
                    // setClocksLP();
                    sleep_us(10);
                    // setClocksHP();
                }
            #endif // NDEBUG
        }
    }
}


void core1Entry()
{
    uint64_t endOfCycle = 0;
    uint64_t cycleDuration = 0;
    int64_t sleepFor = 0;
    
    // let core0 lockout core1
    multicore_lockout_victim_init ();


    // core1 mainloop
    while(true)
    {
        // core is set to free run, start cycle
        auto startOfCycle = time_us_64();

        // turn on led for a short time to signal start of cycle
        gpio_put(USR_LED_PIN, 1);

        if(_reg.config->bits.freeRun)
        {
            sensor.update(); 
        }

        // turn off led
        gpio_put(USR_LED_PIN, 0);

        // calculate how long it took to finish cycle
        endOfCycle = time_us_64();
        cycleDuration = endOfCycle - startOfCycle;

        // calculate net cycle time as moving average
        *_reg.netCycleTimeUs = static_cast<uint32_t>(0.9 * *_reg.netCycleTimeUs) + static_cast<uint32_t>(0.1 * static_cast<uint32_t>(cycleDuration));

        // calculate remaining sleep time
        sleepFor = *_reg.desiredCycleTimeUs - cycleDuration;
        
        // sleep for the remaining time
        if(sleepFor > 0)
        {
            core1idle = true;
            sleep_us(sleepFor);
            core1idle = false;
            _reg.errorClear(ERROR_MASK_SENSOR_OVERLOAD);
        }
        else
        {
            _reg.errorSet(ERROR_MASK_SENSOR_OVERLOAD);
        }
        
    }
    
    core1idle = true;
}
