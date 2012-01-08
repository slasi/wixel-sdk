/** test_radio_sleep app:

Tests suspending/resuming the radio for sleep.
Use this app with the wireless serial app. The wireless serial app
will display a character sent by this app every SLEEP_TIME seconds.
The transmitted characters cycle through the range of printable ascii characters.
If this app receives a character from the wireless_serial
app, it resets its transmit character to the received character.
Sometimes it might not receive the new character until the next time through
handleRadioCom(). If you want to ensure that this doesn't happen then increase
the TIMEOUT constant.
*/

#include <wixel.h>
#include <stdio.h>
#include <sleep.h>
#include <radio_com.h>

#define SLEEP_TIME  5 // seconds

#define FLASH_TIME 150 // ms

#define TIMEOUT 200 // ms

static uint8 ctr = '!';

/* FUNCTIONS ******************************************************************/

void flash(unsigned int times)
{
   unsigned int i;
   for(i = 0; i < times; i++)
   {
      LED_YELLOW(1);
      delayMs(FLASH_TIME);
      LED_YELLOW(0);
      delayMs(FLASH_TIME);
   }
}

void handleRadioCom()
{
	uint32	startTime;
	uint8	bytesReceived;

	startTime = getMs();
	while (!(bytesReceived = radioComRxAvailable()) && (getMs() - startTime) < TIMEOUT);
	while (bytesReceived--)
		ctr = radioComRxReceiveByte();

	if (radioComTxAvailable())
	{
		radioComTxSendByte(ctr++);
		if (ctr == 127)
			ctr = '!';
	}
}

void doSleep(uint16 timeToSleep)
{
    radioMacSleep();
    sleepMode2(timeToSleep);
    radioMacResume();
}

void main()
{
    systemInit();
    sleepInit();
    radioComInit();
    
    while(1)
    {
        boardService();
        radioComTxService();
        
        flash(2);
        handleRadioCom();

        doSleep(SLEEP_TIME);
    }
}
