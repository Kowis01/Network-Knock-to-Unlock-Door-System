#ifndef UART1_H_
#define UART1_H_

#include <stdint.h>
#include <stdbool.h>

// UART1 basic functions
void initUart1(void);
void setUart1BaudRate(uint32_t baudRate, uint32_t fcyc);
void putcUart1(char c);
void putsUart1(const char *str);
char getcUart1(void);
bool kbhitUart1(void);

#endif
