#include "packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t check[] = "123456789";
    struct of_packet source = { .type = OF_MSG_SENSOR, .sequence = 0x1234,
                                .payload_length = 3, .payload = {1, 2, 3} };
    struct of_packet decoded = {0};
    uint8_t frame[OF_PACKET_MAX_FRAME];
    size_t frame_length = 0;

    assert(of_crc16_ccitt_false(check, 9) == 0x29B1u);
    assert(of_packet_encode(&source, frame, sizeof(frame), &frame_length) == OF_PACKET_OK);
    assert(frame_length == OF_PACKET_OVERHEAD + 3);
    assert(frame[0] == 0xA5 && frame[1] == 0x5A);
    assert(frame[3] == 0x12 && frame[4] == 0x34);
    assert(of_packet_decode(frame, frame_length, &decoded) == OF_PACKET_OK);
    assert(decoded.type == source.type && decoded.sequence == source.sequence);
    assert(decoded.payload_length == source.payload_length);
    assert(memcmp(decoded.payload, source.payload, source.payload_length) == 0);

    frame[7] ^= 1u;
    assert(of_packet_decode(frame, frame_length, &decoded) == OF_PACKET_INVALID_CRC);
    frame[7] ^= 1u;
    assert(of_packet_decode(frame, frame_length - 1, &decoded) == OF_PACKET_INVALID_LENGTH);
    frame[5] = 0x01;
    assert(of_packet_decode(frame, frame_length, &decoded) == OF_PACKET_INVALID_LENGTH);
    frame[5] = 0x00;
    frame[0] = 0;
    assert(of_packet_decode(frame, frame_length, &decoded) == OF_PACKET_INVALID_MARKER);
    frame[0] = OF_PACKET_MARKER_0;
    frame[2] = 0xff;
    assert(of_packet_decode(frame, frame_length, &decoded) == OF_PACKET_INVALID_TYPE);

    source.payload_length = OF_PACKET_MAX_PAYLOAD + 1;
    assert(of_packet_encode(&source, frame, sizeof(frame), &frame_length) == OF_PACKET_INVALID_LENGTH);
    source.payload_length = 3;
    source.type = 0xff;
    assert(of_packet_encode(&source, frame, sizeof(frame), &frame_length) == OF_PACKET_INVALID_TYPE);
    source.type = OF_MSG_SENSOR;
    assert(of_packet_encode(&source, frame, OF_PACKET_OVERHEAD, &frame_length) ==
           OF_PACKET_INVALID_LENGTH);
    assert(of_packet_encode(&source, frame, sizeof(frame), &frame_length) == OF_PACKET_OK);
    struct of_packet_stream stream = {0};
    assert(of_packet_stream_feed(&stream, 0x55, &decoded) == 0);
    assert(of_packet_stream_feed(&stream, OF_PACKET_MARKER_0, &decoded) == 0);
    assert(of_packet_stream_feed(&stream, OF_PACKET_MARKER_0, &decoded) == 0);
    int result = 0;
    for (size_t i = 1; i < frame_length; ++i) {
        result = of_packet_stream_feed(&stream, frame[i], &decoded);
    }
    assert(result == 1 && decoded.sequence == source.sequence);
    frame[7] ^= 1u;
    for (size_t i = 0; i < frame_length; ++i) {
        result = of_packet_stream_feed(&stream, frame[i], &decoded);
    }
    assert(result == OF_PACKET_INVALID_CRC);
    frame[7] ^= 1u;
    for (size_t i = 0; i < frame_length; ++i) {
        result = of_packet_stream_feed(&stream, frame[i], &decoded);
    }
    assert(result == 1 && decoded.sequence == source.sequence);
    puts("OpenFlight packet tests passed");
    return 0;
}
