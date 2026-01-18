#include "tipi.h"

#include <string.h>

static void stipi_flush_frame(tipi_ctx_t* ctx);
static void stipi_write_raw(tipi_ctx_t* ctx, const uint8_t* data, uint16_t len);
static uint8_t stipi_encode_varint(uint32_t value, uint8_t* out);
static uint16_t stipi_crc16(const uint8_t* data, uint16_t len);
static uint16_t stipi_cobs_encode(const uint8_t* ptr, uint16_t length, uint8_t* dst);
static uint32_t stipi_zigzag_encode(int32_t n);

tipi_error_t tipi_init(tipi_ctx_t* ctx, uint8_t* buf, uint16_t buf_size, tipi_write_fn write_fn) {
    if (!ctx || !buf || !write_fn) {
        return TIPI_ENULL;
    }

    if (buf_size < 16 || buf_size > 200) {
        return TIPI_EINVALID;
    }

    memset(ctx, 0, sizeof(tipi_ctx_t));
    ctx->tx_buffer = buf;
    ctx->tx_cap = buf_size;
    ctx->tx_len = 0;
    ctx->write = write_fn;
    
    return TIPI_ENONE;
}

tipi_error_t tipi_stream_i8(tipi_ctx_t* ctx, uint8_t tag, int8_t value) {
    return tipi_stream_u32(ctx, tag, stipi_zigzag_encode((int32_t)value));
}

tipi_error_t tipi_stream_i16(tipi_ctx_t* ctx, uint8_t tag, int16_t value) {
    return tipi_stream_u32(ctx, tag, stipi_zigzag_encode((int32_t)value));
}

tipi_error_t tipi_stream_i32(tipi_ctx_t* ctx, uint8_t tag, int32_t value) {
    return tipi_stream_u32(ctx, tag, stipi_zigzag_encode(value));
}

tipi_error_t tipi_stream_u8(tipi_ctx_t* ctx, uint8_t tag, uint8_t value) {
    return tipi_stream_u32(ctx, tag, (uint32_t)value);
}

tipi_error_t tipi_stream_u16(tipi_ctx_t* ctx, uint8_t tag, uint16_t value) {
    return tipi_stream_u32(ctx, tag, (uint32_t)value);
}

tipi_error_t tipi_stream_u32(tipi_ctx_t* ctx, uint8_t tag, uint32_t value) {
    if (!ctx) {
        return TIPI_ENULL;
    }

    uint8_t buf[10];
    uint8_t idx = stipi_encode_varint((tag << 3) | 0, &buf[0]);
    idx += stipi_encode_varint(value, &buf[idx]);

    stipi_write_raw(ctx, buf, idx);
    stipi_flush_frame(ctx);
    return TIPI_ENONE;
}

tipi_error_t tipi_stream_float(tipi_ctx_t* ctx, uint8_t tag, float value) {
    if (!ctx) {
        return TIPI_ENULL;
    }

    uint8_t buf[10];
    uint8_t idx = stipi_encode_varint((tag << 3) | 5, &buf[0]);
    uint32_t raw_bits;
    memcpy(&raw_bits, &value, 4);

    buf[idx++] = (uint8_t)(raw_bits & 0xFF);
    buf[idx++] = (uint8_t)((raw_bits >> 8) & 0xFF);
    buf[idx++] = (uint8_t)((raw_bits >> 16) & 0xFF);
    buf[idx++] = (uint8_t)((raw_bits >> 24) & 0xFF);

    stipi_write_raw(ctx, buf, (uint16_t)idx);
    stipi_flush_frame(ctx);
    return TIPI_ENONE;
}

tipi_error_t tipi_stream_blob(tipi_ctx_t* ctx, uint8_t tag, const uint8_t* data, uint16_t len) {
    if (!ctx) {
        return TIPI_ENULL;
    }

    uint8_t header[10];
    uint8_t h_len = stipi_encode_varint((tag << 3) | 2, &header[0]);
    h_len += stipi_encode_varint((uint32_t)len, &header[h_len]);

    stipi_write_raw(ctx, header, h_len);
    stipi_write_raw(ctx, data, len);
    stipi_flush_frame(ctx);

    return TIPI_ENONE;
}

void stipi_write_raw(tipi_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    uint16_t written = 0;
    while (written < len) {
        uint16_t safe_cap = ctx->tx_cap - 8;

        if (ctx->tx_len >= safe_cap) {
            stipi_flush_frame(ctx);
        }

        uint16_t available = safe_cap - ctx->tx_len;
        uint16_t to_copy = (len - written) < available ? (len - written) : available;

        memcpy(&ctx->tx_buffer[ctx->tx_len], &data[written], to_copy);
        ctx->tx_len += to_copy;
        written += to_copy;
    }
}

void stipi_flush_frame(tipi_ctx_t* ctx) {
    if (ctx->tx_len == 0) {
        return;
    }

    uint16_t crc = stipi_crc16(ctx->tx_buffer, ctx->tx_len);
    ctx->tx_buffer[ctx->tx_len++] = (uint8_t)(crc & 0xFF);
    ctx->tx_buffer[ctx->tx_len++] = (uint8_t)(crc >> 8);

    uint8_t encoded[256];
    uint16_t enc_len = stipi_cobs_encode(ctx->tx_buffer, ctx->tx_len, encoded);
    uint8_t zero = 0;

    ctx->write(encoded, enc_len);
    ctx->write(&zero, 1);
    ctx->tx_len = 0;
}

uint8_t stipi_encode_varint(uint32_t value, uint8_t* out) {
    uint8_t i = 0;
    while (value >= 0x80) {
        out[i++] = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    out[i++] = (uint8_t)value;
    return i;
}

uint16_t stipi_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

uint16_t stipi_cobs_encode(const uint8_t* ptr, uint16_t length, uint8_t* dst) {
    uint16_t write_index = 1;
    uint16_t code_index = 0;
    uint8_t code = 1;

    for (uint16_t i = 0; i < length; i++) {
        if (ptr[i] == 0) {
            dst[code_index] = code;
            code = 1;
            code_index = write_index++;
        } else {
            dst[write_index++] = ptr[i];
            code++;
            if (code == 0xFF) {
                dst[code_index] = code;
                code = 1;
                code_index = write_index++;
            }
        }
    }
    dst[code_index] = code;
    return write_index;
}

uint32_t stipi_zigzag_encode(int32_t n) {
    return (uint32_t)((n << 1) ^ (n >> 31));
}
