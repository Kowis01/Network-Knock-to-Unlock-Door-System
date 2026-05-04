// Socket Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include "arp.h"
#include "ip.h"
#include "udp.h"
#include "tcp.h"

#define MAX_SOCKETS 10

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t socketCount = 0;
socket sockets[MAX_SOCKETS];

//------------------------------------------------------------------------------
//  Structures
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Subroutines
//------------------------------------------------------------------------------

// Every time a SYN is sent, double the time of waiting to retransmit. Start at 1 second, then retransmit. Wait 2 seconds, then retransmit. 6 times and if all fails, go back to CLOSED
// Every time a FIN is sent, send it 3 times each 1 second apart, if no answer, just cut the connection

void initSockets(void)
{
    uint8_t i;
    for (i = 0; i < MAX_SOCKETS; i++){
        sockets[i].state = TCP_CLOSED;
    }

}

socket * newSocket(void)
{
    uint8_t i = 0;
    socket * s = NULL;
    bool foundUnused = false;
    while (i < MAX_SOCKETS && !foundUnused)
    {
        foundUnused = sockets[i].state == TCP_CLOSED;
        if (foundUnused)
            s = &sockets[i];
        i++;
    }
    return s;
}

void deleteSocket(socket *s)
{
    uint8_t i = 0;
    bool foundMatch = false;
    while (i < MAX_SOCKETS && !foundMatch)
    {
        foundMatch = &sockets[i] == s;
        if (foundMatch)
            sockets[i].state = TCP_CLOSED;
        i++;
    }
}

// Get socket information from a received ARP response message
void getSocketInfoFromArpResponse(etherHeader *ether, socket *s)
{
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = arp->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = arp->sourceIp[i];
}

// Get socket information from a received UDP packet
void getSocketInfoFromUdpPacket(etherHeader *ether, socket *s)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = ether->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = ip->sourceIp[i];
    s->remotePort = ntohs(udp->sourcePort);
    s->localPort = ntohs(udp->destPort);
}

// Get socket information from a received TCP packet
void getSocketInfoFromTcpPacket(etherHeader *ether, socket *s)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = ether->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = ip->sourceIp[i];
    s->remotePort = ntohs(tcp->sourcePort);
    s->localPort = ntohs(tcp->destPort);
}

