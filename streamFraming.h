#ifndef __STREAMFRAMING_H__
#define __STREAMFRAMING_H__

#include <stdbool.h>
#include <stdint.h>


#define START_CHAR 0xA5
#define STOP_CHAR 0x5A

typedef enum
{
  /*stop char for stream based connections, such as uart*/
  MSG_STATE_START = 0,

  /*thermit*/
  MSG_STATE_HEADER,
  MSG_STATE_LEN,
  MSG_STATE_PAYLOAD,
  MSG_STATE_CRC,

  /*stop char for stream based connections, such as uart*/
  MSG_STATE_STOP,
  MSG_STATE_FINISHED
} streamFramingState_t;


typedef struct
{
  uint8_t *buf;
  uint16_t len;

  streamFramingState_t state;
  uint8_t stateRoundsLeft;
  uint16_t crcReceived;
  bool isReady;
  uint16_t idleCounter;
} streamFraming_t;



void streamFramingFollow(streamFraming_t *frame, uint8_t inByte);
void streamFramingInitialize(streamFraming_t *frame);




#endif  //__STREAMFRAMING_H__