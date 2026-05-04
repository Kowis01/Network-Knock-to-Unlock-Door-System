// TCP Library
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

/*
    TCP/IP Lecture (Transfer Control Protocol)
        - What is UDP?
            - A connection-less, protocol used to send data. Does not guarantee delivery or duplicates

        - What is TCP?
            - Defined in RFC 793
            - A communication style that requires a connection. Ordered delivery of data. Consists of remote and local socket. The socket holds the IP address and port

        - TCP State Machine
            - There is a state per connection:
                - CLOSED: No connection is open. We can choose to be a server or a client
                    - The server that wishes to accept connections can passively open the port by entering the LISTEN state.
                     - A client can open the port by sending a SYN message and entering the SYN_SENT state

                - Depends on if we are a server or client:

                - As a server: 
                    - LISTEN: Waits for SYN request. Once received, send SYN+ACK and enter SYN_RECEIVED state
                    - SYN_RECEIVED: When an ack is received, enter ESTABLISHED state

                - As a client: 
                    - SYN_SENT: Waits for SYN+ACK from the server and sends ACK, then enters ESTABLISHED state

                Client                      SERVER
                 SYN  -------------------->
                      <--------------------  SYN+ACK
                 ACK  -------------------->     


            - Once in ESTABLISHED State:
                - send a FIN (Either one can initiate this close)
                    - FINWAIT-1: wait for ACK, enter FINWAIT-1
                    - FINWAIT-2: 
                    - FINWAIT-3: 

                - receive a FIN (Either one can receive this FIN), send an ACK, and enter CLOSED_WAIT
                    - CLOSED_WAIT: 
                    - LAST_ACK: 

    TCP Header
        - Source Port (2 bytes)
        - Destination Port (2 bytes)
        - Sequence Number (4 bytes) - Whenever you send a SYNC message, set Sequence to initial value
            - Initial Sequence Number (ISN) is randomly chosen on first SYN message. SYN bit is sent when initial sequence number is sent.
            - The next data sent will be ISN + 1. SYN bit is now off
            - Each byte sent after that will be ISN += N (N is the number of bytes sent)
        - Acknowledgment Number (4 bytes) - Whenever you send an ACK message, set acknowledgment number to initial value
            - ACK Num will be sent in every message except the original SYN message
            - 
        - Data Offset (4 bits) - the offset of where the data starts from 
        - Reseverd (3 bits) - 
        - Flags (9 bits) - NS, CWR, ECE, URG (will not be used)
                         - ACK (acknowlegment number is valid)
                         - PSH (Data is present. Push data to application)
                         - RST(Reset the connection)
                         - SYN (sequence number is ISN)
                         - FIN (final packet is sent) 
        - Window size (2 bytes) - Depth of the messages being saved so I can go back and grab one in case a message was missed
        - Checksum (2 bytes) - Extremly close UDP Checksum calculations 
            - Sum = TCP psuedo-header + TCP header + TCP data
        - Urgent Pointer (2 bytes) - Give priority to a message ("dont worry about it" - Losh)
        - Options and Padding (0 to 40 bytes) - Extra bytes that can be added
        - Data (0 or more bytes) - This will be the clear text of the webpage

        Once SYN, SYN+ACK, ACK and enter the ESTABLISHED state, the messages will turn into MQTT messages
*/
/*
                                      TO_DO LIST

    I need to initialize a socket that corresponds to the server I want to connect to
        - Destination port is their assigned port
        - Source port is my port I assign to filter the messages to the right place
        - The IP Addresses need to be set to personal and remote addresses

    Then I need to build sendTcpMessage to send the message I want to send
        - SYN -> wait for SYN_ACK
        - FIN -> wait for ACK
        - ACK -> waitTime then go to "TCP_CLOSED"
        - FIN ACK -> go to "LAST_ACK" then go to "TCP_CLOSED"

    I also need to build processTcpMessages to wait for messages I am expecting
        - SYN_ACK -> go to ESTABLISHED
        - FIN -> go to "CLOSE_WAIT" then send FIN_ACK
        - ACK -> go to "FIN_WAIT-2" --> wait for FIN, then send ACK

*/

// Install Mosquitto - On my mac, monitor port 1883 with my ip address to see the TCP handshake
// And then install contents PUB and SUB

// If i receive a FIN (While in ESATBLISHED):
//      - Set my state to CLOSE-WAIT and send an ACK
//      - Then, while still in CLOSE_WAIT, I send a FIN_ACK and go to LAST_ACK
//      - While in LAST_ACK, wait for an ACK, then set state to CLOSED and delete socket
//

// If I am sending the FIN (while in ESTABLISHED):
//      - Set state to FIN_WAIT_1, and send a FIN_ACK
//      - While in FIN_WAIT_1, wait for an ACK and then set state to FIN_WAIT_2
//      - While in FIN_WAIT_2, wait for a FIN_ACK, then set state to TIME_WAIT
//      - While in TIME_WAIT, send an ACK back and set state to CLOSED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arp.h"
#include "tcp.h"
#include "timer.h"
#include "uart1.h"
#include "mqtt.h"
#include "uart0.h"
#include "wait.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

#define MAX_TCP_PORTS 1

uint16_t tcpPorts[MAX_TCP_PORTS] = {0};
uint8_t tcpPortCount = 0;
uint8_t tcpState[MAX_TCP_PORTS] = {0};
static uint8_t currentTcpIndex = 0;
bool TcpSocketNeeded = false;

uint16_t personalPortNum;

uint32_t randSEQ = 0;

bool isTcpEnable = false;
bool initiateFIN = false;

bool SynSent = true;

uint8_t ArpReqSent = 0;
uint8_t ConnectSent = 0;
uint8_t SubSent = 0;
uint8_t PingReqSent = 0;


static socket *TcpSocket = NULL;
uint8_t localIpAddress[4];

bool variablesSet = false;
uint8_t statePlaceHolder;
int indexPlaceHolder = -1;
uint8_t retransmitCount = 0;

uint32_t theirSeq;
uint32_t theirAck;

uint8_t PingReqCount = 0;

char uart0Message[40] = {0};

//Boolean variables used to send PendingMessages
bool MqttConnectNeeded = 0;
bool MqttDisconnectNeeded = 0;
bool MqttPublishNeeded = 0;
bool MqttPingReqNeeded = 0;
bool MqttSubscribeNeeded = 0;
bool MqttUnsubscribeNeeded = 0;

//Boolean Variables used to confirm state
bool mqttConnected = 0;
bool mqttPublishReceived = 0;
bool mqttSubscribed = 0;
bool mqttPingResponseReceived = 0;

bool newSubscribe = true;

char mqttClientId[] = "knock";
uint16_t mqttPacketId = 1;
uint8_t mqttTxBuffer[256] = {0};

// Buffer to store topic and data I am going to send
char mqttTopicBuffer[64] = {0};
char mqttDataBuffer[128] = {0};

//Buffer to store topic and data I receive
char mqttRxTopicBuffer[30] = {0};
char mqttRxDataBuffer[20] = {0};

bool MqttMacNeeded = false;
bool MqttIpSet = false;

mqttStateType mqttState = MQTT_DISCONNECTED;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// In final states of FIN state machines (TCP_FIN_WAIT_2 and TCP_LAST_ACK), I set the tcpSocket->state to 0 and set tcpSocket = NULL
// I also wrote a function called "deleteTimers" so I can delete the timers i used so i can re-make them if i set a new MQTT

uint16_t randomClampedValue(void) // Randomized value for the port
{
    return 12000 + rand() % (62000 - 12000 + 1);
}

void printHex32(uint32_t value)
{
    const char hex[] = "0123456789ABCDEF"; // Stores the values as characters aligning their number with the index
    int i;

    putsUart0("0x");

    for (i = 28; i >= 0; i -= 4)
    {
        putcUart0(hex[(value >> i) & 0xF]); // Shifts the index by the value and prints the character
    }
}

void printHex16(uint16_t value)
{
    const char hex[] = "0123456789ABCDEF";
    int i;

    putsUart0("0x");

    for (i = 12; i >= 0; i -= 4)
    {
        putcUart0(hex[(value >> i) & 0xF]);
    }
}

void printHex8(uint8_t value)
{
    const char hex[] = "0123456789ABCDEF";
    int i;

    putsUart0("0x");

    for (i = 4; i >= 0; i -= 4)
    {
        putcUart0(hex[(value >> i) & 0xF]);
    }
}

//Callback Timer Functions
void callbackMsgNotSent()
{
    variablesSet = false;

    if (indexPlaceHolder < 0 || indexPlaceHolder >= MAX_TCP_PORTS)
        return;

    retransmitCount++;

    if (retransmitCount >= 8)
    {
        switch(statePlaceHolder)
        {
            case TCP_SYN_SENT:
                putsUart0("\nSYN failed\n");
                break;

            case TCP_FIN_WAIT_1:
                putsUart0("\nFIN failed\n");
                break;

            case TCP_LAST_ACK:
                putsUart0("\nFIN-ACK failed\n");
                break;

            default:
                break;
        }
        retransmitCount = 0;
        tcpState[indexPlaceHolder] = TCP_CLOSED;
        tcpPorts[indexPlaceHolder] = 0;
        initiateFIN = false;
        SynSent = true;
        indexPlaceHolder = -1;
        return;
    }

    switch(statePlaceHolder)
    {
        case TCP_SYN_SENT:
            TcpSocket->sequenceNumber--;
            tcpState[indexPlaceHolder] = TCP_CLOSED;
            SynSent = false;
            break;

        case TCP_FIN_WAIT_1:
            TcpSocket->sequenceNumber--;
            tcpState[indexPlaceHolder] = TCP_ESTABLISHED;
            initiateFIN = true;
            break;

        case TCP_LAST_ACK:
            TcpSocket->sequenceNumber--;
            tcpState[indexPlaceHolder] = TCP_CLOSE_WAIT;
            initiateFIN = true;
            break;

        default:
            break;
    }
}

void PingReqCallback(){
    MqttPingReqNeeded = true;
}

void ArpCallback(){
    // If this function calls while i am disconnected, that means i am trying to get the mac address of the server and they are not responding
    //     Check if ArpReqSent == 3, if it is, that means i cant get the mac address, so i must set a new server.
                // Set the MqttMacNeeded flag to false and print, "MQTT Server Not Available, re-set the mqtt ip"
    // If the count is not 3, set MqttMacNeeded to true to resend the ARP
    if(mqttState == MQTT_DISCONNECTED){
        if(ArpReqSent == 3){
            ArpReqSent = 0;
            MqttMacNeeded = false;
            putsUart0("MQTT Server Not Available, re-type the 'mqtt ip'");
        }
        else{
            ArpReqSent++;
            MqttMacNeeded = true;
        }
    }

    if(mqttState == MQTT_WAIT_CONNACK){
        if(ConnectSent == 3){
            ConnectSent = 0;
            mqttState = MQTT_DISCONNECTED;
            tcpState[0] = TCP_CLOSED;
            tcpPorts[0] = 0;
        }
        else{
            ConnectSent++;
            MqttConnectNeeded = true;
            mqttState = MQTT_DISCONNECTED;
        }
    }

    if(mqttState == MQTT_WAIT_SUBACK){
        // If a subscribe is sent and a response is not
        if(SubSent == 3){
            SubSent = 0;
            MqttSubscribeNeeded = false;
            mqttState = MQTT_CONNECTED;
        }
        else{
            SubSent++;
            newSubscribe = false;
            MqttSubscribeNeeded = true;
            mqttState = MQTT_CONNECTED;
        }
    }

    if(mqttState == MQTT_WAIT_PINGRESP){
        // If the count goes to 3, that means the server isn't listening anymore
            // Shut off the connection: mqttState = DISCONNECTED, TcpState = CLOSED, TcpPort = 0
        // If the count is less than 3,
            // Change the state to MQTT_CONNECTED and turn the MqttPingReqNeeded flag to true to send the ping again
        if(PingReqSent == 3){
            PingReqSent = 0;
            MqttPingReqNeeded = false;
            mqttState = MQTT_DISCONNECTED;
            tcpState[0] = TCP_CLOSED;
            tcpPorts[0] = 0;
        }
        else{
            PingReqSent++;
            MqttPingReqNeeded = true;
            mqttState = MQTT_CONNECTED;
        }
    }

}

// Set a flag that TCP will read,
void enableTcp(){
    isTcpEnable = true;
    MqttIpSet = true;
    TcpSocketNeeded = true;
    SynSent = false;
    retransmitCount = 0;
}

void disableTcp(){
    isTcpEnable = false;
}

 // If initiateFIN is set to true and the Port is open and in ESTABLISHED, initiate FIN state
void initClosingStateMachine(){
    initiateFIN = true;
}

void initTcpSocket(){
    int i = 0;
    uint8_t MqttServerIp[4];
    getIpTimeServerAddress(MqttServerIp);

    if(TcpSocketNeeded == true){

        TcpSocketNeeded = false;

        if(TcpSocket == NULL) TcpSocket = newSocket();

        if(TcpSocket == NULL){ //Checks if socket was available
            return;
        }

        randSEQ = rand();

        TcpSocket->state = 1;

        for(i = 0; i< 4; i ++){
            TcpSocket->remoteIpAddress[i] = MqttServerIp[i];
        }

        personalPortNum = randomClampedValue();

        TcpSocket->remotePort = 1883;
        TcpSocket->localPort = personalPortNum;

        tcpState[0] = TCP_CLOSED;
        tcpPorts[0] = 1883;

        startOneshotTimer(callbackMsgNotSent, 2);
        stopTimer(callbackMsgNotSent);
        startOneshotTimer(ArpCallback, 2);
        stopTimer(ArpCallback);
        startPeriodicTimer(PingReqCallback, 20);
        stopTimer(PingReqCallback);

        MqttMacNeeded = true;
    }
}

// Set TCP state
void setTcpState(uint8_t instance, uint8_t state)
{
    tcpState[instance] = state;
}

// Get TCP state
uint8_t getTcpState(uint8_t instance)
{
    return tcpState[instance];
}

// Right here, the topic and data payloads are set
// When i went through processTcpPayload, I checked what the message type was (which im looking for a Publish message)
// I need to compare the topic name to my published messages
// If i receive a message from a topic that i am subscribed to, then i need to send
//      a message to Zoha's MCU where she can do something with it
// Use mqttRxTopicBuffer and mqttRxDataBuffer here
// Send the appropriate command to the door MCU
//  -------------------------
// |   Messages I'm sending: |
// |    "knock_learn Alex"   |
// |    "knock_forget Alex"  |
// |       "knock_lock"      |
// |      "knock_unlock"     |
// |      "knock_status"     |
//  -------------------------
#define DOOR_ACK_CHAR           '1'     // Wait for a 1 sent back
#define DOOR_ACK_TIMEOUT_US     5000    // 5 ms per attempt
#define DOOR_ACK_MAX_ATTEMPTS   3       // 3 attempts
#define DOOR_MSG_MAX_CHARS      128     // max buffer size

bool waitForDoorAck(uint32_t timeoutUs)
{
    uint32_t elapsed = 0;
    char c;

    while (elapsed < timeoutUs)
    {
        if (kbhitUart1())
        {
            c = getcUart1();

            // Door MCU accepted the message
            if (c == DOOR_ACK_CHAR)
            {
                return true;
            }

            // Ignore line endings from "1\r\n"
            if (c == '\r' || c == '\n')
            {
                // do nothing
            }

            // Any other character is ignored here.
            // During this small ACK window, we only care about '1'.
        }

        waitMicrosecond(50);
        elapsed += 50;
    }

    return false;
}

bool sendDoorMessageBlockingAck(const char *message)
{
    uint8_t attempt;
    bool try;

    for (attempt = 0; attempt < DOOR_ACK_MAX_ATTEMPTS; attempt++)
    {
        putsUart1(message);

        if (waitForDoorAck(DOOR_ACK_TIMEOUT_US))
        {
            try = true;
        }
        else{
            try = false;
        }
    }

    return try;
}

void sendDoorMCU()
{
    char doorMessage[DOOR_MSG_MAX_CHARS];
    bool acked = false;

    if (strcmp(mqttRxTopicBuffer, "knock_learn") == 0)
    {
        strcpy(doorMessage, "d ");
        strcat(doorMessage, mqttRxDataBuffer);
        strcat(doorMessage, "\r\n");

        acked = sendDoorMessageBlockingAck(doorMessage);

        if (acked)
            putsUart0("knock_learn sent\r\n");
        else
            putsUart0("knock_learn NOT sent\r\n");
    }
    else if (strcmp(mqttRxTopicBuffer, "knock_forget") == 0)
    {
        strcpy(doorMessage, "e ");
        strcat(doorMessage, mqttRxDataBuffer);
        strcat(doorMessage, "\r\n");

        acked = sendDoorMessageBlockingAck(doorMessage);

        if (acked)
            putsUart0("knock_forget sent\r\n");
        else
            putsUart0("knock_forget NOT sent\r\n");
    }
    else if (strcmp(mqttRxTopicBuffer, "knock_lock") == 0)
    {
        acked = sendDoorMessageBlockingAck("f\r\n");

        if (acked)
            putsUart0("knock_lock sent\r\n");
        else
            putsUart0("knock_lock NOT sent\r\n");
    }
    else if (strcmp(mqttRxTopicBuffer, "knock_unlock") == 0)
    {
        acked = sendDoorMessageBlockingAck("g\r\n");

        if (acked)
            putsUart0("knock_unlock sent\r\n");
        else
            putsUart0("knock_unlock NOT sent\r\n");
    }
    else if (strcmp(mqttRxTopicBuffer, "knock_status_request") == 0)
    {
        acked = sendDoorMessageBlockingAck("h\r\n");

        if (acked)
            putsUart0("knock_status_request sent\r\n");
        else
            putsUart0("knock_status_request NOT sent\r\n");
    }
}

// Determines whether packet is TCP packet
// Must be an IP packet
bool isTcp(etherHeader* ether)
{
    uint8_t i = 0;
    uint32_t sum = 0;
    uint16_t tmp16;
    uint16_t tcpLength;
    bool ok = true;
    // Determine if the packet is for me
    // Verify it with my IP address
    // Then loop through my TcpPorts and verify it with the port numbers
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)ip->data;

    if (ip->protocol != PROTOCOL_TCP) ok = false;

    for (i = 0; i < 4; i++){
        if (ip->destIp[i] != localIpAddress[i])
            ok = false;
    }

    if (ok){
        tcpLength = ntohs(ip->length) - (ip->size * 4);

        // 32-bit sum over pseudo-header
        sumIpWords(ip->sourceIp, 8, &sum);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0x00FF) << 8;

        tmp16 = htons(tcpLength);
        sumIpWords(&tmp16, 2, &sum);

        // add tcp header and data, including checksum field
        sumIpWords(tcp, tcpLength, &sum);

        ok = (getIpChecksum(sum) == 0);
    }

    return ok;
}

bool isTcpSyn(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));

    uint16_t of = ntohs(tcp->offsetFields);
    return (of & SYN) != 0;
}

bool isTcpAck(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));

    uint16_t of = ntohs(tcp->offsetFields);
    return (of & ACK) != 0;
}

void processTcpArpResponse(etherHeader *ether){
    stopTimer(ArpCallback);
    arpPacket *arp = (arpPacket*)ether->data;
    int i = 0;

    for(i = 0; i < 6; i++){
        TcpSocket->remoteHwAddress[i] = arp->sourceAddress[i]; // MAC address for MQTT IP Address
    }
    SynSent = false;
}

void sendTcpPendingMessages(etherHeader *ether) // TCP Starts here
{
    uint16_t mqttLength;
    uint8_t i = 0;
    uint8_t MyIp[4] = {0,0,0,0};
    uint8_t MqttIp[4] = {0,0,0,0};

        if(isTcpEnable){

            if(MqttIpSet){
                MqttIpSet = false;
                initTcpSocket();
            }

            if(MqttMacNeeded){
                MqttMacNeeded = false;
                getIpAddress(MyIp);
                getIpMqttBrokerAddress(MqttIp);

                sendArpRequest(ether, MyIp, MqttIp);
                restartTimer(ArpCallback);
            }

            for(i = 0; i < MAX_TCP_PORTS; i++){

                if(tcpState[i] == TCP_CLOSED && tcpPorts[i] != 0 && !SynSent){
                    putsUart0("\n------- New TCP Connection -------\n");
                    TcpSocket->sequenceNumber = randSEQ;
                    TcpSocket->acknowledgementNumber = 0;

                    sendTcpMessage(ether, TcpSocket, SYN, 0, 0);

                    SynSent = true;
                    TcpSocket->sequenceNumber++;
                    tcpState[i] = TCP_SYN_SENT;

                    if(!variablesSet){
                        variablesSet = true;
                        statePlaceHolder = tcpState[i];
                        indexPlaceHolder = i;
                        restartTimer(callbackMsgNotSent);
                    }
                }

                // If the TcpState is in ESTABLISHED and the Port number is defined, check all the flags to send any pending message
                if (tcpState[i] == TCP_ESTABLISHED && tcpPorts[i] != 0)
                {
                    if (MqttConnectNeeded)
                    {
                        if (mqttState == MQTT_DISCONNECTED){

                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_CONNECT, 0, 0);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;
                            restartTimer(ArpCallback);

                            mqttState = MQTT_WAIT_CONNACK;
                        }
                        else
                        {
                            putsUart0("\nMQTT already connecting or connected\n");
                        }

                        MqttConnectNeeded = false;
                    }
                    else if (MqttPublishNeeded)
                    {
                        if ((mqttState == MQTT_CONNECTED) || (mqttState == MQTT_SUBSCRIBED))
                        {
                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_PUBLISH, mqttTopicBuffer, mqttDataBuffer);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;
                            putsUart0("----------Message Published----------\n");
                        }
                        else
                        {
                            putsUart0("\nMQTT not connected - publish error\n");
                        }

                        MqttPublishNeeded = false;
                    }
                    else if (MqttSubscribeNeeded)
                    {
                        if ((mqttState == MQTT_CONNECTED) || (mqttState == MQTT_SUBSCRIBED))
                        {

                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_SUBSCRIBE, mqttTopicBuffer, 0);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;
                            restartTimer(ArpCallback);


                            mqttState = MQTT_WAIT_SUBACK;
                        }
                        else
                        {
                            putsUart0("\nMQTT not connected - subscribe error\n");
                        }

                        MqttSubscribeNeeded = false;
                    }
                    else if (MqttUnsubscribeNeeded)
                    {
                        if (mqttState == MQTT_SUBSCRIBED)
                        {
                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_UNSUBSCRIBE, mqttTopicBuffer, 0);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;

                            mqttState = MQTT_CONNECTED;
                        }
                        else
                        {
                            putsUart0("\nMQTT not subscribed - unsubscribe error\n");
                        }

                        MqttUnsubscribeNeeded = false;
                    }
                    else if (MqttDisconnectNeeded)
                    {
                        if ((mqttState == MQTT_CONNECTED) || (mqttState == MQTT_SUBSCRIBED))
                        {

                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_DISCONNECT, 0, 0);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;

                            mqttState = MQTT_DISCONNECTED;
                            initiateFIN = true;
                        }
                        else
                        {
                            putsUart0("\nMQTT not connected - disconnect error\n");
                        }

                        MqttDisconnectNeeded = false;
                    }
                    else if (MqttPingReqNeeded)
                    {
                        if ((mqttState == MQTT_CONNECTED) || (mqttState == MQTT_SUBSCRIBED) || (mqttState == MQTT_PINGRESP))
                        {
                            mqttLength = buildMqttMessage(mqttTxBuffer, MQTT_PINGREQ, 0, 0);
                            sendTcpMessage(ether, TcpSocket, ACK | PSH, mqttTxBuffer, mqttLength);
                            TcpSocket->sequenceNumber += mqttLength;

                            mqttState = MQTT_WAIT_PINGRESP;
                        }
                        else
                        {
                            putsUart0("\nMQTT not connected - ping error\n");
                        }

                        MqttPingReqNeeded = false;
                    }
                    else if (initiateFIN)
                    {
                        initiateFIN = false;
                        sendTcpMessage(ether, TcpSocket, FIN | ACK, 0, 0);
                        TcpSocket->sequenceNumber++;
                        tcpState[i] = TCP_FIN_WAIT_1;

                        if (!variablesSet)
                        {
                            variablesSet = true;
                            statePlaceHolder = tcpState[i];
                            indexPlaceHolder = i;
                            restartTimer(callbackMsgNotSent);
                        }
                    }
                }

                if (tcpState[i] == TCP_CLOSE_WAIT)
                {
                    sendTcpMessage(ether, TcpSocket, FIN | ACK, 0, 0);
                    TcpSocket->sequenceNumber++;
                    tcpState[i] = TCP_LAST_ACK;

                    mqttState = MQTT_DISCONNECTED;
                    stopTimer(PingReqCallback);

                    if (!variablesSet)
                    {
                        variablesSet = true;
                        statePlaceHolder = tcpState[i];
                        indexPlaceHolder = i;
                        restartTimer(callbackMsgNotSent);
                    }
                }
            }
        }
}

static bool tcpSeqLess(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b) < 0);
}

static bool tcpSeqGreater(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b) > 0);
}

void processTcpResponse(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));

    uint16_t ipHeaderLength;
    uint16_t tcpHeaderLength;
    uint16_t payloadLength;
    uint16_t flags;

    ipHeaderLength = ip->size * 4;

    flags = ntohs(tcp->offsetFields);
    tcpHeaderLength = ((flags >> 12) & 0xF) * 4;

    payloadLength = ntohs(ip->length) - ipHeaderLength - tcpHeaderLength;

    theirSeq = ntohl(tcp->sequenceNumber);
    theirAck = ntohl(tcp->acknowledgementNumber);
    uint32_t expectedSeq = TcpSocket->acknowledgementNumber;

    switch(tcpState[currentTcpIndex]){

        case TCP_SYN_SENT:

            if(flags & RST){
                tcpState[currentTcpIndex] = TCP_CLOSED;
                tcpPorts[currentTcpIndex] = 0;
                break;
            }

            if(isTcpSyn(ether) & isTcpAck(ether)){

                TcpSocket->acknowledgementNumber = ntohl(tcp->sequenceNumber);

                if(ntohl(tcp->acknowledgementNumber) == TcpSocket->sequenceNumber){
                    stopTimer(callbackMsgNotSent);
                    TcpSocket->acknowledgementNumber = ntohl(tcp->sequenceNumber)+1;
                    sendTcpMessage(ether, TcpSocket, ACK, 0, 0);
                    tcpState[currentTcpIndex] = TCP_ESTABLISHED;
                }
            }
        break;

        case TCP_ESTABLISHED:
        {

            if (flags & RST)
            {
                tcpState[currentTcpIndex] = TCP_CLOSED;
                tcpPorts[currentTcpIndex] = 0;
                break;
            }

            if(theirSeq != expectedSeq){

                putsUart0("\r\nTCP Message Received DEBGUG\r\n");

                putsUart0("payloadLength = ");
                printHex16(payloadLength);
                putsUart0("\r\n");

                putsUart0("Flags = ");
                printHex16(flags);
                putsUart0("\r\n");

                putsUart0("TheirSeq = ");
                printHex32(theirSeq);
                putsUart0("\r\n");

                putsUart0("TheirAck = ");
                printHex32(theirAck);
                putsUart0("\r\n");

                putsUart0("SocketSeq = ");
                printHex32(TcpSocket->sequenceNumber);
                putsUart0("\r\n");

                putsUart0("ExpectedSeq = ");
                printHex32(TcpSocket->acknowledgementNumber);
                putsUart0("\r\n");

                putsUart0("Data = ");
                if (payloadLength > 0)
                    printHex8(tcp->data[0]);
                else
                    printHex8(0);

                putsUart0(" ");

                if (payloadLength > 1)
                    printHex8(tcp->data[1]);
                else
                    printHex8(0);

                putsUart0("\r\n");
            }

            if ((payloadLength > 0) || (flags & FIN))
            {
                // If i receive a packet with sequence values less than or greater than the
                //  current sequence i am expecting, i send the server a blank ACK based on my current
                //  sequence and acknowledgment values

                if (tcpSeqLess(theirSeq, expectedSeq))
                {
                    putsUart0("OLD DUPLICATE TCP SEGMENT IGNORED\r\n");

                    sendTcpMessage(ether, TcpSocket, ACK, 0, 0);
                    break;
                }

                if (tcpSeqGreater(theirSeq, expectedSeq))
                {
                    putsUart0("FUTURE TCP SEGMENT IGNORED\r\n");

                    sendTcpMessage(ether, TcpSocket, ACK, 0, 0);
                    break;
                }

                TcpSocket->acknowledgementNumber = theirSeq + payloadLength;

                if (flags & FIN)
                {
                    TcpSocket->acknowledgementNumber++;
                }

                sendTcpMessage(ether, TcpSocket, ACK, 0, 0);

                if (flags & FIN)
                {
                    tcpState[currentTcpIndex] = TCP_CLOSE_WAIT;
                }
                else
                {
                    processTcpPayload(tcp->data, payloadLength);
                }

                if (mqttPublishReceived)
                {
                    mqttPublishReceived = false;
                    sendDoorMCU();
                }
            }

            break;
        }

        case TCP_FIN_WAIT_1:
            if(flags & ACK){
                if(ntohl(tcp->acknowledgementNumber) == TcpSocket->sequenceNumber){
                    stopTimer(callbackMsgNotSent);
                    tcpState[currentTcpIndex] = TCP_FIN_WAIT_2;
                }
            }
            break;

        case TCP_FIN_WAIT_2: // Final state if I initiate close
            if(flags & FIN){
                //if((ntohl(tcp->acknowledgementNumber) == TcpSocket->sequenceNumber) && (ntohl(tcp->sequenceNumber) == TcpSocket->acknowledgementNumber)){
                    TcpSocket->acknowledgementNumber++;
                    sendTcpMessage(ether, TcpSocket, ACK, 0, 0);
                    tcpState[currentTcpIndex] = TCP_CLOSED;
                    tcpPorts[currentTcpIndex] = 0;
                    TcpSocket->state = 0;
                    TcpSocket = NULL;
                    deleteTimer(callbackMsgNotSent);
                    deleteTimer(ArpCallback);
                    deleteTimer(PingReqCallback);
                    disableTcp();
               // }
            }

        case TCP_LAST_ACK: // Final state if they initiate close
            if(flags & ACK){
                if(ntohl(tcp->acknowledgementNumber) == TcpSocket->sequenceNumber){
                    stopTimer(callbackMsgNotSent);
                    tcpState[currentTcpIndex] = TCP_CLOSED;
                    tcpPorts[currentTcpIndex] = 0;
                    TcpSocket->state = 0;
                    TcpSocket = NULL;
                    deleteTimer(callbackMsgNotSent);
                    deleteTimer(ArpCallback);
                    deleteTimer(PingReqCallback);
                    disableTcp();
                }
            }
    }
}


void processTcpPayload(uint8_t *data, uint16_t length)
{
    uint8_t packetType;

    if (length < 2)
        return;

    packetType = (data[0] >> 4) & 0x0F;

    switch (packetType)
    {
        case MQTT_CONNACK: //Change packet type defines
            if (length >= 4)
            {
                if (data[3] == 0x00){
                    if(mqttState == MQTT_WAIT_CONNACK){
                        putsUart0("CONNACK Message Read\n");
                        mqttState = MQTT_CONNECTED;
                        stopTimer(ArpCallback);
                        restartTimer(PingReqCallback);
                    }
                }
            }
            break;

        case MQTT_PUBLISH:
        {
            uint32_t remainingLength = 0;
            uint32_t multiplier = 1;
            uint8_t remainingLengthBytes = 0;
            uint8_t encodedByte = 0;
            uint16_t fixedHeaderLength;
            uint16_t packetEnd;
            uint16_t topicIndex;
            uint16_t topicLength;
            uint16_t payloadStart;
            uint16_t payloadLength;
            uint16_t searchIndex;
            uint16_t adjustedTopicStart;
            uint16_t adjustedTopicLength;
            uint16_t topicCopyLength;
            bool valid = true;

            if (length < 2)
                break;

            // Decode Remaining Length
            do
            {
                if ((1 + remainingLengthBytes) >= length)
                {
                    valid = false;
                    break;
                }

                encodedByte = data[1 + remainingLengthBytes];
                remainingLength += (uint32_t)(encodedByte & 0x7F) * multiplier;

                if (multiplier > (128UL * 128UL * 128UL))
                {
                    valid = false;
                    break;
                }

                multiplier *= 128;
                remainingLengthBytes++;

                if (remainingLengthBytes > 4)
                {
                    valid = false;
                    break;
                }

            } while (encodedByte & 0x80);

            if (!valid)
                break;

            fixedHeaderLength = 1 + remainingLengthBytes;

            if ((fixedHeaderLength + remainingLength) > length)
                break;

            packetEnd = fixedHeaderLength + (uint16_t)remainingLength;

            // First 2 bytes of variable header = topic length
            topicIndex = fixedHeaderLength;

            if ((topicIndex + 2) > packetEnd)
                break;

            topicLength = ((uint16_t)data[topicIndex] << 8) | data[topicIndex + 1];
            topicIndex += 2;

            if ((topicIndex + topicLength) > packetEnd)
                break;

            // Workaround: search for first 'k' inside topic bytes
            adjustedTopicStart = topicIndex;

            for (searchIndex = topicIndex; searchIndex < (topicIndex + topicLength); searchIndex++)
            {
                if (data[searchIndex] == 'k')
                {
                    adjustedTopicStart = searchIndex;
                    break;
                }
            }

            adjustedTopicLength = (topicIndex + topicLength) - adjustedTopicStart;

            memset(mqttRxTopicBuffer, 0, sizeof(mqttRxTopicBuffer));

            topicCopyLength = adjustedTopicLength;
            if (topicCopyLength >= sizeof(mqttRxTopicBuffer))
                topicCopyLength = sizeof(mqttRxTopicBuffer) - 1;

            memcpy(mqttRxTopicBuffer, &data[adjustedTopicStart], topicCopyLength);
            mqttRxTopicBuffer[topicCopyLength] = '\0';

            // QoS 0 payload starts right after original topic field
            payloadStart = topicIndex + topicLength;

            if (payloadStart > packetEnd)
                break;

            payloadLength = packetEnd - payloadStart;

            memset(mqttRxDataBuffer, 0, sizeof(mqttRxDataBuffer));

            if (payloadLength >= sizeof(mqttRxDataBuffer))
                payloadLength = sizeof(mqttRxDataBuffer) - 1;

            memcpy(mqttRxDataBuffer, &data[payloadStart], payloadLength);
            mqttRxDataBuffer[payloadLength] = '\0';

            if (mqttState == MQTT_CONNECTED || mqttState == MQTT_SUBSCRIBED)
                mqttPublishReceived = true;

            break;
        }

        case MQTT_SUBACK:
            if (length >= 5)
            {
                if (data[4] != 0x80)
                {
                    if(mqttState == MQTT_WAIT_SUBACK){
                        putsUart0("SUBACK received\n");
                        stopTimer(ArpCallback);
                        mqttState = MQTT_SUBSCRIBED;
                    }
                }else{
                    putsUart0("not SUBACK\n");
                }
            }
            break;

        case MQTT_PINGRESP:
            if(mqttState == MQTT_WAIT_PINGRESP){
                putsUart0("Ping Response Read\n");
                mqttState = MQTT_CONNECTED;
            }
            break;

        default:
            break;
    }
}

void setTcpPortList(uint16_t ports[], uint8_t count)
{

}

bool isTcpPortOpen(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));
    uint8_t i = 0;

    if(ntohs(tcp->destPort) != personalPortNum) return false;

    for(i = 0; i < MAX_TCP_PORTS; i++){
        if(ntohs(tcp->sourcePort) == tcpPorts[i]){
            currentTcpIndex = i;
            return true;
        }
    }

    return false;

}

void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags)
{

}

// Send TCP message
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{

    uint32_t i = 0;
    uint32_t sum = 0;
    uint16_t tmp16;
    uint16_t tcpLength;
    uint8_t ipHeaderLength;
    uint8_t localHwAddress[6];

    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);

    for(i = 0; i < HW_ADD_LENGTH; i++){
        ether->destAddress[i] = s->remoteHwAddress[i];
        ether->sourceAddress[i] = localHwAddress[i];
    }

    ether->frameType = htons(TYPE_IP);

    //IP Header

    ipHeader* ip = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
       ip->destIp[i] = s->remoteIpAddress[i]; //Destination address is filled in from socket information
       ip->sourceIp[i] = localIpAddress[i]; // Source IP address is filled in from variable that grabbed it from EEPROM
    }

    ipHeaderLength = ip->size * 4; // 5 * 4 = 20 bytes

    //TCP Header

    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4)); // TCP address is set from the beginning of the IP address + 20 bytes

    tcp->sourcePort = htons(s->localPort); // Source port is filled in from the socket information
    tcp->destPort = htons(s->remotePort); // Destination port is filled in from socket information

    tcp->sequenceNumber = htonl(s->sequenceNumber); // Sequence number is grabbed from socket information

    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber); // Acknowledgment number is grabbed from socket information

    tcp->offsetFields = htons((5 << 12) | flags); // sets top 4 bits to '5' and bottom 6 bits to the flags being set

    tcp->windowSize = (htons(1460));
    tcp->checksum = 0;
    tcp->urgentPointer = 0;

    for(i = 0; i < dataSize; i++){
        tcp->data[i] = data[i];
    }

    tcpLength = sizeof(tcpHeader) + dataSize; // The length is the size of the header plus the size of the data
    ip->length = htons(ipHeaderLength + tcpLength);
    calcIpChecksum(ip); //Calculate and fill in ip header checksum

    uint16_t tcpLength_tmp = htons(tcpLength);

    sumIpWords(ip->sourceIp, 8, &sum);   // source IP + dest IP
    tmp16 = ip->protocol;
    sum += (tmp16 & 0x00FF) << 8;        // 0x00 + protocol
    sumIpWords(&tcpLength_tmp, 2, &sum);   // TCP length

    tcp->checksum = 0;
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + tcpLength);

}

uint16_t buildMqttMessage(uint8_t buffer[], mqttMessageType msgType, char strTopic[], char strData[]){

    uint16_t i = 0;
    uint16_t topicLen;
    uint16_t dataLen;
    uint16_t clientIdLen;
    uint16_t remainingLength;

    switch (msgType){

        case MQTT_CONNECT:

            clientIdLen = strlen(mqttClientId);
            remainingLength = 10 + 2 + clientIdLen;

            buffer[i++] = 0x10; // CONNECT
            buffer[i++] = remainingLength; // Remaining Length

            buffer[i++] = 0x00;
            buffer[i++] = 0x04;
            buffer[i++] = 'M';
            buffer[i++] = 'Q';
            buffer[i++] = 'T';
            buffer[i++] = 'T';

            buffer[i++] = 0x04;
            buffer[i++] = 0x02;
            buffer[i++] = 0x00;
            buffer[i++] = 0x3C;

            buffer[i++] = (clientIdLen >> 8) & 0xFF;
            buffer[i++] = clientIdLen & 0xFF;

            memcpy(&buffer[i], mqttClientId, clientIdLen);
            i += clientIdLen;

            break;

        case MQTT_DISCONNECT:

            buffer[i++] = 0xE0;
            buffer[i++] = 0x00;

            break;

        case MQTT_PUBLISH:

            topicLen = strlen(strTopic);
            dataLen = strlen(strData);
            remainingLength = 2 + topicLen + dataLen;

            buffer[i++] = 0x30;
            buffer[i++] = remainingLength;

            buffer[i++] = (topicLen >> 8) & 0xFF;
            buffer[i++] = topicLen & 0xFF;

            memcpy(&buffer[i], strTopic, topicLen);
            i += topicLen;

            memcpy(&buffer[i], strData, dataLen);
            i += dataLen;

            break;

        case MQTT_SUBSCRIBE:

            topicLen = strlen(strTopic);
            remainingLength = 2 + 2 + topicLen + 1;

            buffer[i++] = 0x82;
            buffer[i++] = remainingLength;

            buffer[i++] = (mqttPacketId >> 8) & 0xFF;
            buffer[i++] = mqttPacketId & 0xFF;

            buffer[i++] = (topicLen >> 8) & 0xFF;
            buffer[i++] = topicLen & 0xFF;

            memcpy(&buffer[i], strTopic, topicLen);
            i += topicLen;

            buffer[i++] = 0x00;

            if(newSubscribe){
                mqttPacketId++;
            }

            newSubscribe = true;

            break;

        case MQTT_UNSUBSCRIBE:

            topicLen = strlen(strTopic);
            remainingLength = 2 + 2 + topicLen;

            buffer[i++] = 0xA2;
            buffer[i++] = remainingLength;

            buffer[i++] = (mqttPacketId >> 8) & 0xFF;
            buffer[i++] = mqttPacketId & 0xFF;

            buffer[i++] = (topicLen >> 8) & 0xFF;
            buffer[i++] = topicLen & 0xFF;

            memcpy(&buffer[i], strTopic, topicLen);
            i += topicLen;

            mqttPacketId++;

            break;

        case MQTT_PINGREQ:

            buffer[i++] = 0xC0;
            buffer[i++] = 0x00;

            break;

        default:
            return 0;
    }

    return i;
}

// Process incoming UART1 messages
//  ------------------------------
// |    Messages sent to me:      |
// |  "knock_recognized Alex"     |
// |    "knock_unrecognized"      |
// | "knock_door_status locked"   |
// | "knock_door_status unlocked" |
//  ------------------------------

void processUart1()
{
    // Don't process any data until i am in CONNECTED or SUBSCRIBED state
    if (mqttState != MQTT_CONNECTED && mqttState != MQTT_SUBSCRIBED)
    {
        return;
    }

    static char strInput[127 + 1];
    static uint8_t count = 0;
    char c;

    while (kbhitUart1())
    {
        c = getcUart1();

        // message ends on newline, carriage return, or full buffer
        bool end = (c == '\n') || (c == '\r') || (count == 127);

        if (!end)
        {
            strInput[count++] = c;
        }
        else
        {
            strInput[count] = '\0';
            count = 0;

            if (strncmp(strInput, "a ", 2) == 0)
            {
                char *name = &strInput[2];
                if (name[0] != '\0')
                    publishMqtt("knock_recognized", name);
            }
            else if (strcmp(strInput, "b") == 0)
            {
                publishMqtt("knock_unrecognized", "Door alert");
            }
            else if (strncmp(strInput, "c ", 2) == 0)
            {
                char *status = &strInput[2];
                if (strcmp(status, "locked") == 0 || strcmp(status, "unlocked") == 0)
                    publishMqtt("knock_door_status", status);
            }
        }
    }
}
