#include <stdio.h>
#include <string.h>
#include "streamFraming.h"
#include "crc.h"

#define THERMIT_MAX_PAYLOAD_LENGTH  112







/*
Where packet-aware transfer mechanism is not used, the 
start/stop bytes are used to recognize the frame:
| A5 | A5 | thermit frame | 5A | 5A |
The start/stop bytes are dropped and only the thermit 
frame is given to the protocol.
*/

void streamFramingFollow(streamFraming_t *frame, uint8_t inByte)
{
  bool error = true;

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

      /*ok!*/
      error = false;
    }
    break;

  case MSG_STATE_LEN:
    frame->buf[frame->len++] = inByte;
    if (--(frame->stateRoundsLeft) == 0)
    {
      if(inByte < THERMIT_MAX_PAYLOAD_LENGTH)
      {
        /*advance to next state*/
        frame->state = MSG_STATE_PAYLOAD;
        frame->stateRoundsLeft = inByte;

        /*ok!*/
        error = false;
      }
    }
    break;

  case MSG_STATE_PAYLOAD:
    frame->buf[frame->len++] = inByte;
    if (--(frame->stateRoundsLeft) == 0)
    {
      /*advance to next state*/
      frame->state = MSG_STATE_CRC;
      frame->stateRoundsLeft = 2;

      /*ok!*/
      error = false;
    }
    break;


  case MSG_STATE_CRC:
    /*store the incoming CRC in two parts.
      On first byte, the crcReceived==0, so shifting does not matter. We just or the low part.
      On second byte, we shift left and OR with low part.*/
    frame->crcReceived <<= 8;
    frame->crcReceived |= inByte;

    /*let's assume the receiving will be OK*/
    error = false;

    if (--(frame->stateRoundsLeft) == 0)
    {
      uint16_t crcValue = crc16(frame->buf, frame->len);

      if (frame->crcReceived == crcValue)
      {
        /*Checksum OK!*/
        /*last round -> advance to next state*/
        frame->state = MSG_STATE_STOP;
        frame->stateRoundsLeft = 2;
      }
      else
      {
        /*CRC failed - drop frame*/
        error = true;

        printf("CRC FAIL\r\n");
      }
    }
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
