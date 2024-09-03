#include "parser.h"
#include "endian.h"

#include <arpa/inet.h>
#include <cstddef>
#include <string.h>
#include <stdexcept>
#include <sys/types.h>
#include <iostream>
#include <iomanip>


#ifndef USE_GENERIC_FORMAT
static 
ssize_t parseCANFrame(canfd_frame* frame, const uint8_t* rawData, const uint8_t* rawDataEnd) {
  using namespace cannelloni;
  const uint8_t* rawDataOrig = rawData;
  canid_t tmp;
  memcpy(&tmp, rawData, sizeof (canid_t));
  frame->can_id = ntohl(tmp);
  /* += 4 */
  rawData += sizeof (canid_t);
  frame->len = *rawData;
  /* += 1 */
  rawData += sizeof (frame->len);
  /* If this is a CAN FD frame, also retrieve the flags */
  if (frame->len & CANFD_FRAME)
  {
      frame->flags = *rawData;
      /* += 1 */
      rawData += sizeof (frame->flags);
  }
  /* RTR Frames have no data section although they have a dlc */
  if ((frame->can_id & CAN_RTR_FLAG) == 0)
  {
      /* Check again now that we know the dlc */
      if (rawData + canfd_len(frame) > rawDataEnd)
      {
          frame->len = 0;
          return -1;
      }

      memcpy(frame->data, rawData, canfd_len(frame));
      rawData += canfd_len(frame);
  }
  return rawData-rawDataOrig;
}

void parseFrames(uint16_t len, const uint8_t* buffer, std::function<canfd_frame*()> frameAllocator,
        std::function<void(canfd_frame*, bool)> frameReceiver)
{
    using namespace cannelloni;

    const struct CannelloniDataPacket* data;
    /* Check for OP Code */
    data = reinterpret_cast<const struct CannelloniDataPacket*> (buffer);
    if (data->version != CANNELLONI_FRAME_VERSION)
        throw std::runtime_error("Received wrong version");

    if (data->op_code != DATA)
        throw std::runtime_error("Received wrong OP code");

    if (ntohs(data->count) == 0)
        return; // Empty packets silently ignored

    const uint8_t* rawData = buffer + CANNELLONI_DATA_PACKET_BASE_SIZE;
    const uint8_t* bufferEnd = buffer + len;

    for (uint16_t i = 0; i < ntohs(data->count); i++)
    {
        if (rawData - buffer + CANNELLONI_FRAME_BASE_SIZE > len)
            throw std::runtime_error("Received incomplete packet");

        /* We got at least a complete canfd_frame header */
        canfd_frame* frame = frameAllocator();
        if (!frame)
            throw std::runtime_error("Allocation error.");

        ssize_t bytesParsed = parseCANFrame(frame, rawData, bufferEnd);
        rawData+=bytesParsed;
        if (bytesParsed > 0) {
            frameReceiver(frame, true);
        } else {
            frameReceiver(frame, false);
            throw std::runtime_error("Received incomplete packet / can header corrupt!");
        }
    }
}

size_t encodeFrame(uint8_t *data, canfd_frame *frame) {
    using namespace cannelloni;
    uint8_t *dataOrig = data;
    canid_t tmp = htonl(frame->can_id);
    memcpy(data, &tmp, sizeof(canid_t));
    /* += 4 */
    data += sizeof(canid_t);
    *data = frame->len;
    /* += 1 */
    data += sizeof(frame->len);
    /* If this is a CAN FD frame, also send the flags */
    if (frame->len & CANFD_FRAME) {
        *data = frame->flags;
        /* += 1 */
        data += sizeof(frame->flags);
    }
    if ((frame->can_id & CAN_RTR_FLAG) == 0) {
        memcpy(data, frame->data, canfd_len(frame));
        data += canfd_len(frame);
    }
    return data-dataOrig;
}

uint8_t* buildPacket(uint16_t len, uint8_t* packetBuffer,
        std::list<canfd_frame*>& frames, uint8_t seqNo,
        std::function<void(std::list<canfd_frame*>&, std::list<canfd_frame*>::iterator)> handleOverflow)
{
    using namespace cannelloni;

    uint16_t frameCount = 0;
    uint8_t* data = packetBuffer + CANNELLONI_DATA_PACKET_BASE_SIZE;
    for (auto it = frames.begin(); it != frames.end(); it++)
    {
        canfd_frame* frame = *it;
        /* Check for packet overflow */
        if ((data - packetBuffer + CANNELLONI_FRAME_BASE_SIZE + canfd_len(frame)
                + ((frame->len & CANFD_FRAME) ? sizeof(frame->flags) : 0))
                > len)
        {
            handleOverflow(frames, it);
            break;
        }
        data += encodeFrame(data, frame);
        frameCount++;
    }
    struct CannelloniDataPacket* dataPacket;
    dataPacket = (struct CannelloniDataPacket*) (packetBuffer);
    dataPacket->version = CANNELLONI_FRAME_VERSION;
    dataPacket->op_code = DATA;
    dataPacket->seq_no = seqNo;
    dataPacket->count = htons(frameCount);

    return data;
}

#else 

/**
 * One packages:
 *  | CAN-Frame | CAN-Frame | CAN-Frame |...
 *  
 * One frame:
 *  | Frame-Info | Frame-ID | Frame-Data|
 *  |  1 byte    | 4 bytes  | 8 bytes   |
 *
 * Frame-Info:
 *  | b7              | b6                               | b5                 | b4                 | b3 | b2 | b1 | b0 |
 *  | FF: 0-std;1-ext | RTR:0-data frame; 1-remote frame | reserved(always:0) | reserved(always:0) | Data length       |
 *
 * Frame-ID:
 *  4 bytes, standard frame effective bits 11 bits, extended frame effective bits 29 bits
 *  | Byte1 | Byte2 | Byte3 | Byte4 |
 *  | 12    | 34    | 56    | 78    |
 *
 * Frame-Data:
 *  8 bytes, fill in any less than 8 bits with 0
 *  | Byte1 | Byte2 | Byte3 | Byte4 | Byte5 | Byte6 | Byte7 | Byte8 |
 *  | 01    | 02    | 03    | 04    | 05    | 06    | 07    | 08    |
 *
 * e.g.
 * 1. std frame:
 *  ID: 0x03ff; Data Length: 5 bytes (data: 01 02 03 04 05).
 *  the can frame is:
 *  | 05 | 00 00 03 FF | 01 02 03 04 05 00 00 00 |
 * 
 * 2. extended frame:
 *  ID: 0x12345678; Data Length: 8 bytes (data: 01 02 03 04 05 06 07 08).
 *  the can frame is:
 *  | 88 | 12 34 56 78 | 01 02 03 04 05 06 07 08 |
 * 
 */

#pragma pack(1)
struct DTUEthFrame{
  union{
    uint8_t info;
    struct {
      uint8_t len:4;
      uint8_t reserved1: 1;
      uint8_t reserved2: 1;
      uint8_t RTR:1;
      uint8_t FF:1;
    };
  };

  union{
    uint32_t id;
#if 0
#if IS_LITTLE_ENDIAN
    struct {
      uint32_t stdID:11;
      uint32_t reserved3:21;
    };
    struct{
      uint32_t extID:29;
      uint32_t reserved4: 3;
    };
#else 
    struct {
      uint32_t reserved3:21;
      uint32_t stdID:11;
    };
    struct{
      uint32_t reserved4: 3;
      uint32_t extID:29;
    };
#endif
#endif
  };

  uint8_t data[0]; // Compatible with CAN and CanFD, with a minimum length of 8
};
#pragma pack(0)

static
ssize_t parseCANFrame(canfd_frame* frame, const uint8_t* rawData, const uint8_t* rawDataEnd) {
  using namespace cannelloni;

  const struct DTUEthFrame* src = (const struct DTUEthFrame*)rawData;
  
  if(rawData + sizeof(*src) + src->len > rawDataEnd){
    frame->len = 0;
    return -1;
  }
  
  frame->len = src->len;
  //frame->flags = 
  uint32_t can_id = ntohl(src->id);
  if (src->FF){
    can_id |= CAN_EFF_FLAG;
  }

  if (src->RTR){
    can_id |= CAN_RTR_FLAG;
  }
  frame->can_id = can_id;
  memcpy(frame->data, src->data, src->len);

  return sizeof(*src) + src->len;
}

void parseFrames(uint16_t len, const uint8_t* buffer, std::function<canfd_frame*()> frameAllocator,
        std::function<void(canfd_frame*, bool)> frameReceiver)
{
    using namespace cannelloni;

    const uint8_t* rawData = buffer;
    const uint8_t* bufferEnd = buffer + len;
    for(;rawData - buffer < len;){
        //if (rawData - buffer > len)
        //    throw std::runtime_error("Received incomplete packet");

        /* We got at least a complete canfd_frame header */
        canfd_frame* frame = frameAllocator();
        if (!frame)
            throw std::runtime_error("Allocation error.");

        ssize_t bytesParsed = parseCANFrame(frame, rawData, bufferEnd);
        rawData += bytesParsed;
        if (bytesParsed > 0) {
            frameReceiver(frame, true);
        } else {
            frameReceiver(frame, false);
            throw std::runtime_error("Received incomplete packet / can header corrupt!");
        }
    }
}
size_t encodeFrame(uint8_t *data, canfd_frame *frame) {
    using namespace cannelloni;
    //uint8_t *dataOrig = data;
    //canid_t tmp = htonl(frame->can_id);

    struct DTUEthFrame* dst = (struct DTUEthFrame*)data;
    uint8_t len = canfd_len(frame);

    dst->len = len;
    dst->reserved1 = 0;
    dst->reserved2 = 0;
    dst->RTR = !!(frame->can_id & CAN_RTR_FLAG);
    dst->FF = !!(frame->can_id & CAN_EFF_FLAG);
    dst->id = ntohl(frame->can_id);

    memcpy(dst->data, frame->data, len);
    if (len < 8){
      len = 8;
    }

    return sizeof(*dst) + len;
}

static std::string g_filter;
static uint32_t g_canid;
static uint32_t g_mask;
void setFilter(const std::string& filter) {
    g_filter = filter;

    auto pos = filter.find(":");
    if (pos != std::string::npos) {
        auto id = filter.substr(0,pos);
        auto mask = filter.substr(pos + 1);
        g_canid = std::stoul(id.c_str(), nullptr, 16);
        g_mask = std::stoul(mask.c_str(), nullptr, 16);
        std::cout<< "filter rule: "<<std::hex << g_canid << ":" <<std::hex << g_mask<<std::endl;
    }
}

static bool isEnabled(canfd_frame *frame) {
    if(g_filter.empty())return true;

    auto x = frame->can_id & g_mask;
    if(x == g_canid) {
        return true;
    }
    return false;
}

uint8_t* buildPacket(uint16_t len, uint8_t* packetBuffer,
        std::list<canfd_frame*>& frames, uint8_t seqNo,
        std::function<void(std::list<canfd_frame*>&, std::list<canfd_frame*>::iterator)> handleOverflow)
{
    using namespace cannelloni;

    (void)seqNo;

    uint16_t frameCount = 0;
    uint8_t* data = packetBuffer;
    for (auto it = frames.begin(); it != frames.end(); it++)
    {
        canfd_frame* frame = *it;
        /* Check for packet overflow */
        uint16_t writeable = len -(data - packetBuffer);
        uint8_t dlc = canfd_len(frame);
        uint16_t writeto = (dlc > 8? dlc:8) + sizeof(DTUEthFrame);
        if (writeable < writeto)
        {
            handleOverflow(frames, it);
            break;
        }

        if(isEnabled(frame)) {
            data += encodeFrame(data, frame);
            frameCount++;
        }
    }
    
    return data; // return end of buffer
}
#endif
