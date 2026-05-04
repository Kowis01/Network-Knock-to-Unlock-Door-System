// DHCP Library
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

/////////////////////////////////////////
/// RFC 2131 is the structure of DHCP ///
///  RFC 2132 is the options for DHCP ///
/////////////////////////////////////////

// Options we will want to utilize in the future:
//      Ip address
//      subnet mask
//      DNS server
//      Time server => NPT PORT 123
//      Gateway Address
//      T1 - Time of T1
//      T2 - Time of T2

//SendPendingMessages will be sent everytime based on the state
//Down in the processingResponses is where we update the state
//So when it loops back through SendPendingMessages it will send based on the updated state

//       Binding State
// Start 3 timers - T, T1, and T2
//
// RENEW sends a message to the IP address of the DHCP server from the binding stage (To the stored IP address)
// REBINDING sends a message to EVERYONE to find a new DHCP server (BROADCAST message)

//Send discover -> Receive Offer -> Send Select -> Receive Ack -> BINDING STATE (Loops back and forth from binding to renew states ==> Then might go to REBIND if T2 goes off)

// T is the total time (T1 and T2 are percentages of T)
// T1 controls renew state -> Renews current IP address with current server
// T2 controls rebind state - > Rebinds to a new DHCP server and gets a new IP address


//                            Socket information

// Starting in INIT, socket information is initialized to broadcast values

// Once i get the initial offer, set the remote source mac and remote ip address to values from offer
// Socket stays in this state until REBINDING (T2 expires)
// When T2 expires, set the socket remote mac and ip address back to broadcast


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "dhcp.h"
#include "arp.h"
#include "gpio.h"
#include "timer.h"

#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3

#define DHCPDISCOVER 1 // Client (me)
#define DHCPOFFER    2 // Server
#define DHCPREQUEST  3 // Client (me)
#define DHCPDECLINE  4 // Client (me)
#define DHCPACK      5 // Server
#define DHCPNAK      6 // Server
#define DHCPRELEASE  7 // Client (me)
#define DHCPINFORM   8 // CLient (me)

#define DHCP_DISABLED   0 // Enable DHCP
#define DHCP_INIT       1 // Send Discovery
#define DHCP_SELECTING  2 // Look through offer
#define DHCP_REQUESTING 3 // Send request to server for ip from offer, start timers (T1 and T2)
#define DHCP_TESTING_IP 4 // Test the ip through ARP (Separate timer for this)
#define DHCP_BOUND      5 // If not being used, bound to it
#define DHCP_RENEWING   6 // If T1 expires, send renew
#define DHCP_REBINDING  7 // If T2 expires, send discovery to all DHCP servers
#define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
#define DHCP_REBOOTING  9 // not used since ip not stored over reboot
#define DHCP_RELEASE 10

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint32_t randXID = 0; // XID generated on every Discovery message - Every other message uses the same XID
uint32_t leaseSeconds = 0; // Lease time given in Offer message
uint32_t leaseT1 = 0;
uint32_t leaseT2 = 0;

// Use these variables if you want
bool discoverNeeded = true;
bool requestNeeded = true;
bool rebindNeeded = true;

bool releaseNeeded = false; // Flag to send release message
bool renewNeeded = false; // Flag to loop back and send discover again

bool RequestTimers = false; // Flag

bool ipConflictDetectionMode = false;

uint8_t dhcpOfferedIpAdd[4]; // The address the server offers to you
uint8_t dhcpServerIpAdd[4];  // The address of the server (your landlord)

uint8_t dhcpServerHwAddress[4];

uint8_t tempServer_IpAddress[4]; // Option 54
uint8_t tempServer_SubnetMask[4]; // Option 1
uint8_t tempServer_DNS[4];
uint8_t tempServer_Gateway[4];

uint8_t zeroIP[4] = {0, 0, 0, 0};

uint8_t dhcpState = DHCP_DISABLED;
bool    dhcpEnabled = false;
bool BoundServerData = false;

static socket *dhcpSocket = NULL;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// State functions

void setDhcpState(uint8_t state)
{
    dhcpState = state;
}

uint8_t getDhcpState()
{
    return dhcpState;
}

// New address functions
// Manually requested at start-up
// Discover messages sent every 15 seconds

void DiscoverBufferTimer(){
    discoverNeeded = true;
}

void callbackDhcpGetNewAddressTimer()
{

}

void PeriodicRequestSending()
{
    dhcpState = DHCP_REQUESTING;
}

void requestDhcpNewAddress()
{
    dhcpState = DHCP_INIT;
    RequestTimers = false;
}

// Renew functions

void renewDhcp()
{
    releaseNeeded = true;
    renewNeeded = true;
}

void callbackDhcpT1PeriodicTimer()
{
    requestNeeded = true;
}

// Rebind functions

void rebindDhcp()
{

}

void callbackDhcpT2PeriodicTimer()
{
    rebindNeeded = true;
}

void callbackDhcpT2HitTimer()
{
    dhcpState = DHCP_REBINDING;
    startPeriodicTimer(callbackDhcpT2PeriodicTimer, 2);
}

void callbackDhcpT1HitTimer()
{
    dhcpState = DHCP_RENEWING;
    startPeriodicTimer(callbackDhcpT1PeriodicTimer, 2);
    startOneshotTimer(callbackDhcpT2HitTimer, (leaseSeconds - leaseT2));
}

// End of lease timer
void callbackDhcpLeaseEndTimer()
{
    dhcpState = DHCP_INIT;
    releaseNeeded = true;
    renewNeeded = true;
}

// Release functions

void releaseDhcp()
{
    releaseNeeded = true;
    renewNeeded = false;
}

// IP conflict detection

void callbackDhcpIpConflictWindow()
{
    BoundServerData = true;
}

void requestDhcpIpConflictTest()
{

}

bool isDhcpIpConflictDetectionMode()
{
    return ipConflictDetectionMode;
}

// Lease functions

uint32_t getDhcpLeaseSeconds()
{
    return leaseSeconds;
}

// Determines whether packet is DHCP
// Must be a UDP packet
bool isDhcpResponse(etherHeader* ether)
{
    //Determine if it is dhcp (validate port number, magic cookie, and bootp)

    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;

    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);

    if(ntohs(udp->sourcePort) != 67) return false;
    if(ntohs(udp->destPort) != 68) return false;

    dhcpFrame* dhcp = (dhcpFrame*)udp->data;

    if(ntohl(dhcp->xid) != randXID) return false;

    if(ntohl(dhcp->magicCookie) != 0x63825363) return false;

    if(dhcp->op != 2) return false;

    return true;
}

// Send DHCP message
void sendDhcpMessage(etherHeader *ether, uint8_t type)
{
    int i;

    uint8_t data[300];
    memset(data, 0, sizeof(data));
    dhcpFrame *dhcp = (dhcpFrame*)data;

    uint8_t HWAddress[6];

    uint8_t *opt = (uint8_t*)dhcp->options; // Recast pointer for options field
    uint16_t dataSize;

    switch(type){

        case DHCPDISCOVER:

            if(discoverNeeded){
                randXID = rand();

                dhcp->op = 1;    // For request
                dhcp->htype = 1; // For ethernet
                dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
                dhcp->hops = 0;  // Losh said so
                dhcp->xid = htonl(randXID); // Random value chose by client
                dhcp->secs = htons(0); //
                dhcp->flags = htons(0x8000);

                getEtherMacAddress(HWAddress);

                for (i = 0; i < 6; i++){
                    dhcp->chaddr[i] = HWAddress[i];
                }

                dhcp->magicCookie = htonl(0x63825363);

                *opt++ = 53; // Code
                *opt++ = 1;  // Length
                *opt++ = DHCPDISCOVER; // Type "1" for DHCPDISCOVER

                *opt++ = 61;
                *opt++ = 6;
                *opt++ = 2;
                *opt++ = 3;
                *opt++ = 4;
                *opt++ = 5;
                *opt++ = 6;
                *opt++ = 115;

                *opt++ = 255; // End options

                dataSize = (uint16_t)(opt - data);


                restartTimer(requestDhcpNewAddress);

                sendUdpMessage(ether, *dhcpSocket, data, dataSize);
                dhcpState = DHCP_SELECTING;
            }

            break;

        case DHCPREQUEST:

            dhcp->op = 1;    // For request
            dhcp->htype = 1; // For ethernet
            dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
            dhcp->hops = 0;  // Losh said so
            dhcp->xid = htonl(randXID); // Random value chose by client
            dhcp->secs = htons(0);
            dhcp->flags = htons(0x0000);

            getEtherMacAddress(HWAddress);

            for (i = 0; i < 6; i++){
                dhcp->chaddr[i] = HWAddress[i];
            }

            dhcp->magicCookie = htonl(0x63825363);

            *opt++ = 53; // Code
            *opt++ = 1;  // Length
            *opt++ = DHCPREQUEST;

            *opt++ = 50; // Code for IP address offered to me
            *opt++ = 4;
            *opt++ = dhcpOfferedIpAdd[0];
            *opt++ = dhcpOfferedIpAdd[1];
            *opt++ = dhcpOfferedIpAdd[2];
            *opt++ = dhcpOfferedIpAdd[3];

            *opt++ = 54; // Code for IP address of server
            *opt++ = 4;
            *opt++ = dhcpServerIpAdd[0];
            *opt++ = dhcpServerIpAdd[1];
            *opt++ = dhcpServerIpAdd[2];
            *opt++ = dhcpServerIpAdd[3];

            *opt++ = 55; // Code for Parameter list
            *opt++ = 3;
            *opt++ = 1;
            *opt++ = 3;
            *opt++ = 6;

            *opt++ = 255; // End options

            dataSize = (uint16_t)(opt - data);

            if(!RequestTimers){
                RequestTimers = true;
                restartTimer(requestDhcpNewAddress);
                restartTimer(PeriodicRequestSending);
            }

            sendUdpMessage(ether, *dhcpSocket, data, dataSize);
            dhcpState = DHCP_TESTING_IP;

            break;

        case DHCPDECLINE:

            dhcp->op = 1;    // For request
            dhcp->htype = 1; // For ethernet
            dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
            dhcp->hops = 0;  // Losh said so
            dhcp->xid = htonl(randXID); // Random value chose by client
            dhcp->secs = htons(0); //
            dhcp->flags = htons(0x0000);

            getEtherMacAddress(HWAddress);

            for (i = 0; i < 6; i++){
                dhcp->chaddr[i] = HWAddress[i];
            }

            dhcp->magicCookie = htonl(0x63825363);

            *opt++ = 53; // Code
            *opt++ = 1;  // Length
            *opt++ = DHCPDECLINE; // Type "4" for DHCPDECLINE

            *opt++ = 255; // End options

            dataSize = (uint16_t)(opt - data);

            sendUdpMessage(ether, *dhcpSocket, data, dataSize);

            for(i = 0; i < 4; i++){
                dhcpSocket->remoteIpAddress[i] = 0xFF;
            }

            for(i = 0; i < 6; i++){
                dhcpSocket->remoteHwAddress[i] = 0xFF;
            }

            dhcpState = DHCP_INIT;
            discoverNeeded = false;
            restartTimer(DiscoverBufferTimer);

            break;

        case DHCP_RENEWING: // Renew message - Sends the same as a request except as unicast to the known server

            if(requestNeeded){

                requestNeeded = false;

                dhcp->op = 1;    // For request
                dhcp->htype = 1; // For ethernet
                dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
                dhcp->hops = 0;  // Losh said so
                dhcp->xid = htonl(randXID); // Random value chose by client
                dhcp->secs = htons(0);
                dhcp->flags = htons(0x0000);

                for(i = 0; i < 4; i++){
                    dhcp->ciaddr[i] = dhcpOfferedIpAdd[i];
                }

                getEtherMacAddress(HWAddress);

                for (i = 0; i < 6; i++){
                    dhcp->chaddr[i] = HWAddress[i];
                }

                dhcp->magicCookie = htonl(0x63825363);

                *opt++ = 53; // Code
                *opt++ = 1;  // Length
                *opt++ = DHCPREQUEST;

                *opt++ = 50; // Code for IP address offered to me
                *opt++ = 4;
                *opt++ = dhcpOfferedIpAdd[0];
                *opt++ = dhcpOfferedIpAdd[1];
                *opt++ = dhcpOfferedIpAdd[2];
                *opt++ = dhcpOfferedIpAdd[3];

                *opt++ = 54; // Code for IP address of server
                *opt++ = 4;
                *opt++ = dhcpServerIpAdd[0];
                *opt++ = dhcpServerIpAdd[1];
                *opt++ = dhcpServerIpAdd[2];
                *opt++ = dhcpServerIpAdd[3];

                *opt++ = 255; // End options

                dataSize = (uint16_t)(opt - data);

                sendUdpMessage(ether, *dhcpSocket, data, dataSize);
            }

            break;

        case DHCP_REBINDING: // Renew message - Sends the same as a request except as unicast to the known server

            if(rebindNeeded){

                rebindNeeded = false;

                dhcp->op = 1;    // For request
                dhcp->htype = 1; // For ethernet
                dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
                dhcp->hops = 0;  // Losh said so
                dhcp->xid = htonl(randXID); // Random value chose by client
                dhcp->secs = htons(0);
                dhcp->flags = htons(0x8000);

                for(i = 0; i < 4; i++){
                    dhcp->ciaddr[i] = dhcpOfferedIpAdd[i];
                }

                getEtherMacAddress(HWAddress);

                for (i = 0; i < 6; i++){
                    dhcp->chaddr[i] = HWAddress[i];
                }

                dhcp->magicCookie = htonl(0x63825363);

                *opt++ = 53; // Code
                *opt++ = 1;  // Length
                *opt++ = DHCPREQUEST; // Type "1" for DHCPDISCOVER

                *opt++ = 50; // Code for IP address offered to me
                *opt++ = 4;
                *opt++ = dhcpOfferedIpAdd[0];
                *opt++ = dhcpOfferedIpAdd[1];
                *opt++ = dhcpOfferedIpAdd[2];
                *opt++ = dhcpOfferedIpAdd[3];

                *opt++ = 54; // Code for IP address of server
                *opt++ = 4;
                *opt++ = dhcpServerIpAdd[0];
                *opt++ = dhcpServerIpAdd[1];
                *opt++ = dhcpServerIpAdd[2];
                *opt++ = dhcpServerIpAdd[3];

                *opt++ = 255; // End options

                dataSize = (uint16_t)(opt - data);

                sendUdpMessage(ether, *dhcpSocket, data, dataSize);

            }

            break;

        case DHCP_RELEASE:

            dhcp->op = 1;    // For request
            dhcp->htype = 1; // For ethernet
            dhcp->hlen = 6;  // "6" for 6 bytes of hardware address
            dhcp->hops = 0;  // Losh said so
            dhcp->xid = htonl(randXID); // Random value chose by client
            dhcp->secs = htons(0);
            dhcp->flags = htons(0x0000);

            for(i = 0; i < 4; i++){
                dhcp->ciaddr[i] = dhcpOfferedIpAdd[i];
            }

            getEtherMacAddress(HWAddress);

            for (i = 0; i < 6; i++){
                dhcp->chaddr[i] = HWAddress[i];
            }

            dhcp->magicCookie = htonl(0x63825363);

            *opt++ = 53; // Code
            *opt++ = 1;  // Length
            *opt++ = DHCPRELEASE; // Type "1" for DHCPDISCOVER

            *opt++ = 54; // Code for IP address of server
            *opt++ = 4;
            *opt++ = dhcpServerIpAdd[0];
            *opt++ = dhcpServerIpAdd[1];
            *opt++ = dhcpServerIpAdd[2];
            *opt++ = dhcpServerIpAdd[3];

            *opt++ = 255; // End options

            dataSize = (uint16_t)(opt - data);

            sendUdpMessage(ether, *dhcpSocket, data, dataSize);

            break;

    }
}

uint8_t* getDhcpOption(etherHeader *ether, uint8_t option, uint8_t* length)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;

    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);

    dhcpFrame *dhcp = (dhcpFrame*)udp->data;

    uint16_t udpLength = ntohs(udp->length); // Give the length of the Udp message (Header and data)
    uint16_t dhcpData = udpLength - sizeof(udpHeader); // Gives the length of the DHCP Message

    uint8_t *end = (uint8_t*)dhcp + dhcpData; // End of the entire DHCP message

    uint8_t *opt = dhcp->options; // Start of the options field

    while (opt < end)
        {
            uint8_t code = *opt++;

            if (code == 0){ // If you hit a 0, thats padding, so skip that byte
                continue;
            }

            if (code == 255){ // End of the options, return 0
                return 0;
            }

            if (opt >= end){ // Checks if the end of the options
                return 0;
            }

            uint8_t len = *opt++;

           if ((uint16_t)(end - opt) < len){
               return 0;
           }
           if(code == option){
               *length = len;
               return opt;
           }

            opt += len; // If code does not return 53 then move to the next code and check
        }


    return 0;
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
bool isDhcpOffer(etherHeader *ether, uint8_t ipOfferedAdd[], uint8_t ipServerAdd[])
{
    bool ok = false;
    int i;
    uint8_t len;
    uint8_t *optionData = getDhcpOption(ether, 53, &len); // This is offer type if(optionData = 2) it is an offer

    if(optionData && len == 1 && optionData[0] == 2){
        ok = true;

        dhcpFrame *dhcp = getUdpData(ether);

        for(i = 0; i < 4; i++){
            ipOfferedAdd[i] = dhcp->yiaddr[i];
        }

        if(ipOfferedAdd[0] == 10){
            return false;
        }

        uint8_t len2;
        uint8_t *ServeripData = getDhcpOption(ether, 54, &len2);

        if(ServeripData && len2 == 4){
            for(i = 0; i < 4; i++){
                ipServerAdd[i] = ServeripData[i];
            }
        }

    }
    return ok;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool isDhcpAck(etherHeader *ether)
{
    bool ok = false;
     int i;
     uint8_t len;
     uint8_t *optionData = getDhcpOption(ether, 53, &len); // This is offer type if(optionData = 2) it is an offer

     if(optionData && len == 1 && optionData[0] == 5){
         ok = true;

         uint8_t DHCPServerAddLen;
         uint8_t *DHCPServerAddData = getDhcpOption(ether, 54, &DHCPServerAddLen);

         uint8_t grabSubnetLength;
         uint8_t *SubnetMaskData = getDhcpOption(ether, 1, &grabSubnetLength);

         uint8_t DnsAddressLength;
         uint8_t *DnsAddData = getDhcpOption(ether, 6, &DnsAddressLength);

         uint8_t ServerGatewayLength;
         uint8_t *ServerGatewayData = getDhcpOption(ether, 3, &ServerGatewayLength);

         if(DHCPServerAddData && SubnetMaskData && DnsAddData && ServerGatewayData){
                 if(DHCPServerAddLen == 4 && grabSubnetLength == 4 && DnsAddressLength == 4 && ServerGatewayLength == 4){
                     for(i = 0; i < 4; i++){
                         tempServer_IpAddress[i] = DHCPServerAddData[i];
                         tempServer_SubnetMask[i] = SubnetMaskData[i];
                         tempServer_DNS[i] = DnsAddData[i];
                         tempServer_Gateway[i] = ServerGatewayData[i];
                     }
                 }
         }

         for(i = 0; i < 4; i++){
             dhcpServerHwAddress[i] = ether->sourceAddress[i];
         }


                uint8_t LeaseTimeLen;
                uint8_t *LeaseTimeData = getDhcpOption(ether, 51, &LeaseTimeLen);

                if(LeaseTimeData && LeaseTimeLen == 4){
                    leaseSeconds = LeaseTimeData; // Option 51 and length 4
                    leaseT1 = leaseSeconds/2;
                    leaseT2 = leaseSeconds * 7 / 8;
                }

     }

     return ok;
}

// Handle a DHCP ACK
void handleDhcpAck(etherHeader *ether)
{

}

// Message requests

bool isDhcpDiscoverNeeded()
{
    return false;
}

bool isDhcpRequestNeeded()
{
    return false;
}

bool isDhcpReleaseNeeded()
{
    return false;
}

void sendDhcpPendingMessages(etherHeader *ether)
{
    int i = 0;

    if(releaseNeeded){
        releaseNeeded = false;
        sendDhcpMessage(ether, DHCP_RELEASE);
        setIpAddress(zeroIP);
        dhcpState = DHCP_INIT;

        if(renewNeeded){
            renewNeeded = false;
            discoverNeeded = true;
        }
        else{
            discoverNeeded = false;
        }
    }

    switch(dhcpState){

        case DHCP_INIT:
            sendDhcpMessage(ether, DHCPDISCOVER);
            break;

        case DHCP_REQUESTING:
            sendDhcpMessage(ether, DHCPREQUEST);
            break;

        case DHCP_BOUND:
            if(ipConflictDetectionMode){
                ipConflictDetectionMode = false;
                sendArpRequest(ether, zeroIP, dhcpOfferedIpAdd);
                restartTimer(callbackDhcpIpConflictWindow);
            }
            break;

        case DHCP_RENEWING:
            sendDhcpMessage(ether, DHCP_RENEWING);
            break;

        case DHCP_REBINDING:
            sendDhcpMessage(ether, DHCP_REBINDING);
            break;
    }



    if(BoundServerData){
        BoundServerData = false; // If Arp Call back goes off, then that means my request did not get a response. In that callback, BoundServerData is set to true

        for(i = 0; i < 4; i++){
            dhcpServerIpAdd[i] = tempServer_IpAddress[i];
        }

        setIpAddress(dhcpOfferedIpAdd);
        setIpSubnetMask(tempServer_SubnetMask);
        setIpGatewayAddress(tempServer_Gateway);
        setIpDnsAddress(tempServer_DNS);

    }

}

void processDhcpResponse(etherHeader *ether)
{
    int i;
    uint8_t ipOfferedAdd[4];
    uint8_t ipServerAdd[4];
    switch(dhcpState){

        case DHCP_SELECTING:

            if(isDhcpOffer(ether, ipOfferedAdd, ipServerAdd)){

                stopTimer(requestDhcpNewAddress);

                for(i = 0; i < 4; i++){
                    dhcpOfferedIpAdd[i] = ipOfferedAdd[i]; //Copies Offered ip address into global variable
                    dhcpServerIpAdd[i] = ipServerAdd[i]; //Copies servers ip address to global variable
                }

                dhcpState = DHCP_REQUESTING;
            }
            break;

        case DHCP_TESTING_IP:

            if(isDhcpAck(ether)){
                setPinValue(GREEN_LED, 0);
                setPinValue(BLUE_LED, 1);

                stopTimer(requestDhcpNewAddress);
                stopTimer(PeriodicRequestSending);

                dhcpFrame *dhcp = getUdpData(ether);

                for(i = 0; i < 4; i++){
                    ipOfferedAdd[i] = dhcp->yiaddr[i];
                }

                for(i = 0; i < 6; i++){
                    dhcpServerHwAddress[i] = ether->sourceAddress[i];
                }
/*
                leaseSeconds = 60;
                leaseT1 = leaseSeconds/2;
                leaseT2 = leaseSeconds * 7/8;
*/
                startOneshotTimer(callbackDhcpT1HitTimer, leaseT1);
                startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds);

//                startOneshotTimer(callbackDhcpT2HitTimer, leaseT2);
//                startPeriodicTimer(callbackDhcpT2PeriodicTimer, 2);
//
//                startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds);

                ipConflictDetectionMode = true;
                dhcpState = DHCP_BOUND;


            }
            break;

        case DHCP_RENEWING:

            if(isDhcpAck(ether)){

                restartTimer(callbackDhcpLeaseEndTimer); // Restart big T
                stopTimer(callbackDhcpT1PeriodicTimer); // Stop the periodic Requests

                restartTimer(callbackDhcpT1HitTimer); // Restart T1 and go back to bound

                dhcpState = DHCP_BOUND;

            }

            break;

        case DHCP_REBINDING:

            if(isDhcpAck(ether)){

                restartTimer(callbackDhcpLeaseEndTimer); // Restart big T
                stopTimer(callbackDhcpT2PeriodicTimer); // Stop Periodic Requests

                restartTimer(callbackDhcpT1HitTimer); // Restart T1 Go back to bound

                dhcpState = DHCP_BOUND;

            }

            break;

    }
}

void processDhcpArpResponse(etherHeader *ether)
{
 /*   int i;
    for(i = 0; i < 4; i++){
        dhcpOfferedIpAdd[i] = 0;
    }

    stopTimer(callbackDhcpIpConflictWindow);
    dhcpState = DHCPDECLINE;
    */
}

void enableDhcp()
{
    if(dhcpState == DHCP_DISABLED){ // IF DHCP is disabled, we need to set it to INIT State to send broadcast
                                    // If DHCP is already enabled, then we are good and all of this has already been done
    dhcpEnabled = true; //Sets global variables based on state
    dhcpState = DHCP_INIT;
    discoverNeeded = true;

    if(dhcpSocket == NULL) dhcpSocket = newSocket(); //Makes new socket

    if(dhcpSocket == NULL){ //Checks if socket was available
        disableDhcp();
        return;
    }

    int i;

    for(i = 0;  i < 6; i++){
        dhcpSocket->remoteHwAddress[i] = 0xFF; //Address to broadcast to all devices on network
    }

    dhcpSocket->remoteIpAddress[0] = 255; //Address to broadcast to all networks
    dhcpSocket->remoteIpAddress[1] = 255;
    dhcpSocket->remoteIpAddress[2] = 255;
    dhcpSocket->remoteIpAddress[3] = 255;

    dhcpSocket->localPort = 68;   //Universal ports for DCHP
    dhcpSocket->remotePort = 67;

    dhcpSocket->sequenceNumber = 0; //Andrew said we dont need these, initialize to 0
    dhcpSocket->acknowledgementNumber = 0;
    dhcpSocket->state = 1;

    }
}

void disableDhcp()
{
    //Also kill timers
    dhcpEnabled = false;
    dhcpState = DHCP_DISABLED;

    discoverNeeded = false;

    dhcpSocket = NULL;
    //Send a request to DHCP server to tell it you are done with the IP address
    //XID should be the same up until the binding state //Decline message should also have the same XID
    //Other than those messages, the DHCP server uses the MAC Address to link to the IP Address
}

bool isDhcpEnabled()
{
    return dhcpEnabled;
}

void startTimersNeeded(){

    startOneshotTimer(requestDhcpNewAddress, 5); // OneShot timer before sending next discover
    stopTimer(requestDhcpNewAddress);

    startPeriodicTimer(PeriodicRequestSending, 2); // Periodic timer to send Requests waiting for offer
    stopTimer(PeriodicRequestSending);

    startOneshotTimer(DiscoverBufferTimer, 15); // Wait 15 seconds after sending decline message
    stopTimer(DiscoverBufferTimer);

    startOneshotTimer(callbackDhcpIpConflictWindow, 2); // If arp does not get a response
    stopTimer(callbackDhcpIpConflictWindow);

}

