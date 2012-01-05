#include <wixel.h>
#include <stdio.h>
#include <sleep.h>

#define SLEEP_TIME  5 // seconds

#define FLASH_TIME 150 // ms

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

void main()
{
    systemInit();
    
    while(1)
    {
        boardService();
        
        flash(1);
        sleepMode1(SLEEP_TIME);   
        
        flash(2);
        sleepMode2(SLEEP_TIME);
    }
}
