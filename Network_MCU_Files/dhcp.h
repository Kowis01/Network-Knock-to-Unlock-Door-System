// DHCP Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef DHCP_H_
#define DHCP_H_

#include <stdint.h>
#include <stdbool.h>
#include "udp.h"

typedef struct _dhcpFrame // 240 or more bytes
{
  uint8_t op;  // 1 for BOOTREQUEST or 2 for BOOTREPLY
  uint8_t htype; // 
  uint8_t hlen; // '6' for 6 bytes length of hardware address
  uint8_t hops; // We will set it to '1'
  uint32_t  xid; // Random number chosen and used by client to distinguish message
  uint16_t secs; // Elapsed time since the timer began ("How many seconds have i been trying to get an address")
  uint16_t flags;  // 
  uint8_t ciaddr[4]; //Current IP address (Ifill it in based on what state im in. If i need an ip address, set to 0, if i am renewing or rebinding, fill with my current ip)
  uint8_t yiaddr[4]; //IP address  being offered to me (filled by server during a response
  uint8_t siaddr[4]; //Servers ip address (they will fill it)
  uint8_t giaddr[4]; //Gateway ip address, i dont see a purpose to it rn, ignore set to 0
  uint8_t chaddr[16]; //Hardware address - we will use first 6 bytes
  uint8_t data[192]; // 
  uint32_t magicCookie; //This value specifies that it is DHCP
  uint8_t options[0]; //RFC 2132 (first link when googled - *Page 25*) includes the messages that should go in this field
} dhcpFrame;

//

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

bool isDhcpResponse(etherHeader *ether);
void startTimersNeeded();
void sendDhcpPendingMessages(etherHeader *ether);
void processDhcpResponse(etherHeader *ether);
void processDhcpArpResponse(etherHeader *ether);

void enableDhcp(void);
void disableDhcp(void);
bool isDhcpEnabled(void);

void renewDhcp(void);
void releaseDhcp(void);

uint32_t getDhcpLeaseSeconds();

#endif


//The timers can only set flags in the isr
//Never send a message in an isr
//Timers SET flags

//
