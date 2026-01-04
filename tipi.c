#include "tipi.h"

#include <string.h>

static uint8_t stipi_encode_varint(uint32_t value, uint8_t* out);
static tipi_error_t stipi_push_record(tipi_ctx_t* ctx, tipi_record_t* rec);

tipi_error_t tipi_initialize(tipi_ctx_t* ctx, uint8_t* buf, uint16_t buf_size, tipi_write_fn write_fn) {
    if (!ctx || !buf || !write_fn) {
        return TIPI_ENULL;
    }

    if (buf_size < sizeof(tipi_record_t)) {
        return TIPI_EINVALID;
    }

    ctx->buffer = buf;
    ctx->size = buf_size;
    ctx->head = 0;
    ctx->tail = 0;
    ctx->write = write_fn;

    return TIPI_ENONE;
}

tipi_error_t tipi_tick(tipi_ctx_t* ctx) {
    if (!ctx) {
        return TIPI_ENULL;
    }

    uint16_t cap = ctx->size / sizeof(tipi_record_t);
    tipi_record_t* array = (tipi_record_t*)ctx->buffer;

    while (ctx->tail != ctx->head) {
        tipi_record_t* rec = &array[ctx->tail];
        uint8_t wire_type = rec->key & 0x07;
        uint8_t scratch[5];

        uint8_t k_len = stipi_encode_varint(rec->key, scratch);
        ctx->write(scratch, k_len);

        if (wire_type == TIPI_WIRE_VARINT) {
            uint8_t v_len = stipi_encode_varint(rec->data.u32, scratch);
            ctx->write(scratch, v_len);
        }
        else if (wire_type == TIPI_WIRE_FIX32) {
            ctx->write((uint8_t*)&rec->data.u32, 4);
        }
        else if (wire_type == TIPI_WIRE_LEN) {
            uint8_t l_len = stipi_encode_varint(rec->data.blob.len, scratch);
            ctx->write(scratch, l_len);
            ctx->write(rec->data.blob.ptr, rec->data.blob.len);
        }

        ctx->tail = (ctx->tail + 1) % cap;
    }
    return TIPI_ENONE;
}

tipi_error_t tipi_parse_byte(tipi_ctx_t* ctx, uint8_t byte) {
    if (!ctx || !ctx->handler) {
        return TIPI_ENULL;
    }

    switch (ctx->state) {
    case STATE_IDLE:
        ctx->current_tag = byte >> 3;
        ctx->current_wire = byte & 0x07;

        if (ctx->current_wire == TIPI_WIRE_VARINT) {
            ctx->state = STATE_VARINT;
            ctx->accumulator = 0;
            ctx->shift = 0;
        }
        else if (ctx->current_wire == TIPI_WIRE_FIX32) {
            ctx->state = STATE_FIX32;
            ctx->scratch_idx = 0;
        }
        else if (ctx->current_wire == TIPI_WIRE_LEN) {
            ctx->state = STATE_LEN_HEADER;
            ctx->accumulator = 0;
            ctx->shift = 0;
        }
        else {
            return TIPI_EINVALID;
        }
        break;

    case STATE_VARINT:
        ctx->accumulator |= (uint32_t)(byte & 0x7F) << ctx->shift;
        if (!(byte & 0x80)) {
            ctx->handler(ctx->current_tag, (uint8_t*)&ctx->accumulator, 4);
            ctx->state = STATE_IDLE;
        }
        else {
            ctx->shift += 7;
            if (ctx->shift >= 35) {
                return TIPI_EINVALID;
            }
        }
        break;

    case STATE_FIX32:
        ctx->scratch[ctx->scratch_idx++] = byte;
        if (ctx->scratch_idx == 4) {
            ctx->handler(ctx->current_tag, ctx->scratch, 4);
            ctx->state = STATE_IDLE;
        }
        break;

    case STATE_LEN_HEADER:
        ctx->accumulator |= (uint32_t)(byte & 0x7F) << ctx->shift;
        if (!(byte & 0x80)) {
            if (ctx->accumulator > 256) {
                return TIPI_EINVALID;
            }
            ctx->scratch_idx = 0;
            ctx->state = (ctx->accumulator == 0) ? STATE_IDLE : STATE_BLOB_BODY;
            if (ctx->accumulator == 0) {
                ctx->handler(ctx->current_tag, NULL, 0);
            }
        }
        else {
            ctx->shift += 7;
        }
        break;

    case STATE_BLOB_BODY:
        ctx->scratch[ctx->scratch_idx++] = byte;
        if (ctx->scratch_idx == ctx->accumulator) {
            ctx->handler(ctx->current_tag, ctx->scratch, (uint16_t)ctx->accumulator);
            ctx->state = STATE_IDLE;
        }
        break;
    }

    return TIPI_ENONE;
}

tipi_error_t tipi_stream_i8(tipi_ctx_t* ctx, uint8_t tag, int8_t value) {
    return tipi_stream_u32(ctx, tag, value);
}

tipi_error_t tipi_stream_i16(tipi_ctx_t* ctx, uint8_t tag, int16_t value) {
    return tipi_stream_u32(ctx, tag, value);
}

tipi_error_t tipi_stream_i32(tipi_ctx_t* ctx, uint8_t tag, int32_t value) {
    return tipi_stream_u32(ctx, tag, (uint32_t)value);
}

tipi_error_t tipi_stream_u8(tipi_ctx_t* ctx, uint8_t tag, uint8_t value) {
    return tipi_stream_u32(ctx, tag, value);
}

tipi_error_t tipi_stream_u16(tipi_ctx_t* ctx, uint8_t tag, uint16_t value) {
    return tipi_stream_u32(ctx, tag, value);
}

tipi_error_t tipi_stream_u32(tipi_ctx_t* ctx, uint8_t tag, uint32_t value) {
    tipi_record_t rec = {.key = (tag << 3) | TIPI_WIRE_VARINT, .data.u32 = value};
    return stipi_push_record(ctx, &rec);
}

tipi_error_t tipi_stream_float(tipi_ctx_t* ctx, uint8_t tag, float value) {
    tipi_record_t rec = {.key = (tag << 3) | TIPI_WIRE_FIX32};
    memcpy(&rec.data.u32, &value, 4);
    return stipi_push_record(ctx, &rec);
}

tipi_error_t tipi_stream_blob(tipi_ctx_t* ctx, uint8_t tag, const uint8_t* data, size_t len) {
    tipi_record_t rec = {
            .key = (tag << 3) | TIPI_WIRE_LEN,
            .data.blob.ptr = data,
            .data.blob.len = len
        };
    return stipi_push_record(ctx, &rec);
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

tipi_error_t stipi_push_record(tipi_ctx_t* ctx, tipi_record_t* rec) {
    uint16_t cap = ctx->size / sizeof(tipi_record_t);
    uint16_t next = (ctx->head + 1) % cap;

    if (next == ctx->tail) {
        return TIPI_EFULL;
    }

    tipi_record_t* array = (tipi_record_t*)ctx->buffer;
    array[ctx->head] = *rec;
    ctx->head = next;
    return TIPI_ENONE;
}
