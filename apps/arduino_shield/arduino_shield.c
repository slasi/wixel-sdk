/* arduino_shield app:
 * This is a modified version of the wireless_serial app designed to work with
 * the Wixel Shield for Arduino:
 * http://www.pololu.com/catalog/product/2500
 */

/** Dependencies **************************************************************/
#include <wixel.h>

#include <usb.h>
#include <usb_com.h>

#include <radio_com.h>
#include <radio_link.h>

#include <uart1.h>

/** Parameters ****************************************************************/
#define SERIAL_MODE_AUTO        0
#define SERIAL_MODE_USB_RADIO   1
#define SERIAL_MODE_UART_RADIO  2
#define SERIAL_MODE_USB_UART    3
int32 CODE param_serial_mode = SERIAL_MODE_AUTO;

int32 CODE param_baud_rate = 9600;

int32 CODE param_nDTR_pin = 10;
int32 CODE param_nRTS_pin = 11;
int32 CODE param_nDSR_pin = 12;
int32 CODE param_nCD_pin = 13;

int32 CODE param_DTR_pin = -1;
int32 CODE param_RTS_pin = -1;
int32 CODE param_DSR_pin = -1;
int32 CODE param_CD_pin = -1;

int32 CODE param_arduino_DTR_pin = 0;

/** Global variables **********************************************************/

BIT uartRxDisabled = 0;


/** Functions *****************************************************************/
void updateLeds()
{
    usbShowStatusWithGreenLed();

    LED_YELLOW(vinPowerPresent());

    // Turn on the red LED if the radio is in the RX_OVERFLOW state.
    // There used to be several bugs in the radio libraries that would cause
    // the radio to go into this state, but hopefully they are all fixed now.
    if (MARCSTATE == 0x11)
    {
        LED_RED(1);
    }
    else
    {
        LED_RED(0);
    }
}

void framingErrorService()
{
    static uint8 lastErrorTime;

    // We need this variable because we don't want to be reading U1CSR in the main loop,
    // because reading it might caused the FE or ERR bits to be cleared and then the ISR
    // would not receive notice of those errors.

    if (uart1RxFramingErrorOccurred)
    {
        uart1RxFramingErrorOccurred = 0;
        U1CSR &= ~0x40;    // U1CSR.RE = 0.  Disables reception of bytes on the UART.
        lastErrorTime = (uint8)getMs();
        uartRxDisabled = 1;
    }

    if (uartRxDisabled)
    {
        if (!isPinHigh(17))
        {
            // The line is still low.
            lastErrorTime = (uint8)getMs();
        }
        else if ((uint8)(getMs() - lastErrorTime) > 5)
        {
            // Re-enable the receiver.
            U1CSR |= 0x40;
            uartRxDisabled = 0;
        }
    }
}

uint8 ioRxSignals()
{
    uint8 signals = 0;

    if ((param_CD_pin >= 0 && isPinHigh(param_CD_pin)) ||
            (param_nCD_pin >= 0 && !isPinHigh(param_nCD_pin)))
    {
        signals |= 2;
    }

    if ((param_DSR_pin >= 0 && isPinHigh(param_DSR_pin)) ||
            (param_nDSR_pin >= 0 && !isPinHigh(param_nDSR_pin)))
    {
        signals |= 1;
    }

    return signals;
}

void ioTxSignals(uint8 signals)
{
    static uint8 nTrstPulseStartTime;
    static uint8 lastSignals;

    // Inverted outputs
    setDigitalOutput(param_nDTR_pin, (signals & ACM_CONTROL_LINE_DTR) ? 0 : 1);
    setDigitalOutput(param_nRTS_pin, (signals & ACM_CONTROL_LINE_RTS) ? 0 : 1);

    // Non-inverted outputs.
    setDigitalOutput(param_DTR_pin, (signals & ACM_CONTROL_LINE_DTR) ? 1 : 0);
    setDigitalOutput(param_RTS_pin, (signals & ACM_CONTROL_LINE_RTS) ? 1 : 0);

    // Arduino DTR pin.
    if (!(lastSignals & ACM_CONTROL_LINE_DTR) && (signals & ACM_CONTROL_LINE_DTR))
    {
        // We just made a falling edge on the nDTR line, so start a 1-2ms high pulse
        // on the nTRST line.
        setDigitalOutput(param_arduino_DTR_pin, HIGH);
        nTrstPulseStartTime = getMs();
    }
    else if ((uint8)(getMs() - nTrstPulseStartTime) >= 2)
    {
        setDigitalOutput(param_arduino_DTR_pin, LOW);
    }

    lastSignals = signals;
}

uint8 currentSerialMode()
{
    if ((uint8)param_serial_mode > 0 && (uint8)param_serial_mode <= 3)
    {
        return (uint8)param_serial_mode;
    }

    if (usbPowerPresent())
    {
        if (vinPowerPresent())
        {
            return SERIAL_MODE_USB_UART;
        }
        else
        {
            return SERIAL_MODE_USB_RADIO;
        }
    }
    else
    {
        return SERIAL_MODE_UART_RADIO;
    }
}

void usbToRadioService()
{
    uint8 signals;

    // Data
    while(usbComRxAvailable() && radioComTxAvailable())
    {
        radioComTxSendByte(usbComRxReceiveByte());
    }

    while(radioComRxAvailable() && usbComTxAvailable())
    {
        usbComTxSendByte(radioComRxReceiveByte());
    }

    // Control Signals

    radioComTxControlSignals(usbComRxControlSignals() & 3);

    // Need to switch bits 0 and 1 so that DTR pairs up with DSR.
    signals = radioComRxControlSignals();
    usbComTxControlSignals( ((signals & 1) ? 2 : 0) | ((signals & 2) ? 1 : 0));
}

void uartToRadioService()
{
    // Data
    while(uart1RxAvailable() && radioComTxAvailable())
    {
        radioComTxSendByte(uart1RxReceiveByte());
    }

    while(radioComRxAvailable() && uart1TxAvailable())
    {
        uart1TxSendByte(radioComRxReceiveByte());
    }

    // Control Signals.
    ioTxSignals(radioComRxControlSignals());
    radioComTxControlSignals(ioRxSignals());
}

void usbToUartService()
{
    uint8 signals;

    // Data
    while(usbComRxAvailable() && uart1TxAvailable())
    {
        uart1TxSendByte(usbComRxReceiveByte());
    }

    while(uart1RxAvailable() && usbComTxAvailable())
    {
        usbComTxSendByte(uart1RxReceiveByte());
    }

    ioTxSignals(usbComRxControlSignals());

    // Need to switch bits 0 and 1 so that DTR pairs up with DSR.
    signals = ioRxSignals();
    usbComTxControlSignals( ((signals & 1) ? 2 : 0) | ((signals & 2) ? 1 : 0));

    // TODO: report framing, parity, and overrun errors to the USB host here
}

void main()
{
    systemInit();

    setDigitalOutput(param_arduino_DTR_pin, LOW);
    ioTxSignals(0);

    usbInit();

    uart1Init();
    uart1SetBaudRate(param_baud_rate);

    if (param_serial_mode != SERIAL_MODE_USB_UART)
    {
        radioComRxEnforceOrdering = 1;
        radioComInit();
    }

    // Set up P1_5 to be the radio's TX debug signal.
    P1DIR |= (1<<5);
    IOCFG0 = 0b011011; // P1_5 = PA_PD (TX mode)

    while(1)
    {
        boardService();
        updateLeds();

        if (param_serial_mode != SERIAL_MODE_USB_UART)
        {
            radioComTxService();
        }

        usbComService();

        framingErrorService();

        switch(currentSerialMode())
        {
        case SERIAL_MODE_USB_RADIO:  usbToRadioService();  break;
        case SERIAL_MODE_UART_RADIO: uartToRadioService(); break;
        case SERIAL_MODE_USB_UART:   usbToUartService();   break;
        }
    }
}