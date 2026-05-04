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

#ifndef TCP_H_
#define TCP_H_

#include <stdint.h>
#include <stdbool.h>
#include "ip.h"
#include "socket.h"

typedef struct _tcpHeader // 20 or more bytes
{
  uint16_t sourcePort;
  uint16_t destPort;
  uint32_t sequenceNumber;
  uint32_t acknowledgementNumber;
  uint16_t offsetFields;
  uint16_t windowSize;
  uint16_t checksum;
  uint16_t urgentPointer;
  uint8_t  data[0];
} tcpHeader;

// TCP states
#define TCP_CLOSED 0
#define TCP_LISTEN 1
#define TCP_SYN_RECEIVED 2
#define TCP_SYN_SENT 3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT_1 5
#define TCP_FIN_WAIT_2 6
#define TCP_CLOSING 7
#define TCP_CLOSE_WAIT 8
#define TCP_LAST_ACK 9
#define TCP_TIME_WAIT 10

// TCP offset/flags
#define FIN 0x0001
#define SYN 0x0002
#define RST 0x0004
#define PSH 0x0008
#define ACK 0x0010
#define URG 0x0020
#define ECE 0x0040
#define CWR 0x0080
#define NS  0x0100
#define OFS_SHIFT 12

extern bool MqttConnectNeeded;
extern bool MqttDisconnectNeeded;

extern bool MqttPublishNeeded;

extern bool MqttSubscribeNeeded;
extern bool MqttUnsubscribeNeeded;

extern char mqttTopicBuffer[64];
extern char mqttDataBuffer[128];

// Message Types
typedef enum
{
    FILLER,
    MQTT_CONNECT,
    MQTT_CONNACK,
    MQTT_PUBLISH,
    MQTT_PUBACK,
    MQTT_PUBREC,
    MQTT_PUBREL,
    MQTT_PUBCOMP,
    MQTT_SUBSCRIBE,
    MQTT_SUBACK,
    MQTT_UNSUBSCRIBE,
    MQTT_UNSUBACK,
    MQTT_PINGREQ,
    MQTT_PINGRESP,
    MQTT_DISCONNECT
} mqttMessageType;

// MATT States
typedef enum
{
    MQTT_DISCONNECTED,
    MQTT_WAIT_CONNACK,
    MQTT_CONNECTED,
    MQTT_WAIT_SUBACK,
    MQTT_SUBSCRIBED,
    MQTT_WAIT_PINGRESP,
} mqttStateType;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

uint16_t randomClampedValue(void);
void printHex32(uint32_t value);
void printHex16(uint16_t value);
void printHex8(uint8_t value);

void callbackMsgNotSent();
void PingReqCallback();
void ArpCallback();
void enableTcp();
void disableTcp();
void initClosingStateMachine();
void initTcpSocket();
void setTcpState(uint8_t instance, uint8_t state);
uint8_t getTcpState(uint8_t instance);

bool waitForDoorAck(uint32_t timeoutUs);
bool sendDoorMessageBlockingAck(const char *message);
void sendDoorMCU();

bool isTcp(etherHeader *ether);
bool isTcpSyn(etherHeader *ether);
bool isTcpAck(etherHeader *ether);

void processTcpArpResponse();
void sendTcpPendingMessages(etherHeader *ether);
void processTcpResponse(etherHeader *ether);
void processTcpPayload(uint8_t *data, uint16_t length);

void setTcpPortList(uint16_t ports[], uint8_t count);
bool isTcpPortOpen(etherHeader *ether);
void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags);
void sendTcpMessage(etherHeader *ether, socket* s, uint16_t flags, uint8_t data[], uint16_t dataSize);
uint16_t buildMqttMessage(uint8_t buffer[], mqttMessageType msgType, char strTopic[], char strData[]);

// UART1 message parser
void processUart1();

#endif

