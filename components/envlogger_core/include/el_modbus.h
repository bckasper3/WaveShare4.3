#pragma once
/* Modbus RTU slave protocol engine + register map (docs/PROTOCOLS.md section 3), pure C.
 *
 * The ESP32 side only moves bytes: a frame delimited by the 3.5-character silent interval (UART
 * RX timeout) goes into el_mb_rtu_process(); the returned bytes (if any) are transmitted. The
 * engine never reads a clock, never touches a UART and never executes anything: commands and
 * settings writes are validated and *reported* to the firmware (gate callback and effects).
 *
 * Not thread-safe: the caller serialises el_mb_* calls on one el_mb_t (typically the Modbus task
 * calls both el_mb_rtu_process and el_mb_set_inputs).
 *
 * Word order: every 32-bit value (float32, u32) is two registers, HIGH word at the LOWER
 * address (big-endian word order), each word big-endian on the wire. A 32-bit value is never
 * read or written partially: a request that starts or ends in the middle of a multi-register
 * item (32-bit value or ASCII string) gets exception 02. Reason: a torn float/limit would be
 * silently wrong data. Reads that touch an unmapped register also get exception 02.
 * ASCII strings: 2 characters per register, first character in the high byte, NUL padded. */
#include <stddef.h>
#include <stdint.h>
#include "el_err.h"
#include "el_result.h"

#define EL_MB_RTU_MAX_FRAME    256   /* RTU ADU limit; give el_mb_rtu_process a resp buffer this big */
#define EL_MB_MAX_READ_REGS    125
#define EL_MB_MAX_WRITE_REGS   123
#define EL_MB_TAG_REGS         10    /* tag (location) string = 10 regs = 20 bytes (<= 19 chars) */
#define EL_MB_SERIAL_REGS      8
#define EL_MB_VENDOR           "envlogger"
#define EL_MB_PRODUCT_CODE     "ENV-TRH-1"

/* Function codes */
enum { EL_MB_FC_READ_HOLDING = 0x03, EL_MB_FC_READ_INPUT = 0x04, EL_MB_FC_WRITE_SINGLE = 0x06,
       EL_MB_FC_DIAGNOSTIC = 0x08, EL_MB_FC_WRITE_MULTI = 0x10, EL_MB_FC_ENCAP = 0x2B };

/* Exception codes */
enum { EL_MB_EX_FUNCTION = 1, EL_MB_EX_ADDRESS = 2, EL_MB_EX_VALUE = 3,
       EL_MB_EX_DEVICE_FAILURE = 4, EL_MB_EX_BUSY = 6 };

/* Input register addresses (FC04). One contiguous block 0..27, plus 30..32, 40.., 50.. */
enum {
    EL_MB_IR_TEMP = 0,           /* f32 degC of the last result */
    EL_MB_IR_STATUS = 2,         /* 0 none, 1 normal, 2 alarm, 3 error */
    EL_MB_IR_RESULT_COUNTER = 3, /* u16, wraps */
    EL_MB_IR_TIMESTAMP = 4,      /* u32 Unix s of last result */
    EL_MB_IR_MEAS_STATE = 6,     /* 0 idle, 1 measuring, 2 done, 3 fault */
    EL_MB_IR_ALARMS = 7,         /* bit0 T low, bit1 T high, bit2 RH low, bit3 RH high */
    EL_MB_IR_PROGRESS = 8,       /* measurement progress % */
    EL_MB_IR_FLAGS = 9,          /* EL_MB_FLAG_* */
    EL_MB_IR_HUMIDITY = 10,      /* f32 %RH of the last result */
    EL_MB_IR_REPEATS = 12,       /* readings averaged for the last result */
    EL_MB_IR_DEW_POINT = 13,     /* f32 degC, NaN if unknown */
    EL_MB_IR_TEMP_SD = 15,       /* f32 degC */
    EL_MB_IR_RESULT_SEQ = 17,    /* u32 log sequence number of the last result */
    EL_MB_IR_RH_SD = 19,         /* f32 %RH */
    EL_MB_IR_UPTIME = 21,        /* u32 s */
    EL_MB_IR_LOG_COUNT = 23,     /* u32 results in the log */
    EL_MB_IR_LOG_USED_PCT = 25,  /* log capacity used, % */
    EL_MB_IR_NEXT_AUTO = 26,     /* u32 s until the next automatic reading, 0xFFFFFFFF = off */
    EL_MB_IR_FW_MAJOR = 30, EL_MB_IR_FW_MINOR = 31, EL_MB_IR_FW_PATCH = 32,
    EL_MB_IR_SERIAL = 40,        /* 8 regs ASCII */
    EL_MB_IR_TAG = 50            /* 10 regs ASCII, tag of the last result */
};

/* Holding register addresses (FC03/06/16), contiguous 100..114 */
enum {
    EL_MB_HR_COMMAND = 100,      /* write 1 start, 2 cancel, 3 clear alarm; 0 = no-op; reads 0 */
    EL_MB_HR_REPEATS = 101,
    EL_MB_HR_T_LOW = 102,        /* f32 degC, -40..125 */
    EL_MB_HR_T_HIGH = 104,       /* f32 degC; low < high */
    EL_MB_HR_RH_LOW = 106,       /* f32 %RH, 0..100 */
    EL_MB_HR_RH_HIGH = 108,      /* f32 %RH; low < high */
    EL_MB_HR_ADDRESS = 110,      /* 1..247, applies after reboot */
    EL_MB_HR_BAUD = 111,         /* 0=9600 1=19200 2=38400 3=57600 4=115200 */
    EL_MB_HR_PARITY = 112,       /* 0 none (2 stop bits), 1 even, 2 odd */
    EL_MB_HR_LOG_INTERVAL = 113  /* u32 s, 0 = off or 10..86400 */
};

typedef enum { EL_MB_CMD_NONE = 0, EL_MB_CMD_START = 1, EL_MB_CMD_CANCEL = 2,
               EL_MB_CMD_CLEAR_ALARM = 3 } el_mb_cmd_t;

/* Input register flags (register 9) */
#define EL_MB_FLAG_SENSOR_FAULT  (1u << 0)   /* the last measurement faulted */
#define EL_MB_FLAG_LOG_NEAR_FULL (1u << 1)
#define EL_MB_FLAG_EXPORT_BUSY   (1u << 2)
#define EL_MB_FLAG_SIMULATED     (1u << 3)
#define EL_MB_FLAG_AUTOLOG_ON    (1u << 4)
#define EL_MB_FLAG_SD_PRESENT    (1u << 5)

/* Snapshot the firmware refreshes with el_mb_set_inputs(). Strings are NUL-terminated. */
typedef struct {
    float    temp_c;                 /* NaN if no result */
    float    rh_pct;
    float    dew_point_c;
    float    temp_sd_c, rh_sd_pct;
    uint16_t status;
    uint16_t alarms;
    uint16_t result_counter;
    uint32_t last_seq;
    uint32_t last_timestamp_s;
    uint16_t repeats;
    uint16_t meas_state;
    uint16_t progress_pct;
    uint16_t flags;
    uint16_t fw_major, fw_minor, fw_patch;
    char     serial[EL_MB_SERIAL_REGS * 2 + 1];
    char     last_tag[EL_MB_TAG_REGS * 2 + 1];
    uint32_t uptime_s;
    uint32_t log_count;
    uint16_t log_used_pct;
    uint32_t next_auto_s;
} el_mb_inputs_t;

/* Current values of the holding registers (what FC03 returns). The firmware sets this from the
 * settings module with el_mb_set_holding(). */
typedef struct {
    uint16_t    repeats;
    el_limits_t limits;
    uint16_t    address, baud_code, parity_code;
    uint32_t    log_interval_s;
} el_mb_holding_t;

/* What a write asked for. mask says which members are valid. */
#define EL_MB_FX_COMMAND   (1u << 0)
#define EL_MB_FX_REPEATS   (1u << 1)
#define EL_MB_FX_T_LOW     (1u << 2)
#define EL_MB_FX_T_HIGH    (1u << 3)
#define EL_MB_FX_RH_LOW    (1u << 4)
#define EL_MB_FX_RH_HIGH   (1u << 5)
#define EL_MB_FX_ADDRESS   (1u << 6)
#define EL_MB_FX_BAUD      (1u << 7)
#define EL_MB_FX_PARITY    (1u << 8)
#define EL_MB_FX_INTERVAL  (1u << 9)

typedef struct {
    uint32_t mask;
    el_mb_cmd_t command;
    el_mb_holding_t set;   /* only members named in mask are meaningful */
} el_mb_effects_t;

typedef enum { EL_MB_ACCEPT = 0, EL_MB_REJECT_BUSY, EL_MB_REJECT_FAILURE } el_mb_verdict_t;

/* Optional gate, called once per valid write (atomic: all registers of one request) BEFORE it is
 * recorded. EL_MB_REJECT_BUSY -> exception 06, EL_MB_REJECT_FAILURE -> exception 04, and nothing
 * is applied. Runs inside el_mb_rtu_process: keep it short, no blocking. Broadcast writes also
 * pass through it (the rejection is just not transmitted). */
typedef el_mb_verdict_t (*el_mb_gate_fn)(void *ctx, const el_mb_effects_t *fx);

typedef struct {
    uint32_t frames_rx;          /* every frame handed to the engine */
    uint32_t crc_errors;
    uint32_t bad_length;         /* shorter than 4 bytes or longer than 256 */
    uint32_t broadcasts;
    uint32_t other_address;
    uint32_t responses_tx;       /* normal responses produced */
    uint32_t exceptions_tx;      /* exception responses produced */
} el_mb_stats_t;

typedef struct {
    uint8_t         address;     /* active slave address 1..247 */
    el_mb_inputs_t  inputs;
    el_mb_holding_t holding;
    el_mb_effects_t pending;     /* accepted writes not yet taken */
    el_mb_gate_fn   gate;
    void           *gate_ctx;
    el_mb_stats_t   stats;
} el_mb_t;

/* Zeroes everything; holding defaults: repeats 3, limits 5..35 degC / 20..80 %RH, address as
 * given, baud code 1 (19200), parity code 1 (even), interval 300 s. EL_ERR_ARG if mb NULL or address not 1..247. */
el_err_t el_mb_init(el_mb_t *mb, uint8_t address);
void     el_mb_set_gate(el_mb_t *mb, el_mb_gate_fn gate, void *ctx);
void     el_mb_set_inputs(el_mb_t *mb, const el_mb_inputs_t *in);
/* Authoritative holding values; call at boot and whenever the firmware changes/rejects a setting. */
void     el_mb_set_holding(el_mb_t *mb, const el_mb_holding_t *h);

/* Hand over accepted writes since the last call (and clear them). Returns 1 if any, else 0. The
 * engine already shows written values on read-back; if the settings module rejects one, the
 * firmware restores the truth with el_mb_set_holding(). Several commands between two takes:
 * the latest wins. */
int      el_mb_take_effects(el_mb_t *mb, el_mb_effects_t *out);

/* Process one received RTU frame (address..CRC). Writes the complete response frame (with CRC)
 * to resp and returns its length, or 0 for "send nothing": bad CRC/length, other address,
 * broadcast, or cap too small (use >= EL_MB_RTU_MAX_FRAME). Never writes beyond cap. */
size_t   el_mb_rtu_process(el_mb_t *mb, const uint8_t *req, size_t len, uint8_t *resp, size_t cap);

/* ---- helpers shared by engine, tests and the PC tool ---- */
uint16_t el_mb_crc16(const uint8_t *data, size_t len);   /* CRC-16/MODBUS */
/* Append CRC (low byte first) after len bytes; buf needs len+2. Returns len+2. */
size_t   el_mb_frame_seal(uint8_t *buf, size_t len);

void     el_mb_put_f32(uint16_t w[2], float v);
float    el_mb_get_f32(const uint16_t w[2]);
void     el_mb_put_u32(uint16_t w[2], uint32_t v);
uint32_t el_mb_get_u32(const uint16_t w[2]);
/* NUL padded; chars beyond nregs*2 are cut. */
void     el_mb_put_str(uint16_t *w, size_t nregs, const char *s);
/* Decode; EL_OK, EL_ERR_FORMAT (non-printable char, or data after the NUL), EL_ERR_SIZE (cap <
 * length+1). out is always terminated when cap > 0. */
el_err_t el_mb_get_str(const uint16_t *w, size_t nregs, char *out, size_t cap);

/* ---- master side (tests, future PC tool) ---- */
/* Build requests; return frame length incl. CRC, or 0 if buf too small / qty out of range. */
size_t   el_mb_master_read(uint8_t *buf, size_t cap, uint8_t addr, uint8_t fc, uint16_t start, uint16_t qty);
size_t   el_mb_master_write_single(uint8_t *buf, size_t cap, uint8_t addr, uint16_t reg, uint16_t value);
size_t   el_mb_master_write_multi(uint8_t *buf, size_t cap, uint8_t addr, uint16_t start,
                                  const uint16_t *values, uint16_t qty);

typedef struct {
    uint8_t  address;
    uint8_t  function;           /* without the 0x80 bit */
    uint8_t  exception;          /* 0 = normal response */
    uint16_t start;              /* write responses: echoed register */
    uint16_t quantity;           /* FC16: echoed quantity; reads: registers returned */
    uint16_t regs[EL_MB_MAX_READ_REGS]; /* reads: data; FC06: regs[0] = echoed value */
} el_mb_master_resp_t;

/* Parse a response to a FC03/04/06/16 request. EL_OK (check .exception), EL_ERR_CRC,
 * EL_ERR_FORMAT (malformed, or address/function differs from expect_*), EL_ERR_ARG. */
el_err_t el_mb_master_parse(const uint8_t *frame, size_t len, uint8_t expect_addr, uint8_t expect_fc,
                            el_mb_master_resp_t *out);
