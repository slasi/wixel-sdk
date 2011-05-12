// sleep.c: Basic functions to put the board into low power modes (sleep)
// and switch oscillators. See the datasheet or design note DN106 for more
// information about the different power modes and their impact.
// This code is based primarily on the samples provided in the previously listed
// design note by Torgeir Sundet

#include <sleep.h>

// Initialization of source buffers and DMA descriptor for the DMA transfer
unsigned char __xdata PM2_BUF[7] = {0x06,0x06,0x06,0x06,0x06,0x06,0x04};
unsigned char __xdata PM3_BUF[7] = {0x07,0x07,0x07,0x07,0x07,0x07,0x04};
unsigned char __xdata dmaDesc[8] = {0x00,0x00,0xDF,0xBE,0x00,0x07,0x20,0x42};

void sleepInit(void)
{
   WORCTRL |= 0x03; // Set Sleep Timer to the lowest resolution (1 second)   
   WORIRQ  |= (1<<4); // Enable Event0 interrupt  
}

ISR(ST, 1)
{
   // Clear IRCON.STIF (Sleep Timer CPU interrupt flag)
   IRCON &= 0x7F;
   // Clear WORIRQ.EVENT0_FLAG (Sleep Timer peripheral interrupt flag)
   // This is required for the CC111xFx/CC251xFx only!
   WORIRQ &= 0xFE;
   
   SLEEP &= 0xFC; // Not required when resuming from PM0 
}

void switchToRCOSC(void)
{
   // Power up [HS RCOSC] (SLEEP.OSC_PD = 0)
   SLEEP &= ~0x04;
   // Wait until [HS RCOSC] is stable (SLEEP.HFRC_STB = 1)
   while ( ! (SLEEP & 0x20) );
   // Switch system clock source to HS RCOSC (CLKCON.OSC = 1),
   // and set max CPU clock speed (CLKCON.CLKSPD = 1).
   CLKCON = (CLKCON & ~0x07) | 0x40 | 0x01;
   // Wait until system clock source has actually changed (CLKCON.OSC = 1)
   while ( !(CLKCON & 0x40) );
   // Power down [HS XOSC] (SLEEP.OSC_PD = 1)
   SLEEP |= 0x04;
}

void switchToHSXOSC(void)
{
   // Power up [HS XOSC] (SLEEP.OSC_PD = 0)
   SLEEP &= ~0x04;
   // Wait until [HS XOSC] is stable (SLEEP.XOSC_STB = 1)
   while ( ! (SLEEP & 0x40) );
   // Switch system clock source to [HS XOSC] (CLKCON.OSC = 0)
   CLKCON &= ~0x40;
   // Wait until system clock source has actually changed (CLKCON.OSC = 0)
   while ( CLKCON & 0x40 );
   // Power down [HS RCOSC] (SLEEP.OSC_PD = 1)
   SLEEP |= 0x04; 
}

void sleepMode1(uint16 seconds)
{
   unsigned char temp;
   unsigned short desired_event0;

   desired_event0 = seconds;
    
   // make sure interrupts aren't completely disabled
   // and enable sleep timer interrupt
   IEN0 |= 0xA0;
       
   WORCTRL |= 0x04; // Reset Sleep Timer
   temp = WORTIME0;
   while(temp == WORTIME0); // Wait until a positive 32 kHz edge
   temp = WORTIME0;
   while(temp == WORTIME0); // Wait until a positive 32 kHz edge
   WOREVT1 = desired_event0 >> 8; // Set EVENT0, high byte
   WOREVT0 = desired_event0; // Set EVENT0, low byte
  
   // Set SLEEP.MODE according to PM1
   SLEEP = (SLEEP & 0xFC) | 0x01;
   
   // Apply three NOPs to allow the corresponding interrupt blocking to take
   // effect, before verifying the SLEEP.MODE bits below. Note that all
   // interrupts are blocked when SLEEP.MODE ≠ 0, thus the time between
   // setting SLEEP.MODE ≠ 0, and asserting PCON.IDLE should be as short as
   // possible. If an interrupt occurs before the NOPs have completed, then
   // the enabled ISR shall clear the SLEEP.MODE bits, according to the code
   // in Figure 7.
   
   __asm nop __endasm;
   __asm nop __endasm;
   __asm nop __endasm;
   
   // If no interrupt was executed in between the above NOPs, then all
   // interrupts are effectively blocked when reaching this code position.
   // If the SLEEP.MODE bits have been cleared at this point, which means
   // that an ISR has indeed executed in between the above NOPs, then the
   // application will not enter PM{1 – 3} !
   
   if (SLEEP & 0x03)
   {
      // Set PCON.IDLE to enter the selected PM, e.g. PM1.
      PCON |= 0x01;
      // The SoC is now in PM and will only wake up upon Sleep Timer interrupt
      // or external Port interrupt.
      __asm nop __endasm;    
   }
   
   // Switch back to high speed      
   switchToHSXOSC();   
}

void sleepMode2(uint16 seconds)
{
   unsigned char temp;
   unsigned short desired_event0;
   
   unsigned char storedDescHigh, storedDescLow;
   unsigned char interrupts0, interrupts1, interrupts2;
   
   desired_event0 = seconds;

   // must be using RC OSC before going to PM2
   switchToRCOSC();
   
   // Following DMA code is a workaround for a bug described in Design Note
   // DN106 section 4.1.4 where there is a small chance that the sleep mode
   // bits are faulty set to a value other than zero and this prevents the
   // processor from waking up correctly (appears to hang)
   
   // Store current DMA channel 0 descriptor and abort any ongoing transfers,
   // if the channel is in use.
   storedDescHigh = DMA0CFGH;
   storedDescLow = DMA0CFGL;
   DMAARM |= 0x81;
   // Update descriptor with correct source.
   dmaDesc[0] = (unsigned int)& PM2_BUF >> 8;
   dmaDesc[1] = (unsigned int)& PM2_BUF;
   // Associate the descriptor with DMA channel 0 and arm the DMA channel
   DMA0CFGH = (unsigned int)&dmaDesc >> 8;
   DMA0CFGL = (unsigned int)&dmaDesc;
   DMAARM = 0x01;   
   
   // save enabled interrupts
   interrupts0 = IEN0;
   interrupts1 = IEN1;
   interrupts2 = IEN2; 
   
   // make sure interrupts aren't completely disabled
   // and enable sleep timer interrupt
   IEN0 |= 0xA0;
         
   // then disable all interrupts except the sleep timer
   IEN0 &= 0xA0;
   IEN1 &= ~0x3F;
   IEN2 &= ~0x3F;
          
   WORCTRL |= 0x04; // Reset Sleep Timer
   temp = WORTIME0;
   while(temp == WORTIME0); // Wait until a positive 32 kHz edge
   temp = WORTIME0;
   while(temp == WORTIME0); // Wait until a positive 32 kHz edge
   WOREVT1 = desired_event0 >> 8; // Set EVENT0, high byte
   WOREVT0 = desired_event0; // Set EVENT0, low byte
  
   // Flash cache must be disabled.
   MEMCTR |= 0x02;
   SLEEP = 0x06; // PM2, disable USB, power down other oscillators
    
   __asm nop __endasm; 
   __asm nop __endasm; 
   __asm nop __endasm;
   
   if (SLEEP & 0x03)
   {
      __asm mov 0xD7,#0x01 __endasm; // DMAREQ = 0x01;
      __asm nop __endasm;            // Needed to perfectly align the DMA transfer.
      __asm orl 0x87,#0x01 __endasm; // PCON |= 0x01;
      __asm nop __endasm;      
   }
   
   // restore enabled interrupts
   IEN0 = interrupts0;
   IEN1 = interrupts1;
   IEN2 = interrupts2; 

   // Switch back to high speed
   switchToHSXOSC();   
}


void sleepMode3(void)
{  
   unsigned char storedDescHigh, storedDescLow;
   
   // must be using RC OSC before going to PM3
   switchToRCOSC();
   
   // Following DMA code is a workaround for a bug described in Design Note
   // DN106 section 4.1.4 where there is a small chance that the sleep mode
   // bits are faulty set to a value other than zero and this prevents the
   // processor from waking up correctly (appears to hang)
   
   // Store current DMA channel 0 descriptor and abort any ongoing transfers,
   // if the channel is in use.
   storedDescHigh = DMA0CFGH;
   storedDescLow = DMA0CFGL;
   DMAARM |= 0x81;
   // Update descriptor with correct source.
   dmaDesc[0] = (unsigned int)& PM3_BUF >> 8;
   dmaDesc[1] = (unsigned int)& PM3_BUF;
   // Associate the descriptor with DMA channel 0 and arm the DMA channel
   DMA0CFGH = (unsigned int)&dmaDesc >> 8;
   DMA0CFGL = (unsigned int)&dmaDesc;
   DMAARM = 0x01;   
   
   // make sure interrupts aren't completely disabled
   IEN0 |= (1<<7);
            
   // Flash cache must be disabled.
   MEMCTR |= 0x02;
   SLEEP = 0x07; // PM3, disable USB, power down other oscillators
    
   __asm nop __endasm; 
   __asm nop __endasm; 
   __asm nop __endasm;
   
   if (SLEEP & 0x03)
   {
      __asm mov 0xD7,#0x01 __endasm; // DMAREQ = 0x01;
      __asm nop __endasm;            // Needed to perfectly align the DMA transfer.
      __asm orl 0x87,#0x01 __endasm; // PCON |= 0x01;
      __asm nop __endasm;      
   }

   // Switch back to high speed
   switchToHSXOSC();   
}

