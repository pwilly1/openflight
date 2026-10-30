#include "packet.h"

#include <string.h>

static int valid_type(uint8_t type)
{
    return type >= OF_MSG_SENSOR && type <= OF_MSG_ACK;
}

uint16_t of_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;

    if (data == NULL && length != 0u) {
        return 0u;
    }
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

int of_packet_encode(const struct of_packet *packet, uint8_t *frame,
                     size_t frame_capacity, size_t *frame_length)
{
    size_t length;
    uint16_t crc;

    if (packet == NULL || frame == NULL || frame_length == NULL) {
        return OF_PACKET_INVALID_ARGUMENT;
    }
    if (!valid_type(packet->type)) {
        return OF_PACKET_INVALID_TYPE;
    }
    if (packet->payload_length > OF_PACKET_MAX_PAYLOAD) {
        return OF_PACKET_INVALID_LENGTH;
    }
    length = OF_PACKET_OVERHEAD + packet->payload_length;
    if (frame_capacity < length) {
        return OF_PACKET_INVALID_LENGTH;
    }

    frame[0] = OF_PACKET_MARKER_0;
    frame[1] = OF_PACKET_MARKER_1;
    frame[2] = packet->type;
    frame[3] = (uint8_t)(packet->sequence >> 8);
    frame[4] = (uint8_t)packet->sequence;
    frame[5] = (uint8_t)(packet->payload_length >> 8);
    frame[6] = (uint8_t)packet->payload_length;
    memcpy(&frame[7], packet->payload, packet->payload_length);
    crc = of_crc16_ccitt_false(&frame[2], 5u + packet->payload_length);
    frame[7u + packet->payload_length] = (uint8_t)(crc >> 8);
    frame[8u + packet->payload_length] = (uint8_t)crc;
    *frame_length = length;
    return OF_PACKET_OK;
}

int of_packet_decode(const uint8_t *frame, size_t frame_length,
                     struct of_packet *packet)
{
    uint16_t payload_length;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (frame == NULL || packet == NULL) {
        return OF_PACKET_INVALID_ARGUMENT;
    }
    if (frame_length < OF_PACKET_OVERHEAD || frame_length > OF_PACKET_MAX_FRAME) {
        return OF_PACKET_INVALID_LENGTH;
    }
    if (frame[0] != OF_PACKET_MARKER_0 || frame[1] != OF_PACKET_MARKER_1) {
        return OF_PACKET_INVALID_MARKER;
    }
    if (!valid_type(frame[2])) {
        return OF_PACKET_INVALID_TYPE;
    }
    payload_length = (uint16_t)((uint16_t)frame[5] << 8 | frame[6]);
    if (payload_length > OF_PACKET_MAX_PAYLOAD ||
        frame_length != OF_PACKET_OVERHEAD + payload_length) {
        return OF_PACKET_INVALID_LENGTH;
    }
    expected_crc = (uint16_t)((uint16_t)frame[7u + payload_length] << 8 |
                              frame[8u + payload_length]);
    actual_crc = of_crc16_ccitt_false(&frame[2], 5u + payload_length);
    if (expected_crc != actual_crc) {
        return OF_PACKET_INVALID_CRC;
    }

    packet->type = frame[2];
    packet->sequence = (uint16_t)((uint16_t)frame[3] << 8 | frame[4]);
    packet->payload_length = payload_length;
    memcpy(packet->payload, &frame[7], payload_length);
    return OF_PACKET_OK;
}

int of_packet_stream_feed(struct of_packet_stream *stream, uint8_t byte,
                          struct of_packet *packet)
{
    uint16_t payload_length;
    size_t expected_length;
    int result;

    if (stream == NULL || packet == NULL) {
        return OF_PACKET_INVALID_ARGUMENT;
    }
    if (stream->used >= OF_PACKET_MAX_FRAME) {
        stream->used = 0;
    }
    if (stream->used == 0) {
        if (byte == OF_PACKET_MARKER_0) {
            stream->frame[stream->used++] = byte;
        }
        return 0;
    }
    if (stream->used == 1 && byte != OF_PACKET_MARKER_1) {
        stream->used = 0;
        if (byte == OF_PACKET_MARKER_0) {
            stream->frame[stream->used++] = byte;
        }
        return 0;
    }
    stream->frame[stream->used++] = byte;
    if (stream->used < 7) {
        return 0;
    }
    payload_length = (uint16_t)((uint16_t)stream->frame[5] << 8 |
                                stream->frame[6]);
    if (payload_length > OF_PACKET_MAX_PAYLOAD) {
        stream->used = 0;
        return OF_PACKET_INVALID_LENGTH;
    }
    expected_length = OF_PACKET_OVERHEAD + payload_length;
    if (stream->used < expected_length) {
        return 0;
    }
    result = of_packet_decode(stream->frame, expected_length, packet);
    stream->used = 0;
    return result == OF_PACKET_OK ? 1 : result;
}
