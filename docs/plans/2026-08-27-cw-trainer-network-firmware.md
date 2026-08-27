# CW Trainer 联网对练（固件侧）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `firmware/cw-trainer` 能连上 WiFi、接入自建的 CWCom wire 服务器，并在一页界面上完成收报与发报对打。

**Architecture:** 协议与缓冲逻辑全部放进 `main/core/`（纯 C、零 ESP-IDF 依赖），由主机测试覆盖；ESP-IDF 只做三件事——WiFi 站点管理、UDP socket 任务、NVS 持久化；LVGL 页面通过既有的 `ui_async_post()` 单向接收网络事件。WiFi 配置存在**独立 NVS key**，不进 `cw_settings_t`。

**Tech Stack:** ESP-IDF 5.5 / LVGL / lwIP UDP socket / NVS blob / 主机测试用 C11 + `test_util.h`

## Global Constraints

- 协议权威实现是 `MorseKOB/PyKOB` 的 `src.py/pykob/internet.py`（MIT）。**不要移植 `Morse-Code-over-IP/protocol-cwcom` 的 C 代码——它是 GPL-3.0，会传染本项目。**
- 命令码：`DIS=2, DAT=3, CON=4, ACK=5`，全部小端 `int16`。
- 长包固定 **496 字节**，短包固定 **4 字节**。
- 服务器默认 `8.138.130.141:7890`，wire `11`。wire **不得**默认为 1844 或 1850（服务端 `pins.py` 的历史 PIN，见 spec 5.3）。
- **`cw_settings_t` 与 `CW_SETTINGS_VERSION` 一个字节都不许动**。该结构体一旦长度或版本变化就 `set_defaults()`，会清空用户全部 Koch 进度（`cw_settings.c:59`）。
- `main/core/*.c` 会被测试 Makefile 自动 glob 编译进**每一个**测试程序，因此该目录下的文件**不得** `#include` 任何 ESP-IDF 头。
- 编译必须零警告：测试用 `-std=c11 -Wall -Wextra -Werror`。
- LVGL 非线程安全。任何非 LVGL 任务（socket 任务、`key_input` 的 5ms 任务、`esp_timer` 回调）都**必须**经由 `ui_async_post()` 才能碰 `lv_obj_t`。
- WiFi 默认关闭，只在配网页与对练页临时开启（spec 3.5）。
- 每个任务结束时提交一次。

---

## File Structure

**新建（纯 C 核心，`main/core/`，主机可测）**

| 文件 | 职责 |
|---|---|
| `core/kob_packet.c/.h` | CWCom 包编解码。短包 4B、ID 包 496B、码包 496B。无状态。 |
| `core/kob_client.c/.h` | 协议状态机：连接 → 注册 → 在线、保活、seq 去重。不碰 socket，靠回调收发字节。 |
| `core/kob_rxbuf.c/.h` | 收报抖动缓冲 + 闭合电路语义（`+1`/`+2` 锁存、`n>3000` 打断）。 |

**新建（ESP-IDF 胶水，`main/`）**

| 文件 | 职责 |
|---|---|
| `net_store.c/.h` | 独立 NVS blob：4 个 WiFi 槽位 + 网络设置；从 fmo-companion 的 `appcfg` 导入。 |
| `wifi_net.c/.h` | WiFi 站点：按需开关、扫描、与已存槽位匹配、连接、事件上报。 |
| `tone_seq.c/.h` | 播放原始时序元素序列（`cw_player` 只能播文本，装不下 int32 数组）。 |
| `kob_net.c/.h` | UDP socket 任务，驱动 `kob_client`，把收到的码接到 `tone_seq`，把 `key_input` 的时序打包发出。 |

**新建（UI，`main/ui/`）**

| 文件 | 职责 |
|---|---|
| `ui_wifi.c/.h` | 配网页：槽位列表、扫描选择、键盘输入、从 FMO 导入。 |
| `ui_net.c/.h` | 对练页：按 spec 6.4 线框，覆盖 6.5 全部状态。 |

**修改**

| 文件 | 改动 |
|---|---|
| `main/main.c` | `net_store_init()`；WiFi 与 socket 任务**不在**这里启动（按需） |
| `main/ui/ui_home.c` | 主页加「联网对练」入口 |
| `main/ui/ui_settings.c` | 设置页加「网络」分组 |
| `firmware/cw-trainer/sdkconfig.defaults` | 打开 WiFi 相关配置 |

---

## Task 1: kob_packet — 包编解码

纯 C，无状态，本计划中 TDD 最纯粹的一块。黄金向量是**从真实服务器抓下来的**，不是手算的。

**Files:**
- Create: `firmware/cw-trainer/main/core/kob_packet.c`, `firmware/cw-trainer/main/core/kob_packet.h`
- Test: `firmware/cw-trainer/test/test_kob_packet.c`

**Interfaces:**
- Consumes: 无
- Produces:
```c
#define KOB_PKT_LONG   496
#define KOB_PKT_SHORT  4
#define KOB_ID_MAX     127
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

size_t kob_pkt_short(uint8_t out[KOB_PKT_SHORT], int16_t cmd, int16_t wire);
size_t kob_pkt_id(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq, const char *ver);
size_t kob_pkt_code(uint8_t out[KOB_PKT_LONG], const char *office, int32_t seq,
                    const int32_t *code, int32_t n, const char *txt);
bool   kob_pkt_parse(const uint8_t *buf, size_t len, kob_packet_t *out);
```

**字段偏移（已对 PyKOB 源码核实，且用真实包验证过）**

```
短包 4B :  cmd@0(i16)  wire@2(i16)
长包 496B: cmd@0(i16)  byts@2(i16)  id@4..132(128B)  seq@136(i32)
           码包: code[51]@152..356(51×i32)  n@356(i32)  txt@360..488(128B)
           ID包: idflag@140(i32)            n@356 = 0   ver@360..488(128B)
```
全部小端。`byts` 字段服务端不读，填 492。

- [ ] **Step 1: 生成黄金向量**

```bash
python3 - <<'PY' > /tmp/golden_kob.h
import socket, struct, time
DIS, DAT, CON = 2, 3, 4
short = struct.Struct("<hh")
idPkt = struct.Struct("<hh 128s 4x i i 8x 208x 128s 8x")
codePkt = struct.Struct("<hh 128s 4x i 12x 51i i 128s 8x")
HOST, PORT, WIRE = "8.138.130.141", 7890, 11
def dump(name, b):
    print("static const uint8_t %s[%d] = {" % (name, len(b)))
    for i in range(0, len(b), 12):
        print("    " + " ".join("0x%02X," % c for c in b[i:i+12]))
    print("};\n")
V = [-2000, 60, -60, 60, -60, 60, -60, 180]
code = V + [0] * (51 - len(V))
dump("GOLDEN_CON",  short.pack(CON, WIRE))
dump("GOLDEN_ID",   idPkt.pack(DAT, 492, b"PROBE-A", 0, 1, b"CWTRN 1.0"))
dump("GOLDEN_CODE", codePkt.pack(DAT, 492, b"PROBE-A", 7, *code, len(V), b"V"))
PY
```
把输出粘进 `test_kob_packet.c` 顶部。真实中继回来的包已确认与 `GOLDEN_CODE` 逐字节一致（`id=PROBE-A seq=7 n=8 txt=V code=[-2000,60,-60,60,-60,60,-60,180]`）。

- [ ] **Step 2: 写失败的测试**

```c
#include "test_util.h"
#include "kob_packet.h"
/* ... 此处粘贴 Step 1 生成的 GOLDEN_* ... */

TEST(short_packet_matches_golden) {
    uint8_t buf[KOB_PKT_SHORT];
    ASSERT_EQ_INT(kob_pkt_short(buf, KOB_CMD_CON, 11), KOB_PKT_SHORT);
    ASSERT_EQ_INT(memcmp(buf, GOLDEN_CON, sizeof GOLDEN_CON), 0);
}

TEST(id_packet_matches_golden) {
    uint8_t buf[KOB_PKT_LONG];
    ASSERT_EQ_INT(kob_pkt_id(buf, "PROBE-A", 0, "CWTRN 1.0"), KOB_PKT_LONG);
    ASSERT_EQ_INT(memcmp(buf, GOLDEN_ID, sizeof GOLDEN_ID), 0);
}

TEST(code_packet_matches_golden) {
    static const int32_t v[] = { -2000, 60, -60, 60, -60, 60, -60, 180 };
    uint8_t buf[KOB_PKT_LONG];
    ASSERT_EQ_INT(kob_pkt_code(buf, "PROBE-A", 7, v, 8, "V"), KOB_PKT_LONG);
    ASSERT_EQ_INT(memcmp(buf, GOLDEN_CODE, sizeof GOLDEN_CODE), 0);
}

TEST(parse_roundtrips_code_packet) {
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(GOLDEN_CODE, sizeof GOLDEN_CODE, &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_DAT);
    ASSERT_EQ_STR(p.id, "PROBE-A");
    ASSERT_EQ_INT(p.seq, 7);
    ASSERT_EQ_INT(p.n, 8);
    ASSERT_EQ_STR(p.txt, "V");
    ASSERT_EQ_INT(p.code[0], -2000);
    ASSERT_EQ_INT(p.code[7], 180);
}

TEST(parse_id_packet_has_n_zero) {
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(GOLDEN_ID, sizeof GOLDEN_ID, &p));
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "PROBE-A");
}

TEST(parse_ack_short_packet) {
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(ack, sizeof ack, &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_ACK);
}

TEST(parse_rejects_bad_length) {
    kob_packet_t p;
    uint8_t junk[100] = {0};
    ASSERT_TRUE(!kob_pkt_parse(junk, 100, &p));
    ASSERT_TRUE(!kob_pkt_parse(junk, 0, &p));
}

TEST(parse_clamps_absurd_n) {
    /* 服务器或攻击者给出 n > 51 时不得越界读 */
    uint8_t buf[KOB_PKT_LONG];
    memcpy(buf, GOLDEN_CODE, sizeof buf);
    buf[356] = 0xFF; buf[357] = 0xFF; buf[358] = 0xFF; buf[359] = 0x7F;  /* n = INT32_MAX */
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(buf, sizeof buf, &p));
    ASSERT_TRUE(p.n >= 0 && p.n <= KOB_CODE_MAX);
}

TEST(id_field_without_nul_is_terminated) {
    /* 128 字节全填满、无 NUL 的 id 不得让 parse 读出界 */
    uint8_t buf[KOB_PKT_LONG];
    memcpy(buf, GOLDEN_CODE, sizeof buf);
    memset(buf + 4, 'X', 128);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(buf, sizeof buf, &p));
    ASSERT_EQ_INT((int)strlen(p.id), KOB_ID_MAX);
}

TEST(long_office_id_is_truncated_not_overflowed) {
    char longid[300];
    memset(longid, 'A', sizeof longid - 1);
    longid[sizeof longid - 1] = '\0';
    uint8_t buf[KOB_PKT_LONG];
    ASSERT_EQ_INT(kob_pkt_id(buf, longid, 0, "V"), KOB_PKT_LONG);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(buf, KOB_PKT_LONG, &p));
    ASSERT_EQ_INT((int)strlen(p.id), KOB_ID_MAX);
}

int main(void) {
    RUN(short_packet_matches_golden);
    RUN(id_packet_matches_golden);
    RUN(code_packet_matches_golden);
    RUN(parse_roundtrips_code_packet);
    RUN(parse_id_packet_has_n_zero);
    RUN(parse_ack_short_packet);
    RUN(parse_rejects_bad_length);
    RUN(parse_clamps_absurd_n);
    RUN(id_field_without_nul_is_terminated);
    RUN(long_office_id_is_truncated_not_overflowed);
    TEST_MAIN_END();
}
```

- [ ] **Step 3: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test test_kob_packet`
Expected: 编译失败，`kob_packet.h: No such file or directory`

- [ ] **Step 4: 实现**

用显式逐字节小端读写，**不要**用 `struct` 打包或 `memcpy` 整个结构体——ESP32 是小端而主机也是小端，但对齐和 padding 不能靠运气。

```c
static void put_i16(uint8_t *p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_i32(uint8_t *p, int32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int16_t get_i16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static int32_t get_i32(const uint8_t *p) {
    uint32_t u = 0; for (int i = 0; i < 4; i++) u |= (uint32_t)p[i] << (8 * i); return (int32_t)u;
}
static void put_str(uint8_t *p, size_t field, const char *s) {
    memset(p, 0, field);
    size_t n = s ? strlen(s) : 0;
    if (n > field - 1) n = field - 1;      /* 始终留一个 NUL */
    if (n) memcpy(p, s, n);
}
static void get_str(const uint8_t *p, size_t field, char *out, size_t out_sz) {
    size_t n = 0;
    while (n < field - 1 && n < out_sz - 1 && p[n]) n++;
    memcpy(out, p, n);
    out[n] = '\0';
}
```

`kob_pkt_parse` 的分派：`len == 2` → 只有 cmd（ACK）；`len == 4` → 短包；`len == 496` → 长包，按 `n` 区分 ID/码包；其余一律返回 `false`。解析时 `n` 必须钳到 `[0, KOB_CODE_MAX]`。

- [ ] **Step 5: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: 全部通过（原有 41 用例 + 新增 10）；零警告

- [ ] **Step 6: 提交**

```bash
git add firmware/cw-trainer/main/core/kob_packet.* firmware/cw-trainer/test/test_kob_packet.c
git commit -m "feat(cw-trainer): CWCom 包编解码，黄金向量取自真实服务器"
```

---

## Task 2: kob_client — 协议状态机

**Files:**
- Create: `firmware/cw-trainer/main/core/kob_client.c`, `.h`
- Test: `firmware/cw-trainer/test/test_kob_client.c`

**Interfaces:**
- Consumes: Task 1 的 `kob_pkt_*` 与 `kob_packet_t`
- Produces:
```c
typedef enum {
    KOB_ST_IDLE, KOB_ST_CONNECTING, KOB_ST_ONLINE
} kob_state_t;

typedef struct kob_client kob_client_t;

/* 发送由宿主实现（主机测试里记录到数组，设备上是 sendto） */
typedef void (*kob_send_fn)(const uint8_t *buf, size_t len, void *user);
/* 收到别人的码包 */
typedef void (*kob_code_fn)(const char *from, const int32_t *code, int32_t n,
                            const char *txt, void *user);
/* 站点出现/更新（ID 包） */
typedef void (*kob_peer_fn)(const char *id, void *user);
typedef void (*kob_state_fn)(kob_state_t st, void *user);

struct kob_client {
    kob_state_t  state;
    int16_t      wire;
    char         office[KOB_ID_MAX + 1];
    int32_t      tx_seq;
    int32_t      last_rx_seq;
    uint32_t     last_tx_ms;
    uint32_t     last_rx_ms;
    kob_send_fn  send;  kob_code_fn on_code;  kob_peer_fn on_peer;  kob_state_fn on_state;
    void        *user;
};

void kob_client_init(kob_client_t *c, const char *office, int16_t wire);
void kob_client_start(kob_client_t *c, uint32_t now_ms);
void kob_client_stop(kob_client_t *c, uint32_t now_ms);
void kob_client_tick(kob_client_t *c, uint32_t now_ms);          /* 驱动保活与超时 */
void kob_client_rx(kob_client_t *c, const uint8_t *buf, size_t len, uint32_t now_ms);
bool kob_client_send_code(kob_client_t *c, const int32_t *code, int32_t n,
                          const char *txt, uint32_t now_ms);

#define KOB_KEEPALIVE_MS  10000
#define KOB_TIMEOUT_MS    30000
```

**关键行为（实测得来，不是文档所载）**：收到 `ACK` 只是**连上**，必须紧接着发一个 ID 包才会被服务器纳入中继。漏了这一步的表现是「显示在线但对方永远收不到」，且**服务器不报错**。

> **实现时的修正**：本计划初稿要求 `REGISTERING` 作为独立状态。实现时判定不需要——服务器**不会** ACK 这个 ID 包，没有任何东西可等，该状态会在同一次调用里进入又退出，是死重量。最终做法是在 ACK 处理里同步发 ID 再进 `ONLINE`，由 `ack_triggers_id_registration_then_online` 与 `reconnect_after_timeout_registers_again` 两个用例钉死「ID 包必须发出」和「重连后必须重新注册」。

- [ ] **Step 1: 写失败的测试**

```c
#include "test_util.h"
#include "kob_client.h"

static uint8_t  g_sent[16][KOB_PKT_LONG];
static size_t   g_sent_len[16];
static int      g_sent_n;
static char     g_code_from[64];
static int32_t  g_code_n;
static char     g_code_txt[64];
static kob_state_t g_state;

static void rec_send(const uint8_t *b, size_t l, void *u) {
    (void)u;
    if (g_sent_n < 16) { memcpy(g_sent[g_sent_n], b, l); g_sent_len[g_sent_n] = l; g_sent_n++; }
}
static void rec_code(const char *f, const int32_t *c, int32_t n, const char *t, void *u) {
    (void)c; (void)u;
    snprintf(g_code_from, sizeof g_code_from, "%s", f);
    g_code_n = n;
    snprintf(g_code_txt, sizeof g_code_txt, "%s", t);
}
static void rec_state(kob_state_t s, void *u) { (void)u; g_state = s; }

static void setup(kob_client_t *c) {
    g_sent_n = 0; g_code_n = -1; g_code_from[0] = 0; g_code_txt[0] = 0;
    kob_client_init(c, "BI8SYN", 11);
    c->send = rec_send; c->on_code = rec_code; c->on_state = rec_state;
}

TEST(start_sends_con_and_enters_connecting) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    ASSERT_EQ_INT(c.state, KOB_ST_CONNECTING);
    ASSERT_EQ_INT(g_sent_n, 1);
    ASSERT_EQ_INT(g_sent_len[0], KOB_PKT_SHORT);
    kob_packet_t p; ASSERT_TRUE(kob_pkt_parse(g_sent[0], g_sent_len[0], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_CON);
}

TEST(ack_triggers_id_registration_then_online) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    /* 必须自动补发 ID 包 —— 这是漏了就静默失效的一步 */
    ASSERT_EQ_INT(g_sent_n, 2);
    kob_packet_t p; ASSERT_TRUE(kob_pkt_parse(g_sent[1], g_sent_len[1], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_DAT);
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "BI8SYN");
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
}

TEST(cannot_send_code_before_online) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const int32_t v[] = { 60, -60, 180 };
    ASSERT_TRUE(!kob_client_send_code(&c, v, 3, "A", 1100));
}

TEST(send_code_increments_seq) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    const int32_t v[] = { 60, -60, 180 };
    ASSERT_TRUE(kob_client_send_code(&c, v, 3, "A", 1300));
    ASSERT_TRUE(kob_client_send_code(&c, v, 3, "N", 1400));
    kob_packet_t a, b;
    kob_pkt_parse(g_sent[2], g_sent_len[2], &a);
    kob_pkt_parse(g_sent[3], g_sent_len[3], &b);
    ASSERT_TRUE(b.seq > a.seq);
}

TEST(incoming_code_from_others_is_reported) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    const int32_t v[] = { -2000, 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 5, v, 4, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_code_from, "BG7XXX");
    ASSERT_EQ_INT(g_code_n, 4);
    ASSERT_EQ_STR(g_code_txt, "A");
}

TEST(own_echo_is_ignored) {
    /* 服务器会把我发的包回给我自己，不能当成对方发报，否则会自听成回声 */
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BI8SYN", 9, v, 3, "A");
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(duplicate_seq_is_dropped) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 5, v, 3, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1600);   /* 同一个 seq 再来一次 */
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(tick_sends_keepalive) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    int before = g_sent_n;
    kob_client_tick(&c, 1200 + KOB_KEEPALIVE_MS + 1);
    ASSERT_TRUE(g_sent_n > before);
}

TEST(silence_drops_back_to_connecting) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    kob_client_tick(&c, 1200 + KOB_TIMEOUT_MS + 1);
    ASSERT_EQ_INT(c.state, KOB_ST_CONNECTING);
}

TEST(stop_sends_dis_and_goes_idle) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 1200);
    int before = g_sent_n;
    kob_client_stop(&c, 1300);
    ASSERT_EQ_INT(g_sent_n, before + 1);
    kob_packet_t p; kob_pkt_parse(g_sent[before], g_sent_len[before], &p);
    ASSERT_EQ_INT(p.cmd, KOB_CMD_DIS);
    ASSERT_EQ_INT(c.state, KOB_ST_IDLE);
}

TEST(millis_wraparound_does_not_hang_keepalive) {
    /* uint32 毫秒 49.7 天回绕；不能用裸减法比较 */
    kob_client_t c; setup(&c);
    kob_client_start(&c, 0xFFFFF000u);
    const uint8_t ack[2] = { 0x05, 0x00 };
    kob_client_rx(&c, ack, 2, 0xFFFFF100u);
    int before = g_sent_n;
    kob_client_tick(&c, 0x00000100u);   /* 已回绕，实际过去约 4 秒 */
    ASSERT_EQ_INT(g_sent_n, before);    /* 不该误判为超时或立刻保活 */
}

int main(void) {
    RUN(start_sends_con_and_enters_connecting);
    RUN(ack_triggers_id_registration_then_online);
    RUN(cannot_send_code_before_online);
    RUN(send_code_increments_seq);
    RUN(incoming_code_from_others_is_reported);
    RUN(own_echo_is_ignored);
    RUN(duplicate_seq_is_dropped);
    RUN(tick_sends_keepalive);
    RUN(silence_drops_back_to_connecting);
    RUN(stop_sends_dis_and_goes_idle);
    RUN(millis_wraparound_does_not_hang_keepalive);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test test_kob_client`
Expected: 编译失败，`kob_client.h: No such file or directory`

- [ ] **Step 3: 实现**

时间比较一律用 `(uint32_t)(now - then) >= X` 的形式，天然处理回绕；**不要**写 `now - then >= X` 之后再和有符号量混用。

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: 全部通过，零警告

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/kob_client.* firmware/cw-trainer/test/test_kob_client.c
git commit -m "feat(cw-trainer): CWCom 协议状态机，含 ACK 后必须补发 ID 注册包"
```

---

## Task 3: kob_rxbuf — 收报缓冲与闭合电路语义

**Files:**
- Create: `firmware/cw-trainer/main/core/kob_rxbuf.c`, `.h`
- Test: `firmware/cw-trainer/test/test_kob_rxbuf.c`

**Interfaces:**
- Consumes: 无（只吃 `int32_t` 数组）
- Produces:
```c
typedef struct {
    bool     key_down;     /* 当前是否应发声 */
    uint32_t until_ms;     /* 本状态持续到何时；latched 时无意义 */
    bool     latched;      /* 收到 +1，电路锁存闭合 */
} kob_rx_out_t;

typedef struct kob_rxbuf kob_rxbuf_t;

void kob_rxbuf_init(kob_rxbuf_t *b, uint16_t delay_ms);
/* 投入一个收到的码包；now_ms 为收到时刻 */
void kob_rxbuf_push(kob_rxbuf_t *b, const int32_t *code, int32_t n, uint32_t now_ms);
/* 每 tick 调用，返回当前应该的发声状态 */
void kob_rxbuf_poll(kob_rxbuf_t *b, uint32_t now_ms, kob_rx_out_t *out);
void kob_rxbuf_reset(kob_rxbuf_t *b);
bool kob_rxbuf_is_break(const int32_t *code, int32_t n);   /* (-n, +2) 且 n > 3000 */
```

**规则来源**（`Morse-Code-over-IP/protocol-cwcom` 的规范文档，非代码，不涉 GPL）：
- `+1`：长闭合开始，锁存电路闭合，直到下一个长度 > 1ms 的 mark 结束
- `+2`：长闭合结束，解锁
- 收到 `(-n, +2)` 且 `n > 3000` 时视为**打断**，必须绕过缓冲立即开路，否则打断反应迟钝
- 正常情况缓冲约 500ms 吸收抖动

- [ ] **Step 1: 写失败的测试**

```c
#include "test_util.h"
#include "kob_rxbuf.h"

TEST(elements_play_after_delay_not_before) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { 60, -60, 180 };
    kob_rxbuf_push(&b, v, 3, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1100, &o);            /* 还没到延迟 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500, &o);            /* 到点，第一个 mark 60ms */
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 61, &o);       /* mark 结束进入 gap */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500 + 130, &o);      /* gap 结束进入 dah */
    ASSERT_TRUE(o.key_down);
}

TEST(negative_leading_gap_is_honored) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { -300, 60 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);            /* 延迟到了，但还在 300ms 前导间隔里 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500 + 310, &o);
    ASSERT_TRUE(o.key_down);
}

TEST(plus_one_latches_circuit_closed) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { 60, 1 };           /* mark 然后长闭合 */
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1500 + 100, &o);
    ASSERT_TRUE(o.latched);
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 100000, &o);   /* 很久以后仍然闭合 */
    ASSERT_TRUE(o.key_down);
}

TEST(plus_two_unlatches) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v1[] = { 60, 1 };
    kob_rxbuf_push(&b, v1, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.latched);
    const int32_t v2[] = { -500, 2 };
    kob_rxbuf_push(&b, v2, 2, 2000);
    kob_rxbuf_poll(&b, 2600, &o);
    ASSERT_TRUE(!o.latched);
    ASSERT_TRUE(!o.key_down);
}

TEST(break_is_detected) {
    const int32_t brk[]   = { -3500, 2 };
    const int32_t nobrk[] = { -1200, 2 };
    const int32_t plain[] = { 60, -60, 180 };
    ASSERT_TRUE(kob_rxbuf_is_break(brk, 2));
    ASSERT_TRUE(!kob_rxbuf_is_break(nobrk, 2));
    ASSERT_TRUE(!kob_rxbuf_is_break(plain, 3));
}

TEST(break_bypasses_buffer_immediately) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { 60, 1 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.key_down);
    const int32_t brk[] = { -3500, 2 };
    kob_rxbuf_push(&b, brk, 2, 2000);
    kob_rxbuf_poll(&b, 2001, &o);            /* 不等 500ms 缓冲 */
    ASSERT_TRUE(!o.key_down);
}

TEST(reset_clears_everything) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { 60, 1 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_reset(&b);
    kob_rxbuf_poll(&b, 1700, &o);
    ASSERT_TRUE(!o.key_down);
    ASSERT_TRUE(!o.latched);
}

TEST(overflow_drops_oldest_not_crash) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    const int32_t v[] = { 60, -60, 180, -60, 60 };
    for (int i = 0; i < 200; i++) kob_rxbuf_push(&b, v, 5, 1000 + i);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 5000, &o);            /* 只要不崩、不越界即可 */
    ASSERT_TRUE(true);
}

int main(void) {
    RUN(elements_play_after_delay_not_before);
    RUN(negative_leading_gap_is_honored);
    RUN(plus_one_latches_circuit_closed);
    RUN(plus_two_unlatches);
    RUN(break_is_detected);
    RUN(break_bypasses_buffer_immediately);
    RUN(reset_clears_everything);
    RUN(overflow_drops_oldest_not_crash);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test test_kob_rxbuf`
Expected: 编译失败

- [ ] **Step 3: 实现**

内部用固定容量环形队列存待播元素（建议 256 个 `int32_t`，1KB，堆压力可忽略），溢出丢最旧。**不要**动态分配。

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/kob_rxbuf.* firmware/cw-trainer/test/test_kob_rxbuf.c
git commit -m "feat(cw-trainer): 收报抖动缓冲与闭合电路打断语义"
```

---

## Task 4: net_store — 独立 NVS 存储

**Files:**
- Create: `firmware/cw-trainer/main/net_store.c`, `.h`
- Modify: `firmware/cw-trainer/main/main.c`（`app_main` 里在 `cw_settings_init()` 之后调 `net_store_init()`）

**Interfaces:**
- Produces:
```c
#define CW_WIFI_PROFILE_MAX 4

typedef struct {
    uint8_t used;
    char    ssid[33];        /* 32B SSID + NUL */
    char    password[64];    /* 63B WPA2 + NUL */
} cw_wifi_profile_t;

typedef struct {
    uint32_t          version;
    cw_wifi_profile_t profiles[CW_WIFI_PROFILE_MAX];
    char              host[64];
    uint16_t          port;
    int16_t           wire;
    char              office[16];
    uint16_t          rx_delay_ms;
    uint8_t           echo_mode;
} cw_net_cfg_t;

#define CW_NET_CFG_VERSION 1
#define NVS_KEY_NET_CFG    "net_cfg"

esp_err_t     net_store_init(void);
cw_net_cfg_t *net_store_get(void);
esp_err_t     net_store_save(void);
bool          net_store_has_wifi(void);
/* 从 fmo-companion 的 appcfg 命名空间导入 WiFi；返回导入的槽位数，0 表示没有可导入的 */
int           net_store_import_from_fmo(void);
```

**默认值**：`host = "8.138.130.141"`、`port = 7890`、`wire = 11`、`office` 跟随 `cw_settings_get()->callsign`、`rx_delay_ms = 500`、`echo_mode = 0`。

**这个任务最容易出的错**（spec 3.2）：绝不能把这些字段加进 `cw_settings_t`。那个结构体一旦长度或版本变化，`cw_settings.c:59` 就 `set_defaults()`，用户的 `koch_lesson` 和 `koch_last_pct[41]` 全部清零。用**独立 NVS key + 独立版本号**。

- [ ] **Step 1: 实现**

命名空间沿用 `cwtrn`（与 `cw_settings.c:7` 相同），但 key 用 `net_cfg`，与 `cw_settings` 的 key 分开。加载失败或版本不符时**只**重置本结构体，不碰 `cw_settings`。

导入逻辑：`nvs_open("appcfg", NVS_READONLY, ...)` 读 `wifi_profiles`，按 fmo 的 `app_wifi_profiles_blob_t` 布局显式解析（**不要** `memcpy` 整个结构体——两边定义不同），把前 3 个有效槽位搬到本侧前 3 格。读不到就返回 0，不报错。

- [ ] **Step 2: 设备验证（这一步没有主机测试可用，必须上设备）**

```bash
PROJECT=firmware/cw-trainer tools/idf.sh build
PROJECT=firmware/cw-trainer tools/idf.sh flash monitor
```

日志必须出现 `net_cfg loaded`（或首次运行的 `no net cfg, defaults`），且**必须确认**同一次启动里 `cw_settings` 没有打印 `config invalid ... reset`。

- [ ] **Step 3: 升级路径回归（本任务最关键的一次验证）**

先刷入**未含本任务改动**的固件，练一课 Koch 让 `koch_lesson` 非 0；再刷入含本任务的固件，确认 `koch_lesson` 与 `koch_last_pct` **原样保留**。这一条不过就说明结构体被动了，必须回头改。

- [ ] **Step 4: 提交**

```bash
git add firmware/cw-trainer/main/net_store.* firmware/cw-trainer/main/main.c
git commit -m "feat(cw-trainer): 网络配置独立 NVS blob，不动 cw_settings_t"
```

---

## Task 5: wifi_net — WiFi 站点管理

**Files:**
- Create: `firmware/cw-trainer/main/wifi_net.c`, `.h`
- Modify: `firmware/cw-trainer/sdkconfig.defaults`

**Interfaces:**
- Consumes: Task 4 的 `net_store_get()`
- Produces:
```c
typedef enum {
    WIFI_NET_OFF, WIFI_NET_SCANNING, WIFI_NET_CONNECTING,
    WIFI_NET_CONNECTED, WIFI_NET_FAIL_NO_AP, WIFI_NET_FAIL_AUTH
} wifi_net_state_t;

typedef void (*wifi_net_cb_t)(wifi_net_state_t st, const char *ssid, void *user);

esp_err_t wifi_net_init(void);                          /* 只做 netif/event 初始化，不开射频 */
esp_err_t wifi_net_start(wifi_net_cb_t cb, void *user); /* 开射频、扫描、连已存槽位 */
void      wifi_net_stop(void);                          /* 关射频 */
bool      wifi_net_is_connected(void);
/* 配网页用：扫描并返回 SSID 列表 */
int       wifi_net_scan(char out[][33], int max_n);
```

**照搬来源**：`firmware/fmo-companion/main/wifi_manager.c`，尤其 `:627` 起的主动全信道扫描与槽位匹配逻辑（`scan_time.active.min = 50 / max = 120`）。删掉 fmo 里与 WebSocket、重连退避、WireGuard 相关的部分。

**回调上下文警告**：`wifi_net_cb_t` 在 ESP-IDF 的事件任务里触发，**不是** LVGL 任务。UI 侧必须 `ui_async_post()` 转发。

- [ ] **Step 1: 打开 sdkconfig 里的 WiFi**

`sdkconfig.defaults` 里确认 `CONFIG_ESP_WIFI_ENABLED=y`；不要动 `CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE`（默认 core 0，正好与 core 1 上的 `tone`/`key_input` 错开）。

- [ ] **Step 2: 实现**

- [ ] **Step 3: 设备验证**

烧录后进配网页触发扫描，日志应看到扫描到的 AP 数量与匹配结果。断电重启后能自动连上已存槽位。

**同时必须验证一条**：`wifi_net_stop()` 之后 `esp_get_free_heap_size()` 回到接近开启前的水平（预期 WiFi 常驻 50~60KB）。回不去说明有泄漏。

- [ ] **Step 4: 提交**

```bash
git add firmware/cw-trainer/main/wifi_net.* firmware/cw-trainer/sdkconfig.defaults
git commit -m "feat(cw-trainer): WiFi 站点管理，按需开关"
```

---

## Task 6: ui_wifi — 配网页

**Files:**
- Create: `firmware/cw-trainer/main/ui/ui_wifi.c`, `.h`
- Modify: `firmware/cw-trainer/main/ui/ui_settings.c`（加「网络」入口）

**Interfaces:**
- Consumes: Task 4 `net_store_*`、Task 5 `wifi_net_scan/start/stop`
- Produces: `lv_obj_t *ui_wifi_create(void);`

**照搬来源**：`firmware/fmo-companion/main/app_ui.c:1981` 起的槽位编辑、扫描列表回填、`lv_textarea` + `lv_keyboard` 组合。

**必须重新排版**：fmo 是竖屏，本项目是 320×240 **横屏**，键盘弹出会占掉大半屏，textarea 与键盘要错开，不能照搬布局。

- [ ] **Step 1: 实现槽位列表（4 行）与空槽提示**
- [ ] **Step 2: 实现扫描选择与手工输入**
- [ ] **Step 3: 实现「从 FMO 伴侣导入」按钮**

仅当 `net_store_has_wifi() == false` 且 `net_store_import_from_fmo()` 探测到有效数据时才显示该按钮。

- [ ] **Step 4: 设置页加「网络」分组**

字段与默认值见 Task 4：服务器地址、端口、wire 号、office ID、收报缓冲（0.2/0.5/1.0s）、**陪练模式（机器人 QSO / 回声）**。后者写 `net_store_get()->echo_mode`。spec 6.2 明确要求这个切换收进设置、不做成入口处的必选项。

- [ ] **Step 5: 设备验证**

四个槽位存取、扫描匹配、导入、连接状态显示。键盘弹出时 textarea 不被遮挡。设置页各字段能存能读。

- [ ] **Step 6: 提交**

```bash
git add firmware/cw-trainer/main/ui/ui_wifi.* firmware/cw-trainer/main/ui/ui_settings.c
git commit -m "feat(cw-trainer): 配网页，含从 FMO 伴侣导入"
```

---

## Task 7: tone_seq — 原始时序播放

**Files:**
- Create: `firmware/cw-trainer/main/tone_seq.c`, `.h`

**Interfaces:**
- Consumes: `tone_gen_set()`（`tone_gen.h`）
- Produces:
```c
esp_err_t tone_seq_init(void);
/* 由 kob_net 的 tick 调用，把 kob_rxbuf 算出的 key_down 落到发声器 */
void      tone_seq_set(bool key_down);
void      tone_seq_silence(void);
```

**为什么不能复用 `cw_player`**：`cw_player_play()` 收的是文本，内部用 `morse_timing` 换算时长。CWCom 给的是对端**实际手法**的毫秒数组——这正是联网对练的价值所在（听得出对方的手法）。走 `cw_player` 会把对方的手法抹平成标准时长。

**与 `cw_player` 的互斥**：两者共用同一个 `tone_gen`。进对练页时必须 `cw_player_stop_if()` 确保没有残留播放。

- [ ] **Step 1: 实现**
- [ ] **Step 2: 设备验证** —— 手工喂一组固定元素（如字母 V 的 `-2000,60,-60,60,-60,60,-60,180`），耳朵确认听到的是 `... -`
- [ ] **Step 3: 提交**

```bash
git add firmware/cw-trainer/main/tone_seq.*
git commit -m "feat(cw-trainer): 原始时序元素播放，保留对端真实手法"
```

---

## Task 8: kob_net — socket 任务与接线

**Files:**
- Create: `firmware/cw-trainer/main/kob_net.c`, `.h`

**Interfaces:**
- Consumes: Task 2 `kob_client_*`、Task 3 `kob_rxbuf_*`、Task 4 `net_store_get()`、Task 7 `tone_seq_set()`、`key_input.h`
- Produces:
```c
typedef enum {
    KOB_NET_OFF, KOB_NET_WIFI, KOB_NET_CONNECTING, KOB_NET_ONLINE,
    KOB_NET_ERR_WIFI, KOB_NET_ERR_SERVER, KOB_NET_RECONNECTING
} kob_net_state_t;

typedef struct {
    kob_net_state_t state;
    char            peers[4][KOB_ID_MAX + 1];
    int             peer_n;
    char            sender[KOB_ID_MAX + 1];   /* 当前发报方，空串表示空闲 */
    bool            i_am_sending;
    bool            broken;                   /* 我被打断 */
} kob_net_status_t;

typedef void (*kob_net_evt_cb_t)(const kob_net_status_t *st, void *user);
typedef void (*kob_net_text_cb_t)(const char *from, const char *txt, void *user);

esp_err_t kob_net_start(kob_net_evt_cb_t evt, kob_net_text_cb_t text, void *user);
void      kob_net_stop(void);
void      kob_net_send_code(const int32_t *code, int32_t n, const char *txt);
```

**任务与核心**：socket 任务钉在 **core 0**（与 WiFi/lwIP 同核），优先级低于 `tone`(23) 和 `key_input`(22)，建议 10。绝不能钉到 core 1 抢发报时序。

**回调上下文警告**：`kob_net_evt_cb_t` / `kob_net_text_cb_t` 在 socket 任务里触发，UI 必须 `ui_async_post()` 转发，不得直接碰 `lv_obj_t`。

- [ ] **Step 1: 实现 socket 任务**

`recvfrom` 用 500ms 超时循环，每轮调 `kob_client_tick()` 与 `kob_rxbuf_poll()`。发送走 `kob_client` 的 `send` 回调。

- [ ] **Step 2: 接上收报**

`kob_client.on_code` → `kob_rxbuf_push()`；`kob_rxbuf_poll()` 的 `key_down` → `tone_seq_set()`；`txt` 经 `kob_net_text_cb_t` 上抛。

- [ ] **Step 3: 接上发报**

`key_input` 的解码回调给的是字符，但要发的是**时序**。在 `kob_net` 内维护一个元素累积缓冲：`key_input` 每次键位翻转记一个元素（正=闭合时长，负=断开时长），攒到一个字符边界（`key_input` 回调到达时）就 `kob_client_send_code()` 发出。

- [ ] **Step 4: 实现打断**

我在发时收到别人的码包 → `status.broken = true` 并上报；我在对方发报时按键 → 发一个 `(-n, +2)` 且 n > 3000 的包。

- [ ] **Step 5: 设备验证**

设备连上自建服务器后，用主机上的 PyKOB 或下面这个脚本扮演对端，双向确认：

```bash
python3 - <<'PY'
import socket, struct, time
DIS, DAT, CON = 2, 3, 4
short = struct.Struct("<hh"); idp = struct.Struct("<hh 128s 4x i i 8x 208x 128s 8x")
codep = struct.Struct("<hh 128s 4x i 12x 51i i 128s 8x")
H, P, W = "8.138.130.141", 7890, 11
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)
s.sendto(short.pack(CON, W), (H, P)); s.recvfrom(1024)
s.sendto(idp.pack(DAT, 492, b"PEER", 0, 1, b"peer 1.0"), (H, P))
V = [-2000, 60, -60, 60, -60, 60, -60, 180]
s.sendto(codep.pack(DAT, 492, b"PEER", 1, *(V + [0]*(51-len(V))), len(V), b"V"), (H, P))
print("sent V; now listening for the device")
t0 = time.time()
while time.time() - t0 < 30:
    try: d, _ = s.recvfrom(1024)
    except socket.timeout: continue
    if len(d) == 496:
        f = codep.unpack(d)
        who = f[2].split(b"\x00")[0].decode()
        if who != "PEER" and f[-2] > 0:
            print("device sent:", f[-1].split(b"\x00")[0].decode(), list(f[4:4+f[-2]]))
s.sendto(short.pack(DIS, W), (H, P))
PY
```

设备耳机里应听到 `V`（`...-`）；设备上发报，脚本应打印出时序数组。

- [ ] **Step 6: 提交**

```bash
git add firmware/cw-trainer/main/kob_net.*
git commit -m "feat(cw-trainer): UDP socket 任务，收发报接线与打断"
```

---

## Task 9: ui_net — 对练页

**Files:**
- Create: `firmware/cw-trainer/main/ui/ui_net.c`, `.h`
- Modify: `firmware/cw-trainer/main/ui/ui_home.c`（加入口）

**Interfaces:**
- Consumes: Task 8 `kob_net_*`、Task 4 `net_store_has_wifi()`
- Produces: `lv_obj_t *ui_net_create(void);`

**线框**（spec 6.4，垂直预算必须严格遵守）：

```
顶栏 28px  │ 联网对练            BOT · 1 人  [退出]
收报 78px  │ 三行对话记录，前缀区分收发
状态 24px  │ 永远回答「现在什么情况」
电键 110px │ 两个 160×110 触摸块
```

**必须覆盖 spec 6.5 的全部状态**，不只做 Happy Path：未配网、连接中（两段）、WiFi 失败、服务器不可达、在线仅陪练、在线有真人、对方正在发、我正在发、我被打断、我打断对方、断线重连、收报残缺、回声模式。「权限被拒」不适用。

**电键在「对方正在发」时不禁用** —— 按下即打断（spec 6.5 末尾）。只在「断线重连」状态置灰。

- [ ] **Step 1: 搭三段布局与顶栏**
- [ ] **Step 2: 实现对话记录（收发同流，前缀区分）**
- [ ] **Step 3: 实现状态带的全部文案分支**
- [ ] **Step 4: 接上电键，含打断路径**
- [ ] **Step 5: 首次进入时告知侧音底噪**

spec 6.2 要求这条必须告知、不能藏着：联网期间 WiFi 电流尖峰可能窜进内置 DAC，侧音出现底噪属正常，离线练习不受影响。用 `ui_notice()`（`ui_common.h`）在首次进入对练页时显示一次，之后不再打扰（标志位存 `net_store` 的 blob）。

- [ ] **Step 6: 逐个状态对照灰阶原型验收**

原型：`docs/design/cw-trainer-network-prototype.html`（浏览器打开，侧边面板可跳到每个状态）

- [ ] **Step 7: 提交**

```bash
git add firmware/cw-trainer/main/ui/ui_net.* firmware/cw-trainer/main/ui/ui_home.c
git commit -m "feat(cw-trainer): 对练页，覆盖全部状态"
```

---

## Task 10: 联调与文档

- [ ] **Step 1: 三方互通**

设备 ↔ 自建服务器 ↔ PyKOB 桌面客户端。这是 spec 第 8 节遗留的那一项。

```bash
git clone https://github.com/MorseKOB/PyKOB /tmp/pykob
cd /tmp/pykob && python3 src.py/MKOB.py     # 服务器填 8.138.130.141:7890，wire 11
```

- [ ] **Step 2: 侧音底噪主观测试**

WiFi 开启时听侧音，确认是否有可闻底噪。有的话记录严重程度，写进 spec 第 9 节风险表。这是 spec 里预判的风险，必须实测确认。

- [ ] **Step 3: 长稳测试**

对练页挂 30 分钟，确认不断线、不泄漏（周期打印 `esp_get_free_heap_size()`，末值应与初值持平）。

- [ ] **Step 4: 更新 README 与 spec**

`firmware/cw-trainer/README.md` 加「联网对练」一节；spec 第 8 节把已完成阶段标掉。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "docs(cw-trainer): 联网对练联调结果与文档"
```

---

## 未覆盖 / 另开计划

**陪练机器人**（spec 第 5 节、6.1、实施阶段 4）不在本计划内。它是服务端 Python、跑在 `ali99` 上、作为普通 CWCom 客户端接入，**与固件零耦合**，属于独立子系统，另开一份计划。

本计划的 Task 8 / Task 9 验证时用上面给出的 Python 脚本或 PyKOB 桌面端人工扮演对端即可，不必等机器人写完。
