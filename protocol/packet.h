#ifndef OPENFLIGHT_PACKET_H
#define OPENFLIGHT_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define OF_PACKET_MARKER_0 0xA5u
#define OF_PACKET_MARKER_1 0x5Au
#define OF_PACKET_MAX_PAYLOAD 32u
#define OF_PACKET_OVERHEAD 9u
#define OF_PACKET_MAX_FRAME (OF_PACKET_OVERHEAD + OF_PACKET_MAX_PAYLOAD)

enum of_message_type {
    OF_MSG_SENSOR = 1,
    OF_MSG_COMMAND = 2,
    OF_MSG_HEARTBEAT = 3,
    OF_MSG_ACK = 4,
};

enum of_packet_result {
    OF_PACKET_OK = 0,
    OF_PACKET_INVALID_ARGUMENT = -1,
    OF_PACKET_INVALID_MARKER = -2,
    OF_PACKET_INVALID_TYPE = -3,
    OF_PACKET_INVALID_LENGTH = -4,
    OF_PACKET_INVALID_CRC = -5,
};

struct of_packet {
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[OF_PACKET_MAX_PAYLOAD];
};

uint16_t of_crc16_ccitt_false(const uint8_t *data, size_t length);
int of_packet_encode(const struct of_packet *packet, uint8_t *frame,
                     size_t frame_capacity, size_t *frame_length);
int of_packet_decode(const uint8_t *frame, size_t frame_length,
                     struct of_packet *packet);

#endif
