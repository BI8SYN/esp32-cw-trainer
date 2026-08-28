#include "kob_packet.h"
#include <string.h>

/* 长包内的字段偏移，见 kob_packet.h 顶部的说明 */
#define OFF_CMD      0
#define OFF_BYTS     2
#define OFF_ID       4
#define FIELD_ID     128
#define OFF_SEQ      136
#define OFF_IDFLAG   140
#define OFF_CODE     152
#define OFF_N        356
#define OFF_TXT      360
#define FIELD_TXT    128

/* byts 字段：服务端 KOBServer.py 把这两个字节当 padding，从不读取。
   PyKOB 发的是 492，这里跟着发同样的值，纯粹为了和真实客户端的字节流一致。 */
#define BYTS_VALUE   492

/* 逐字节读写，不用 struct 打包——对齐与 padding 不能靠编译器的运气，
   而且这份代码同时要在主机（x86_64/arm64）和 ESP32 上跑。 */
static void put_i16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
}

static void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(u >> (8 * i));
}

static int16_t get_i16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t get_i32(const uint8_t *p)
{
    uint32_t u = 0;
    for (int i = 0; i < 4; i++) u |= (uint32_t)p[i] << (8 * i);
    return (int32_t)u;
}

/* 定长字段：先清零再拷贝，最多写 field-1 字节，保证末尾一定有 NUL。
   s 为 NULL 等同空串。 */
static void put_str(uint8_t *p, size_t field, const char *s)
{
    memset(p, 0, field);
    if (!s) return;
    size_t n = strlen(s);
    if (n > field - 1) n = field - 1;
    if (n) memcpy(p, s, n);
}

/* 反向：字段里可能根本没有 NUL（对端填满了 128 字节），所以扫描长度要同时
   受字段宽度和输出缓冲两头限制。 */
static void get_str(const uint8_t *p, size_t field, char *out, size_t out_sz)
{
    size_t n = 0;
    while (n < field - 1 && n < out_sz - 1 && p[n] != 0) n++;
    if (n) memcpy(out, p, n);
    out[n] = '\0';
}

size_t kob_pkt_short(uint8_t out[KOB_PKT_SHORT], int16_t cmd, int16_t wire)
{
    put_i16(out + 0, cmd);
    put_i16(out + 2, wire);
    return KOB_PKT_SHORT;
}

size_t kob_pkt_id(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq,
                  const char *ver)
{
    memset(out, 0, KOB_PKT_LONG);
    put_i16(out + OFF_CMD, KOB_CMD_DAT);
    put_i16(out + OFF_BYTS, BYTS_VALUE);
    put_str(out + OFF_ID, FIELD_ID, office);
    put_i32(out + OFF_SEQ, seq);
    put_i32(out + OFF_IDFLAG, 1);
    /* n 留 0 —— 这正是服务器区分 ID 包与码包的依据 */
    put_str(out + OFF_TXT, FIELD_TXT, ver);
    return KOB_PKT_LONG;
}

size_t kob_pkt_code(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq,
                    const int32_t *code, int32_t n, const char *txt)
{
    memset(out, 0, KOB_PKT_LONG);
    put_i16(out + OFF_CMD, KOB_CMD_DAT);
    put_i16(out + OFF_BYTS, BYTS_VALUE);
    put_str(out + OFF_ID, FIELD_ID, office);
    put_i32(out + OFF_SEQ, seq);

    if (n < 0) n = 0;
    if (n > KOB_CODE_MAX) n = KOB_CODE_MAX;
    if (code) {
        for (int32_t i = 0; i < n; i++) put_i32(out + OFF_CODE + 4 * i, code[i]);
    } else {
        n = 0;
    }
    put_i32(out + OFF_N, n);
    put_str(out + OFF_TXT, FIELD_TXT, txt);
    return KOB_PKT_LONG;
}

bool kob_pkt_parse(const uint8_t *buf, size_t len, kob_packet_t *out)
{
    if (!buf || !out) return false;
    memset(out, 0, sizeof *out);

    /* 服务器的 ACK 只有 2 字节（KOBServer.py 的 ackRecord 是 '<H'），
       不是 4 字节短包——两者都要认。 */
    if (len == 2) {
        out->cmd = get_i16(buf + OFF_CMD);
        return true;
    }
    if (len == KOB_PKT_SHORT) {
        out->cmd = get_i16(buf + OFF_CMD);
        return true;
    }
    if (len != KOB_PKT_LONG) return false;

    out->cmd = get_i16(buf + OFF_CMD);
    get_str(buf + OFF_ID, FIELD_ID, out->id, sizeof out->id);
    out->seq = get_i32(buf + OFF_SEQ);

    /* n 来自网络，必须钳住再拿去索引 code[]，否则越界读。 */
    int32_t n = get_i32(buf + OFF_N);
    if (n < 0) n = 0;
    if (n > KOB_CODE_MAX) n = KOB_CODE_MAX;
    out->n = n;
    for (int32_t i = 0; i < n; i++) out->code[i] = get_i32(buf + OFF_CODE + 4 * i);

    get_str(buf + OFF_TXT, FIELD_TXT, out->txt, sizeof out->txt);
    return true;
}
