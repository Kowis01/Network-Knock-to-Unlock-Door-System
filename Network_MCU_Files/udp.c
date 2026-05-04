// UDP Library
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
#include "udp.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Determines whether packet is UDP datagram
// Must be an IP packet
bool isUdp(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    bool ok;
    uint16_t tmp16;
    uint32_t sum = 0;
    ok = (ip->protocol == PROTOCOL_UDP);
    if (ok)
    {
        // 32-bit sum over pseudo-header
        sumIpWords(ip->sourceIp, 8, &sum);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0xff) << 8;
        sumIpWords(&udp->length, 2, &sum);
        // add udp header and data
        sumIpWords(udp, ntohs(udp->length), &sum);
        ok = (getIpChecksum(sum) == 0);
    }
    return ok;
}

// Gets pointer to UDP payload of frame
uint8_t * getUdpData(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    return udp->data;
}

// Send UDP message
void sendUdpMessage(etherHeader *ether, socket s, uint8_t data[], uint16_t dataSize)
{
    uint8_t i;
    uint16_t j;
    uint32_t sum;
    uint16_t tmp16;
    uint16_t udpLength;
    uint8_t *copyData;
    uint8_t localHwAddress[6];
    uint8_t localIpAddress[4];

    // Ether frame
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = s.remoteHwAddress[i]; //Sets the destination hardware address from the socket and fills it in the ether message
        ether->sourceAddress[i] = localHwAddress[i]; //sets the source IP address from the variable that grabs it from our device
    }
    ether->frameType = htons(TYPE_IP); // fills in the frame type => (TYPE_IP for IP message)

    // IP header - fills in order of the message, so the IP Header is next
    ipHeader* ip = (ipHeader*)ether->data; // Sets the address of where the ip message starts (Data variable of ether)
    ip->rev = 0x4; // 4 for rev
    ip->size = 0x5; // 5 for size
    ip->typeOfService = 0; 
    ip->id = 0; 
    ip->flagsAndOffset = 0; 
    ip->ttl = 128; 
    ip->protocol = PROTOCOL_UDP; //Since this is a UDP message, the protocol is Protocol_UDP
    ip->headerChecksum = 0; // Take this out
     for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = s.remoteIpAddress[i]; //Destination address is filled in from socket information
        ip->sourceIp[i] = localIpAddress[i]; // Source IP address is filled in from variable that grabbed it from EEPROM
    }
    uint8_t ipHeaderLength = ip->size * 4; // 5 * 4 = 20 bytes
    //FIRST 34 BYTES OF THE MESSAGE IS COMPLETE

    // UDP header
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + (ip->size * 4)); // UDP address is set from the beginning of the IP address + 20 bytes
    udp->sourcePort = htons(s.localPort); // Source port is filled in from the socket information
    udp->destPort = htons(s.remotePort); // Destination port is filled in from socket information
    // adjust lengths
    udpLength = sizeof(udpHeader) + dataSize; // The length is the size of the header plus the size of the data
    ip->length = htons(ipHeaderLength + udpLength);
    // 32-bit sum over ip header
    calcIpChecksum(ip);
    // set udp length
    udp->length = htons(udpLength);
    // copy data
    copyData = udp->data;
    for (j = 0; j < dataSize; j++)
        copyData[j] = data[j];
    // 32-bit sum over pseudo-header
    sum = 0;
    sumIpWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    sumIpWords(&udp->length, 2, &sum);
    // add udp header
    udp->check = 0;
    sumIpWords(udp, udpLength, &sum);
    udp->check = getIpChecksum(sum);

    // send packet with size = ether + udp hdr + ip header + udp_size
    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + udpLength);
}
