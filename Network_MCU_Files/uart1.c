#include <stdint.h>
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "uart1.h"
#include "gpio.h"

// PB0 = U1RX
// PB1 = U1TX
#define UART1_TX PORTB,1
#define UART2_RX PORTB,0

void initUart1(void)
{
    // Enable clocks
    SYSCTL_RCGCUART_R |= SYSCTL_RCGCUART_R1;
    _delay_cycles(3);
    enablePort(PORTB);

    // Configure UART1 pins
    selectPinPushPullOutput(UART1_TX);
    selectPinDigitalInput(UART2_RX);
    setPinAuxFunction(UART1_TX, GPIO_PCTL_PB1_U1TX);
    setPinAuxFunction(UART2_RX, GPIO_PCTL_PB0_U1RX);

    // Configure UART1 with default baud rate
    UART1_CTL_R = 0;                                    // turn-off UART1 to allow safe programming
    UART1_CC_R = UART_CC_CS_SYSCLK;                     // use system clock (usually 40 MHz)
}

// Set UART1 baud rate
void setUart1BaudRate(uint32_t baudRate, uint32_t fcyc)
{
    uint32_t divisorTimes128;

    divisorTimes128 = (fcyc * 8) / baudRate;
    divisorTimes128 += 1;   // rounding

    UART1_CTL_R = 0;                                    // disable UART1 while programming
    UART1_IBRD_R = divisorTimes128 >> 7;                // integer divisor
    UART1_FBRD_R = (divisorTimes128 >> 1) & 63;         // fractional divisor
    UART1_LCRH_R = UART_LCRH_WLEN_8 | UART_LCRH_FEN;    // 8-bit, no parity, 1 stop, FIFO enabled
    UART1_CTL_R = UART_CTL_TXE | UART_CTL_RXE | UART_CTL_UARTEN;
}

// Send one character
void putcUart1(char c)
{
    while (UART1_FR_R & UART_FR_TXFF);
    UART1_DR_R = c;
}

// Send string
void putsUart1(const char *str)
{
    uint8_t i = 0;
    while (str[i] != '\0')
        putcUart1(str[i++]);
}

// Receive one character
char getcUart1(void)
{
    while (UART1_FR_R & UART_FR_RXFE);
    return UART1_DR_R & 0xFF;
}

// Check if data is waiting
bool kbhitUart1(void)
{
    return !(UART1_FR_R & UART_FR_RXFE);
}


