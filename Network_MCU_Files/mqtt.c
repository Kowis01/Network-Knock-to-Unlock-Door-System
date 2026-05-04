// MQTT Library (framework only)
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
#include <string.h>
#include "mqtt.h"
#include "timer.h"
#include "tcp.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

/*
 * Once in established state of TCP, we will send data in the payload and set the PUSH bit. The content of the data will match one of these messages
 * We will be coding the client: we will publish information for anyone to subscribe to
 * PORT 1883
 * MQTT Packet:
 *    - Control Header (1 Byte)
 *      - Packet Type (4 bits)
 *      - Flags (4 bits)
 *    - Packet remaining length (1 - 4 bytes) ==>
 *    - Variable Length Header ()
 *    - Variable Length Payload
 *
 * Packet Types:
 *  CONNECT      1 Client -> Server
 *  CONNACK      2 Server -> client
 *  PUBLISH      3 either
 *  PUBACK       4 server -> client
 *  PUBREC       5 Client -> Server
 *  PUBREL       6 Client -> Server
 *  PUBCOMP      7 Client -> Server
 *
 *  SUBSCRIBE    8
 *  SUBACK       9
 *  UNSUBSCRIBE 10
 *  UNSUBACK    11
 *  PINGREQ     12
 *  PINGRESP    13
 *  DISCONNECT  14
 *
 * I will send MQTT connect, the broker team should respond with a CONNACK
 * After that, i am connected and i need to send a subscribe and get a SUBACK back
 *       or send a publish and get a PUBACK back
 *
 *
 *
 */

void connectMqtt()
{
    MqttConnectNeeded = true;
}

void disconnectMqtt()
{
    MqttDisconnectNeeded = true;
}

void publishMqtt(char strTopic[], char strData[])
{
    strcpy(mqttTopicBuffer, strTopic);
    strcpy(mqttDataBuffer, strData);
    MqttPublishNeeded = true;
}

void subscribeMqtt(char strTopic[])
{
    strcpy(mqttTopicBuffer, strTopic);
    MqttSubscribeNeeded = true;
}

void unsubscribeMqtt(char strTopic[])
{
    strcpy(mqttTopicBuffer, strTopic);
    MqttUnsubscribeNeeded = true;
}
