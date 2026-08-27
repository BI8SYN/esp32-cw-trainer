#ifndef KOB_PACKET_H
#define KOB_PACKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * CWCom / MorseKOB 线路协议的包编解码。纯 C，无状态，不依赖 ESP-IDF——
 * 本文件在 main/core/ 下，会被 test/Makefile 自动编译进每一个主机测试。
 *
 * 权威参考是 MorseKOB/PyKOB 的 src.py/pykob/internet.py（MIT）。
 * 不要移植 Morse-Code-over-IP/protocol-cwcom 的 C 实现——它是 GPL-3.0。
 *
 * 字段偏移（已对 PyKOB 源码核实，并用真实服务器中继回来的包逐字节验证）：
 *
 *   短包 4B :  cmd@0(i16)  wire@2(i16)
 *   长包 496B: cmd@0(i16)  byts@2(i16)  id@4..132(128B)  seq@136(i32)
 *              码包: code[51]@152..356(51xi32)  n@356(i32)  txt@360..488(128B)
 *              ID包: idflag@140(i32)            n@356 = 0   ver@360..488(128B)
 *
 * 全部小端。ID 包与码包的区别只在 n：n == 0 是 ID 包，n > 0 是码包。
 */

#define KOB_PKT_LONG   496
#define KOB_PKT_SHORT  4
#define KOB_ID_MAX     127   /* 128 字节字段，始终留一个 NUL */
#define KOB_TXT_MAX    127
#define KOB_CODE_MAX   51

enum { KOB_CMD_DIS = 2, KOB_CMD_DAT = 3, KOB_CMD_CON = 4, KOB_CMD_ACK = 5 };

typedef struct {
    int16_t cmd;
    char    id[KOB_ID_MAX + 1];
    int32_t seq;
    int32_t code[KOB_CODE_MAX];
    int32_t n;                       /* 0 = ID 包；>0 = 码包 */
    char    txt[KOB_TXT_MAX + 1];
} kob_packet_t;

/* 三个编码函数都返回写入的字节数，且都不会失败：过长的字符串被截断，
   n 超过 KOB_CODE_MAX 被钳住，office/txt 传 NULL 等同空串。 */
size_t kob_pkt_short(uint8_t out[KOB_PKT_SHORT], int16_t cmd, int16_t wire);
size_t kob_pkt_id(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq,
                  const char *ver);
size_t kob_pkt_code(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq,
                    const int32_t *code, int32_t n, const char *txt);

/* 只接受 2（裸 ACK）、KOB_PKT_SHORT、KOB_PKT_LONG 三种长度，其余返回 false。
   解析出的 n 保证落在 [0, KOB_CODE_MAX]，id/txt 保证以 NUL 结尾。 */
bool kob_pkt_parse(const uint8_t *buf, size_t len, kob_packet_t *out);

#endif
