#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>

#include <stdint.h>

#include "packet.h"

/* Fixed-point values keep the packet-facing sample independent of a host FPU. */
struct sensor_sample {
    uint32_t sequence;
    int16_t temperature_centi_c;
    int16_t roll_centi_deg;
    int16_t pitch_centi_deg;
    int16_t yaw_centi_deg;
    uint16_t battery_millivolts;
};

K_MSGQ_DEFINE(sample_queue, sizeof(struct sensor_sample), 16, 4);

static atomic_t samples_produced;
static atomic_t samples_dropped;
static atomic_t samples_reported;
static atomic_t collection_enabled = ATOMIC_INIT(0);
static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
K_MUTEX_DEFINE(uart_tx_mutex);

static void send_frame(const struct of_packet *packet)
{
    uint8_t frame[OF_PACKET_MAX_FRAME];
    size_t length;
    if (!device_is_ready(uart) ||
        of_packet_encode(packet, frame, sizeof(frame), &length) != OF_PACKET_OK) {
        return;
    }
    k_mutex_lock(&uart_tx_mutex, K_FOREVER);
    for (size_t i = 0; i < length; ++i) {
        uart_poll_out(uart, frame[i]);
    }
    k_mutex_unlock(&uart_tx_mutex);
}

static void command_thread(void *arg1, void *arg2, void *arg3)
{
    struct of_packet_stream stream = {0};
    struct of_packet packet;
    unsigned char byte;
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);
    while (true) {
        while (device_is_ready(uart) && uart_poll_in(uart, &byte) == 0) {
            if (of_packet_stream_feed(&stream, byte, &packet) == 1 &&
                packet.type == OF_MSG_COMMAND && packet.payload_length == 1) {
                uint8_t command = packet.payload[0];
                uint8_t accepted = command == 1 || command == 2;
                if (accepted) {
                    atomic_set(&collection_enabled, command == 1);
                    if (command == 2) { k_msgq_purge(&sample_queue); }
                }
                printk("COMMAND code=%u accepted=%u\n", command, accepted);
                struct of_packet ack = {
                    .type = OF_MSG_ACK, .sequence = packet.sequence,
                    .payload_length = 2, .payload = {command, accepted},
                };
                send_frame(&ack);
            }
        }
        k_sleep(K_MSEC(10));
    }
}

static void store_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void emit_sensor_frame(const struct sensor_sample *sample)
{
    struct of_packet packet = {
        .type = OF_MSG_SENSOR,
        .sequence = (uint16_t)sample->sequence,
        .payload_length = 10,
    };
    uint8_t frame[OF_PACKET_MAX_FRAME];
    size_t frame_length;

    store_u16(&packet.payload[0], (uint16_t)sample->temperature_centi_c);
    store_u16(&packet.payload[2], (uint16_t)sample->roll_centi_deg);
    store_u16(&packet.payload[4], (uint16_t)sample->pitch_centi_deg);
    store_u16(&packet.payload[6], (uint16_t)sample->yaw_centi_deg);
    store_u16(&packet.payload[8], sample->battery_millivolts);
    if (of_packet_encode(&packet, frame, sizeof(frame), &frame_length) != OF_PACKET_OK) {
        printk("FRAME_ERROR seq=%u\n", sample->sequence);
        return;
    }
    printk("FRAME ");
    for (size_t i = 0; i < frame_length; ++i) {
        printk("%02x", frame[i]);
    }
    printk("\n");
    send_frame(&packet);
}

static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
    struct sensor_sample sample = {
        .sequence = 0,
        .temperature_centi_c = 2450,
        .roll_centi_deg = 120,
        .pitch_centi_deg = -80,
        .yaw_centi_deg = 1540,
        .battery_millivolts = 3800,
    };

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        if (!atomic_get(&collection_enabled)) {
            k_sleep(K_MSEC(100));
            continue;
        }
        sample.sequence++;
        sample.temperature_centi_c = 2450 + (sample.sequence % 100);
        sample.yaw_centi_deg = 1540 + (sample.sequence % 30);

        if (k_msgq_put(&sample_queue, &sample, K_NO_WAIT) != 0) {
            struct sensor_sample discarded;
            if (k_msgq_get(&sample_queue, &discarded, K_NO_WAIT) == 0) {
                atomic_inc(&samples_dropped);
            }
            (void)k_msgq_put(&sample_queue, &sample, K_NO_WAIT);
        }
        atomic_inc(&samples_produced);
        k_sleep(K_MSEC(100));
    }
}

static void telemetry_thread(void *arg1, void *arg2, void *arg3)
{
    struct sensor_sample latest;
    struct sensor_sample next;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        k_sleep(K_MSEC(500));
        if (!atomic_get(&collection_enabled)) { continue; }
        if (k_msgq_get(&sample_queue, &latest, K_NO_WAIT) != 0) {
            continue;
        }
        while (k_msgq_get(&sample_queue, &next, K_NO_WAIT) == 0) {
            latest = next;
        }
        printk("SAMPLE seq=%u temp_centi_c=%d roll_centi_deg=%d "
               "pitch_centi_deg=%d yaw_centi_deg=%d battery_mv=%u\n",
               latest.sequence, latest.temperature_centi_c,
               latest.roll_centi_deg, latest.pitch_centi_deg,
               latest.yaw_centi_deg, latest.battery_millivolts);
        emit_sensor_frame(&latest);
        atomic_inc(&samples_reported);
    }
}

static void health_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        k_sleep(K_SECONDS(1));
        printk("HEALTH produced=%ld reported=%ld dropped=%ld queue=%u\n",
               (long)atomic_get(&samples_produced),
               (long)atomic_get(&samples_reported),
               (long)atomic_get(&samples_dropped),
               k_msgq_num_used_get(&sample_queue));
    }
}

K_THREAD_DEFINE(sensor_tid, 1024, sensor_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(telemetry_tid, 1024, telemetry_thread, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(health_tid, 1024, health_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(command_tid, 1024, command_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    printk("OpenFlight simulated sensor controller started\n");
    return 0;
}
