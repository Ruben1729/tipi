#ifndef TIPI_LIBRARY_H
#define TIPI_LIBRARY_H

#include <stdint.h>

#define TIPI_WIRE_VARINT 0
#define TIPI_WIRE_FIX64  1
#define TIPI_WIRE_LEN    2
#define TIPI_WIRE_FIX32  5

typedef enum {
    STATE_IDLE,
    STATE_VARINT,
    STATE_FIX32,
    STATE_LEN_HEADER,
    STATE_BLOB_BODY
} tipi_parser_state_t;

typedef void (*tipi_write_fn)(const uint8_t* data, uint16_t len);
typedef void (*tipi_cmd_handler)(uint8_t tag, uint8_t* buffer, uint16_t size);

typedef uint8_t tipi_error_t;
typedef uint8_t tipi_tag_t;

typedef struct {
    uint8_t key;
    union {
        uint32_t u32;
        struct {
            const uint8_t* ptr;
            uint16_t len;
        } blob;
    } data;
} tipi_record_t;

typedef struct {
    uint8_t* buffer;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
    tipi_write_fn write;
    tipi_cmd_handler handler;

    tipi_parser_state_t  state;
    uint8_t  current_tag;
    uint8_t  current_wire;

    uint32_t accumulator;
    uint8_t  shift;
    uint16_t count;

    uint16_t scratch_idx;
    uint8_t  scratch[64];
} tipi_ctx_t;

tipi_error_t tipi_initialize(tipi_ctx_t* ctx, uint8_t* buf, uint16_t buf_size, tipi_write_fn write_fn);
tipi_error_t tipi_tick(tipi_ctx_t* ctx);
tipi_error_t tipi_parse_byte(tipi_ctx_t* ctx, uint8_t byte);
tipi_error_t tipi_stream_i8(tipi_ctx_t* ctx, uint8_t tag, int8_t value);
tipi_error_t tipi_stream_i16(tipi_ctx_t* ctx, uint8_t tag, int16_t value);
tipi_error_t tipi_stream_i32(tipi_ctx_t* ctx, uint8_t tag, int32_t value);
tipi_error_t tipi_stream_u8(tipi_ctx_t* ctx, uint8_t tag, uint8_t value);
tipi_error_t tipi_stream_u16(tipi_ctx_t* ctx, uint8_t tag, uint16_t value);
tipi_error_t tipi_stream_u32(tipi_ctx_t* ctx, uint8_t tag, uint32_t value);
tipi_error_t tipi_stream_float(tipi_ctx_t* ctx, uint8_t tag, float value);
tipi_error_t tipi_stream_blob(tipi_ctx_t* ctx, uint8_t tag, const uint8_t* data, size_t len);

enum {
    TIPI_ENONE,
    TIPI_EFULL,
    TIPI_EINVALID,
    TIPI_ENULL,
};

#endif // TIPI_LIBRARY_H