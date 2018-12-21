#include <stdio.h>
#include <string.h>
#include "streamFraming.h"
#include "crc.h"

#define THERMIT_MAX_PAYLOAD_LENGTH  112







/*
Where packet-aware transfer mechanism is not used, the 
start/stop bytes are used to recognize the frame:
[ A5 | A5 | thermit frame | 5A | 5A ]

thermit frame:
[ FCode | RecFileID | RecFeedback | SendFileID | SendChunkNo | PayloadLength | Payload | CRC (16bit) ]

The start/stop bytes are dropped and only the thermit 
frame is given to the protocol.
*/

void streamFramingFollow(streamFraming_t *frame, uint8_t inByte)
{
  bool error = true;

  printf("rec: %02X\r\n", inByte);


  switch (frame->state)
  {
  case MSG_STATE_START:
    if (inByte == START_CHAR)
    {
      if (--(frame->stateRoundsLeft) == 0)
      {
        /*last round -> advance to next state*/
        frame->state = MSG_STATE_HEADER;
        frame->stateRoundsLeft = 5;
      }
      error = false;
    }
    break;

  case MSG_STATE_HEADER:
    frame->buf[frame->len++] = inByte;
    if (--(frame->stateRoundsLeft) == 0)
    {
      /*advance to next state*/
      frame->state = MSG_STATE_LEN;
      frame->stateRoundsLeft = 1;
    }
    
    error = false;
    break;

  case MSG_STATE_LEN:
    frame->buf[frame->len++] = inByte;
    if (--(frame->stateRoundsLeft) == 0)
    {
      if(inByte < THERMIT_MAX_PAYLOAD_LENGTH)
      {
        /*advance to next state*/
        frame->state = MSG_STATE_PAYLOAD_AND_CRC;
        frame->stateRoundsLeft = inByte + 2;  /*payload+crc*/
      }
    }
    
    error = false;
    break;

  case MSG_STATE_PAYLOAD_AND_CRC:
    /*note: we don't check CRC here. The protocol handler will check it anyway.*/
    frame->buf[frame->len++] = inByte;
    if (--(frame->stateRoundsLeft) == 0)
    {
      /*advance to next state*/
      frame->state = MSG_STATE_STOP;
      frame->stateRoundsLeft = 2;
    }
    
    error = false;
    break;

  case MSG_STATE_STOP:
    if (inByte == STOP_CHAR)
    {
      if (--(frame->stateRoundsLeft) == 0)
      {
        /*last round -> advance to next state*/
        frame->state = MSG_STATE_FINISHED;
        /*no stateRoundsLeft needed for this state*/

        /*mark frame ready to be sent to ControlTask*/
        frame->isReady = true;
      }

      error = false;
    }
    break;

  case MSG_STATE_FINISHED:
    /*idle... we shouldn't get here at anytime, but it wouldn't be an error.
      We just drop all the bytes.*/
    error = false;
    break;

  default:
    /*out of sync*/
    break;
  }

  if (error)
  {
    /*restart listening from start*/
    streamFramingInitialize(frame);
  }
}

void streamFramingInitialize(streamFraming_t *frame)
{
  memset(frame, 0, sizeof(streamFraming_t));

  frame->state = MSG_STATE_START;
  frame->stateRoundsLeft = 2;
}
