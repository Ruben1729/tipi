#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "tipi.h"

// Mock Output Buffer (simulates the wire)
static uint8_t MOCK_WIRE[4096];
static uint16_t MOCK_WIRE_LEN = 0;

int32_t helper_zigzag_decode(uint32_t n);
void mock_write(const uint8_t* data, uint16_t len);
void reset_mock(void);
uint16_t helper_uncobs(const uint8_t* src, uint16_t len, uint8_t* dst);
uint8_t helper_decode_varint(const uint8_t* buf, uint32_t* value);
int helper_check_crc(const uint8_t* data, uint16_t len);

#define RUN_TEST(fn) { \
    printf("Running %-30s ... ", #fn); \
    reset_mock(); \
    fn(); \
    printf("PASS\n"); \
}

void test_tipi_init(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[300];

    assert(tipi_init(NULL, buffer, 100, mock_write) == TIPI_ENULL);
    assert(tipi_init(&ctx, NULL, 100, mock_write) == TIPI_ENULL);
    assert(tipi_init(&ctx, buffer, 100, NULL) == TIPI_ENULL);
    assert(tipi_init(&ctx, buffer, 15, mock_write) == TIPI_EINVALID);
    assert(tipi_init(&ctx, buffer, 201, mock_write) == TIPI_EINVALID);

    assert(tipi_init(&ctx, buffer, 100, mock_write) == TIPI_ENONE);
    assert(ctx.tx_cap == 100);
    assert(ctx.tx_len == 0);
}

void test_stream_unsigned_types(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[64];
    uint8_t decoded[64];
    uint16_t dec_len;

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);
    tipi_stream_u32(&ctx, 1, 42);

    dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);
    assert(MOCK_WIRE_LEN > 0);
    assert(MOCK_WIRE[MOCK_WIRE_LEN - 1] == 0x00);
    assert(decoded[0] == 0x08);
    assert(decoded[1] == 42);
    assert(helper_check_crc(decoded, dec_len) == 1);

    reset_mock();
    tipi_stream_u16(&ctx, 2, 0xFFFF);

    dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);
    assert(helper_check_crc(decoded, dec_len) == 1);
    assert(decoded[0] == 0x10);
    assert(decoded[1] == 0xFF);
    assert(decoded[2] == 0xFF);
    assert(decoded[3] == 0x03);

    reset_mock();
    tipi_stream_u8(&ctx, 3, 250);

    dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);
    assert(helper_check_crc(decoded, dec_len) == 1);
    assert(decoded[0] == 0x18);
    assert(decoded[1] == 0xFA);
    assert(decoded[2] == 0x01);
}

void test_stream_varint_encoding(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[64];
    uint8_t decoded[64];

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);
    tipi_stream_u32(&ctx, 2, 300);

    uint16_t dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);

    assert(decoded[0] == 0x10);
    assert(decoded[1] == 0xAC);
    assert(decoded[2] == 0x02);
    assert(helper_check_crc(decoded, dec_len) == 1);
}

void test_blob_fragmentation(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[32];
    uint8_t blob_data[60];

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);

    for(int i=0; i<60; i++) {
        blob_data[i] = (uint8_t)(i + 1);
    }

    tipi_stream_blob(&ctx, 5, blob_data, 60);

    uint8_t reassembled[256];
    uint16_t re_idx = 0;
    uint8_t* ptr = MOCK_WIRE;
    uint8_t* end = MOCK_WIRE + MOCK_WIRE_LEN;
    int frame_count = 0;

    while (ptr < end) {
        uint8_t* delim = memchr(ptr, 0x00, end - ptr);
        assert(delim != NULL);

        uint16_t frame_len = delim - ptr;
        uint8_t decoded[64];
        uint16_t dec_len = helper_uncobs(ptr, frame_len, decoded);

        assert(helper_check_crc(decoded, dec_len) == 1);
        frame_count++;

        memcpy(&reassembled[re_idx], decoded, dec_len - 2);
        re_idx += (dec_len - 2);

        ptr = delim + 1;
    }

    assert(frame_count >= 2);
    assert(reassembled[0] == 0x2A);
    assert(reassembled[1] == 60);

    for(int i=0; i<60; i++) {
        assert(reassembled[2 + i] == blob_data[i]);
    }
}

void test_flush_logic(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[128];
    uint8_t chunk[10] = {0xAA};

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);

    for(int i=0; i<11; i++) {
        uint16_t old_len = MOCK_WIRE_LEN;
        tipi_stream_blob(&ctx, 1, chunk, 1);
        assert(MOCK_WIRE_LEN > old_len);
    }
}

void test_stream_integers(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[64];
    uint8_t decoded[64];

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);
    tipi_stream_i8(&ctx, 10, -5);

    uint16_t dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);
    assert(helper_check_crc(decoded, dec_len) == 1);
    assert(decoded[0] == 0x50);

    uint32_t raw_val;
    helper_decode_varint(&decoded[1], &raw_val);
    int32_t actual_val = helper_zigzag_decode(raw_val);
    assert(raw_val == 9);
    assert(actual_val == -5);
}

void test_stream_float(void) {
    tipi_ctx_t ctx;
    uint8_t buffer[64];
    uint8_t decoded[64];

    tipi_init(&ctx, buffer, sizeof(buffer), mock_write);

    float my_float = 123.456f;
    tipi_stream_float(&ctx, 3, my_float);

    // FIXED: Decode AFTER streaming
    uint16_t dec_len = helper_uncobs(MOCK_WIRE, MOCK_WIRE_LEN - 1, decoded);

    assert(helper_check_crc(decoded, dec_len) == 1);
    assert(decoded[0] == 0x1D);

    float recovered;
    memcpy(&recovered, &decoded[1], 4);
    float diff = recovered - my_float;
    if (diff < 0) {
        diff = -diff;
    }

    assert(diff < 0.0001f);
}

int main(void) {
    printf("Starting TIPI Unit Tests...\n");
    printf("---------------------------\n");

    RUN_TEST(test_tipi_init);
    RUN_TEST(test_stream_unsigned_types);
    RUN_TEST(test_stream_varint_encoding);
    RUN_TEST(test_blob_fragmentation);
    RUN_TEST(test_flush_logic);
    RUN_TEST(test_stream_integers);
    RUN_TEST(test_stream_float);

    printf("---------------------------\n");
    printf("All tests passed successfully.\n");
    return 0;
}

int32_t helper_zigzag_decode(uint32_t n) {
    return (int32_t)(n >> 1) ^ -(int32_t)(n & 1);
}

uint8_t helper_decode_varint(const uint8_t* buf, uint32_t* value) {
    uint32_t result = 0;
    uint8_t shift = 0;
    uint8_t count = 0;

    while (1) {
        uint8_t byte = buf[count++];
        result |= (uint32_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            break;
        }

        shift += 7;
        if (count > 5) {
            return 0;
        }
    }
    *value = result;
    return count;
}

void mock_write(const uint8_t* data, uint16_t len) {
    if (MOCK_WIRE_LEN + len > sizeof(MOCK_WIRE)) {
        return;
    }
    memcpy(&MOCK_WIRE[MOCK_WIRE_LEN], data, len);
    MOCK_WIRE_LEN += len;
}

void reset_mock(void) {
    MOCK_WIRE_LEN = 0;
    memset(MOCK_WIRE, 0, sizeof(MOCK_WIRE));
}

uint16_t helper_uncobs(const uint8_t* src, uint16_t len, uint8_t* dst) {
    const uint8_t* end = src + len;
    uint16_t out_len = 0;
    while (src < end) {
        uint8_t code = *src++;
        if (code == 0) {
            return 0;
        }

        for (int i = 1; src < end && i < code; i++) {
            dst[out_len++] = *src++;
        }

        if (code < 0xFF && src < end) {
            dst[out_len++] = 0;
        }
    }
    return out_len;
}

int helper_check_crc(const uint8_t* data, uint16_t len) {
    if (len < 2) return 0;
    uint16_t received_crc = data[len-2] | (data[len-1] << 8);

    uint16_t calc = 0xFFFF;
    for (uint16_t i = 0; i < len - 2; i++) {
        calc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (calc & 1) calc = (calc >> 1) ^ 0xA001;
            else calc >>= 1;
        }
    }
    return (calc == received_crc);
}