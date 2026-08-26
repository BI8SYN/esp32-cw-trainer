# CW Trainer 固件实现计划（阶段 1：灰阶）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `firmware/cw-trainer` 建一个独立的 ESP-IDF + LVGL 8 固件，实现 spec `docs/superpowers/specs/2026-08-25-cw-trainer-design.md` 的全部 MVP 功能（Koch 听写、呼号/QSO 抄收、触摸桨电键 + 侧音 + 实时解码、设置持久化），UI 为黑白灰灰阶版。

**Architecture:** 核心逻辑（莫尔斯表、时序、电键状态机、解码器、Koch 引擎、呼号生成、练习会话）全部是无 IDF 依赖的纯 C，放在 `main/core/`，用 `test/` 下的主机测试（`make test`）驱动开发。设备层（`tone_gen` I2S DAC 侧音、`cw_player` esp_timer 发报、`key_input`、`cw_settings` NVS、`battery`、`backlight`）和 LVGL 页面（`main/ui/`）只做粘合与显示。

**Tech Stack:** ESP-IDF v5.5.4，LVGL 8（复用 `firmware/fmo-companion/components/` 下的 lvgl / lvgl_esp32_drivers / lv_porting），legacy I2S 内置 DAC 驱动（`driver/i2s.h`），legacy ADC（`driver/adc.h` + `esp_adc_cal.h`），主机测试用系统 `cc`。

## Global Constraints

- 屏幕 320×240 横屏（`sdkconfig.defaults` 已是 `CONFIG_LV_DISPLAY_ORIENTATION_LANDSCAPE=y`，`LV_HOR_RES_MAX=320`），不改方向。
- 阶段 1 只允许黑、白、灰：不设置任何非灰色 `lv_color`，不加圆角/阴影/动画（`CONFIG_LV_THEME_DEFAULT_TRANSITION_TIME` 保持，但不额外写动画）。
- 字体只用现有 `font20`（含中文，line_height 26）、`font28`（line_height 37）、`EN_BIG`（大号英数，line_height 82）。
- **字形限制（已实测）**：`font28` / `EN_BIG` 只含可打印 ASCII（无中文）；`font20` 含全部 ASCII + 3791 个汉字。三者都**没有** `…` `≥` `♪` `·` `−` 等符号，也没有 `LV_SYMBOL_*` 图标字形。界面文字只能用 ASCII + 汉字：省略号写 `...`，"大于等于"写"以上"，点划用 ASCII 的 `.` 和 `-`。中文只能用 `font20`（`UI_FONT_CN`）；`UI_FONT_MID`/`UI_FONT_BIG` 只能放 ASCII。
- 完全离线：不编译 WiFi / BT / WebSocket；`idf_component.yml` 不含 `esp_websocket_client`。
- 侧音延迟目标 <15ms：I2S DMA 缓冲总深度 ≤ 12ms。
- NVS 命名空间 `cwtrn`，与 fmo-companion 的 `appcfg` 隔离。
- 所有 `main/core/*.c` 文件**不得** include 任何 `esp_*`、`freertos/*`、`driver/*`、`lvgl` 头。
- 构建命令：`PROJECT=firmware/cw-trainer tools/idf.sh build`；主机测试：`make -C firmware/cw-trainer/test`。
- 提交信息用中文，风格同仓库历史（如 `CW：新增莫尔斯表`）。
- 所有 LVGL 对象操作只能在 LVGL 主循环上下文或 `lv_async_call()` 回调里执行；esp_timer / 其它任务回调必须经 `ui_async_post()` 投递。

## 文件结构

```
firmware/cw-trainer/
  CMakeLists.txt                 project(cw-trainer)
  partitions.csv                 复制自 fmo-companion
  sdkconfig.defaults             复制自 fmo-companion，去掉 BT/WiFi 相关项不必要（保持原样即可编译）
  components/                    复制自 fmo-companion（lvgl, lvgl_esp32_drivers, lv_porting）
  main/
    CMakeLists.txt
    board_config.h               GPIO 定义（只保留本项目用到的）
    main.c                       启动顺序
    fonts/font20.c font28.c EN_BIG.c   复制
    core/                        纯 C，无 IDF 依赖
      rng.h                      xorshift32
      morse_table.h/.c           字符 ↔ 点划
      morse_timing.h/.c          WPM/Farnsworth → ms
      keyer.h/.c                 电键状态机
      cw_decoder.h/.c            键控时序 → 字符
      koch_engine.h/.c           课程表、分组、达标
      callsign_gen.h/.c          呼号与干扰项
      trainer.h/.c               练习会话状态机
    tone_gen.h/.c                I2S DAC 侧音
    cw_player.h/.c               文本 → 定时 tone on/off
    key_input.h/.c               触摸桨 / 外接口 → keyer 输入
    cw_settings.h/.c             NVS
    battery.h/.c                 ADC 电量
    backlight.h/.c               LEDC 背光
    ui_async.h/.c                任意上下文 → LVGL 上下文
    ui/
      ui_common.h/.c             顶栏、按钮、导航、字体
      ui_home.h/.c
      ui_preview.h/.c
      ui_practice.h/.c           Koch / 呼号 / QSO 三模式
      ui_result.h/.c
      ui_keyer.h/.c
      ui_settings.h/.c           分组列表 + 数值调整 + 单选 + 恢复出厂
  test/
    Makefile
    test_util.h                  极简断言宏
    test_morse_table.c test_morse_timing.c test_keyer.c test_cw_decoder.c
    test_koch_engine.c test_callsign_gen.c test_trainer.c
```

---

### Task 1: 工程骨架 + 主机测试框架

**Files:**
- Create: `firmware/cw-trainer/CMakeLists.txt`, `firmware/cw-trainer/main/CMakeLists.txt`, `firmware/cw-trainer/main/main.c`, `firmware/cw-trainer/main/board_config.h`, `firmware/cw-trainer/test/Makefile`, `firmware/cw-trainer/test/test_util.h`
- Copy: `firmware/fmo-companion/{partitions.csv,sdkconfig.defaults}` → `firmware/cw-trainer/`；`firmware/fmo-companion/components/` → `firmware/cw-trainer/components/`；`firmware/fmo-companion/main/fonts/{font20.c,font28.c,EN_BIG.c}` → `firmware/cw-trainer/main/fonts/`
- Modify: `.gitignore`（确认 `build/` 已忽略）

**Interfaces:**
- Produces: 可编译的空固件（屏幕显示 "CW Trainer"）；`make -C firmware/cw-trainer/test` 跑通空测试集。`test_util.h` 提供 `TEST(name)`、`ASSERT_TRUE(x)`、`ASSERT_EQ_INT(a,b)`、`ASSERT_EQ_STR(a,b)`、`RUN_TESTS()`。

- [ ] **Step 1: 复制骨架文件**

```bash
cd /Users/liyongsheng/projects/esp32-companion
mkdir -p firmware/cw-trainer/main/fonts firmware/cw-trainer/main/core firmware/cw-trainer/main/ui firmware/cw-trainer/test
cp firmware/fmo-companion/partitions.csv firmware/fmo-companion/sdkconfig.defaults firmware/cw-trainer/
cp -R firmware/fmo-companion/components firmware/cw-trainer/components
cp firmware/fmo-companion/main/fonts/font20.c firmware/fmo-companion/main/fonts/font28.c firmware/fmo-companion/main/fonts/EN_BIG.c firmware/cw-trainer/main/fonts/
grep -q '^build/$\|^\*\*/build/' .gitignore || echo '**/build/' >> .gitignore
```

- [ ] **Step 2: 写 CMakeLists**

`firmware/cw-trainer/CMakeLists.txt`：
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cw-trainer)
```

`firmware/cw-trainer/main/CMakeLists.txt`（后续任务往 SRCS 追加）：
```cmake
idf_component_register(
    SRCS
        "main.c"
        "fonts/font20.c"
        "fonts/font28.c"
        "fonts/EN_BIG.c"
    INCLUDE_DIRS "." "fonts" "core" "ui"
    REQUIRES
        driver
        esp_driver_i2s
        esp_timer
        nvs_flash
        esp_adc
        lvgl
        lv_porting
        lvgl_esp32_drivers
)
```

- [ ] **Step 3: 写 board_config.h 与最小 main.c**

`firmware/cw-trainer/main/board_config.h`：
```c
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
#include "driver/gpio.h"

#define BOARD_LCD_BL_GPIO        GPIO_NUM_21
#define BOARD_AUDIO_DAC_GPIO     GPIO_NUM_26   /* DAC2，I2S 内置 DAC */
#define BOARD_AUDIO_EN_GPIO      GPIO_NUM_4
#define BOARD_AUDIO_EN_ACTIVE    0             /* 低电平开功放 */
#define BOARD_AUDIO_MUTE_LEVEL   1
#define BOARD_KEY_DIT_GPIO       GPIO_NUM_22   /* CN3 外接口，预留 */
#define BOARD_KEY_DAH_GPIO       GPIO_NUM_27   /* CN3 外接口，预留 */
#define BOARD_BATTERY_ADC_CH     ADC1_CHANNEL_6 /* GPIO34 */

#endif
```

`firmware/cw-trainer/main/main.c`：
```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

static const char *TAG = "main";
LV_FONT_DECLARE(font20);

static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    nvs_init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    const esp_timer_create_args_t tick_args = { .callback = &lv_tick_task, .name = "lv_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &font20, 0);
    lv_label_set_text(label, "CW Trainer");
    lv_obj_center(label);
    ESP_LOGI(TAG, "ui ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
```

- [ ] **Step 4: 编译固件**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build`
Expected: 末尾出现 `Project build complete.`；若 `lv_porting` 组件因为找不到 `lvgl_helpers.h` 报错，检查 `components/` 是否完整复制。

- [ ] **Step 5: 写主机测试框架**

`firmware/cw-trainer/test/test_util.h`：
```c
#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_tests_run = 0, g_tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { g_tests_run++; printf("  %s\n", #name); name(); } while (0)
#define FAIL(msg) do { g_tests_failed++; printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return; } while (0)
#define ASSERT_TRUE(x) do { if (!(x)) FAIL("expected true: " #x); } while (0)
#define ASSERT_EQ_INT(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a != _b) { printf("    got %ld, want %ld\n", _a, _b); FAIL(#a " != " #b); } } while (0)
#define ASSERT_EQ_STR(a, b) do { const char *_a = (a), *_b = (b); if (!_a || !_b || strcmp(_a, _b) != 0) { printf("    got \"%s\", want \"%s\"\n", _a ? _a : "(null)", _b ? _b : "(null)"); FAIL(#a " != " #b); } } while (0)
#define TEST_MAIN_END() do { printf("%d tests, %d failed\n", g_tests_run, g_tests_failed); return g_tests_failed ? 1 : 0; } while (0)

#endif
```

`firmware/cw-trainer/test/Makefile`：
```make
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -I../main/core -I.
CORE_SRCS = $(wildcard ../main/core/*.c)
TESTS = $(patsubst %.c,%,$(wildcard test_*.c))

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

test_%: test_%.c $(CORE_SRCS) test_util.h
	$(CC) $(CFLAGS) -o $@ $< $(CORE_SRCS)

clean:
	rm -f $(TESTS)
```

- [ ] **Step 6: 跑空测试集**

Run: `make -C firmware/cw-trainer/test`
Expected: 没有 `test_*.c` 时 `for` 循环体不执行，命令成功退出（exit 0）。

- [ ] **Step 7: 提交**

```bash
git add firmware/cw-trainer .gitignore
git commit -m "CW：新建 cw-trainer 工程骨架与主机测试框架"
```

---

### Task 2: morse_table

**Files:**
- Create: `firmware/cw-trainer/main/core/morse_table.h`, `firmware/cw-trainer/main/core/morse_table.c`
- Test: `firmware/cw-trainer/test/test_morse_table.c`

**Interfaces:**
- Produces:
  - `const char *morse_table_lookup(char c);` — 输入 ASCII（大小写不敏感），返回 `"."`/`"-"` 组成的字符串，未知返回 `NULL`。支持 A–Z、0–9、`.` `,` `?` `/` `=`（BT）`+`（AR）。
  - `char morse_table_decode(const char *pattern);` — 输入点划串，返回大写字符；未知返回 `'\0'`。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_morse_table.c`：
```c
#include "test_util.h"
#include "morse_table.h"

TEST(lookup_letters_and_digits)
{
    ASSERT_EQ_STR(morse_table_lookup('K'), "-.-");
    ASSERT_EQ_STR(morse_table_lookup('m'), "--");
    ASSERT_EQ_STR(morse_table_lookup('5'), ".....");
    ASSERT_EQ_STR(morse_table_lookup('0'), "-----");
    ASSERT_EQ_STR(morse_table_lookup('?'), "..--..");
    ASSERT_EQ_STR(morse_table_lookup('='), "-...-");
    ASSERT_TRUE(morse_table_lookup('#') == NULL);
    ASSERT_TRUE(morse_table_lookup(' ') == NULL);
}

TEST(decode_roundtrip)
{
    const char *all = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=+";
    for (const char *p = all; *p; p++) {
        const char *pat = morse_table_lookup(*p);
        ASSERT_TRUE(pat != NULL);
        ASSERT_EQ_INT(morse_table_decode(pat), *p);
    }
    ASSERT_EQ_INT(morse_table_decode("........"), 0);
    ASSERT_EQ_INT(morse_table_decode(""), 0);
}

int main(void)
{
    RUN(lookup_letters_and_digits);
    RUN(decode_roundtrip);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误 `morse_table.h: No such file or directory`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/morse_table.h`：
```c
#ifndef MORSE_TABLE_H
#define MORSE_TABLE_H

/** 返回字符的点划串（"." / "-"），未知字符返回 NULL。大小写不敏感。 */
const char *morse_table_lookup(char c);

/** 点划串 → 大写字符，未知返回 '\0'。 */
char morse_table_decode(const char *pattern);

#endif
```

`firmware/cw-trainer/main/core/morse_table.c`：
```c
#include "morse_table.h"
#include <string.h>

typedef struct { char c; const char *pat; } entry_t;

static const entry_t TABLE[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
    {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'=', "-...-"}, {'+', ".-.-."},
};

const char *morse_table_lookup(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (TABLE[i].c == c) return TABLE[i].pat;
    }
    return NULL;
}

char morse_table_decode(const char *pattern)
{
    if (!pattern || !*pattern) return '\0';
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (strcmp(TABLE[i].pat, pattern) == 0) return TABLE[i].c;
    }
    return '\0';
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `2 tests, 0 failed`。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/morse_table.* firmware/cw-trainer/test/test_morse_table.c
git commit -m "CW：新增莫尔斯表"
```

---

### Task 3: morse_timing（WPM / Farnsworth）

**Files:**
- Create: `firmware/cw-trainer/main/core/morse_timing.h`, `firmware/cw-trainer/main/core/morse_timing.c`
- Test: `firmware/cw-trainer/test/test_morse_timing.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct {
      uint16_t dit_ms;        /* 点 */
      uint16_t dah_ms;        /* 划 = 3 dit */
      uint16_t intra_gap_ms;  /* 字符内元素间隔 = 1 dit */
      uint16_t char_gap_ms;   /* 字符间总间隔（Farnsworth 拉长） */
      uint16_t word_gap_ms;   /* 词间总间隔 */
  } morse_timing_t;
  void morse_timing_compute(uint8_t char_wpm, uint8_t eff_wpm, morse_timing_t *out);
  ```
  规则：`dit = 1200 / char_wpm`。若 `eff_wpm >= char_wpm` 则 `char_gap = 3 dit`、`word_gap = 7 dit`。否则按 ARRL Farnsworth：`ta_ms = (60000*c - 37200*s) / (s*c)`，`char_gap = 3*ta/19`，`word_gap = 7*ta/19`。参数被钳到 5..60。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_morse_timing.c`：
```c
#include "test_util.h"
#include "morse_timing.h"

TEST(standard_20wpm)
{
    morse_timing_t t;
    morse_timing_compute(20, 20, &t);
    ASSERT_EQ_INT(t.dit_ms, 60);
    ASSERT_EQ_INT(t.dah_ms, 180);
    ASSERT_EQ_INT(t.intra_gap_ms, 60);
    ASSERT_EQ_INT(t.char_gap_ms, 180);
    ASSERT_EQ_INT(t.word_gap_ms, 420);
}

TEST(farnsworth_20_8)
{
    morse_timing_t t;
    morse_timing_compute(20, 8, &t);
    ASSERT_EQ_INT(t.dit_ms, 60);
    /* ta = (60000*20 - 37200*8)/(8*20) = (1200000-297600)/160 = 5640 ms */
    ASSERT_EQ_INT(t.char_gap_ms, 5640 * 3 / 19);   /* 890 */
    ASSERT_EQ_INT(t.word_gap_ms, 5640 * 7 / 19);   /* 2077 */
}

TEST(eff_greater_than_char_uses_standard)
{
    morse_timing_t t;
    morse_timing_compute(15, 30, &t);
    ASSERT_EQ_INT(t.dit_ms, 80);
    ASSERT_EQ_INT(t.char_gap_ms, 240);
}

TEST(clamps)
{
    morse_timing_t t;
    morse_timing_compute(0, 0, &t);
    ASSERT_EQ_INT(t.dit_ms, 240);   /* 5 wpm */
    morse_timing_compute(200, 200, &t);
    ASSERT_EQ_INT(t.dit_ms, 20);    /* 60 wpm */
}

int main(void)
{
    RUN(standard_20wpm);
    RUN(farnsworth_20_8);
    RUN(eff_greater_than_char_uses_standard);
    RUN(clamps);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `morse_timing.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/morse_timing.h`：
```c
#ifndef MORSE_TIMING_H
#define MORSE_TIMING_H
#include <stdint.h>

typedef struct {
    uint16_t dit_ms;
    uint16_t dah_ms;
    uint16_t intra_gap_ms;
    uint16_t char_gap_ms;
    uint16_t word_gap_ms;
} morse_timing_t;

#define MORSE_WPM_MIN 5
#define MORSE_WPM_MAX 60

/** 由字符速度与有效速度（Farnsworth）计算各时长。参数钳到 5..60。 */
void morse_timing_compute(uint8_t char_wpm, uint8_t eff_wpm, morse_timing_t *out);

#endif
```

`firmware/cw-trainer/main/core/morse_timing.c`：
```c
#include "morse_timing.h"

static uint8_t clamp_wpm(uint8_t w)
{
    if (w < MORSE_WPM_MIN) return MORSE_WPM_MIN;
    if (w > MORSE_WPM_MAX) return MORSE_WPM_MAX;
    return w;
}

void morse_timing_compute(uint8_t char_wpm, uint8_t eff_wpm, morse_timing_t *out)
{
    uint32_t c = clamp_wpm(char_wpm);
    uint32_t s = clamp_wpm(eff_wpm);
    uint32_t dit = 1200 / c;

    out->dit_ms = (uint16_t)dit;
    out->dah_ms = (uint16_t)(dit * 3);
    out->intra_gap_ms = (uint16_t)dit;

    if (s >= c) {
        out->char_gap_ms = (uint16_t)(dit * 3);
        out->word_gap_ms = (uint16_t)(dit * 7);
        return;
    }
    uint32_t ta = (60000UL * c - 37200UL * s) / (s * c);
    out->char_gap_ms = (uint16_t)(ta * 3 / 19);
    out->word_gap_ms = (uint16_t)(ta * 7 / 19);
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `test_morse_timing` 报 `4 tests, 0 failed`。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/morse_timing.* firmware/cw-trainer/test/test_morse_timing.c
git commit -m "CW：新增 WPM/Farnsworth 时序计算"
```

---

### Task 4: keyer 电键状态机

**Files:**
- Create: `firmware/cw-trainer/main/core/keyer.h`, `firmware/cw-trainer/main/core/keyer.c`
- Test: `firmware/cw-trainer/test/test_keyer.c`

**Interfaces:**
- Produces:
  ```c
  typedef enum { KEYER_MODE_STRAIGHT = 0, KEYER_MODE_IAMBIC_A = 1, KEYER_MODE_IAMBIC_B = 2 } keyer_mode_t;
  typedef struct {
      keyer_mode_t mode;
      uint16_t dit_ms;
      bool dit_in, dah_in;        /* 当前输入电平 */
      bool key_out;               /* 当前输出（true = 发音） */
      int state;                  /* 内部 */
      int cur_el;                 /* 0 dit 1 dah，内部 */
      bool mem_dit, mem_dah;      /* 桨键记忆 */
      uint32_t el_end_ms;         /* 内部 */
  } keyer_t;
  void keyer_init(keyer_t *k, keyer_mode_t mode, uint16_t dit_ms);
  void keyer_set_input(keyer_t *k, bool dit, bool dah);   /* 直键模式只看 dit */
  bool keyer_tick(keyer_t *k, uint32_t now_ms);           /* 每 1~10ms 调用，返回 key_out */
  ```
  行为：
  - 直键：`key_out == dit_in`。
  - Iambic：空闲时按 dit 发 dit、按 dah 发 dah；元素结束后插 1 dit 间隔；间隔结束时：两桨都按 → 交替；一桨按 → 该元素；都松 → Iambic B 若在元素/间隔期间**对侧**桨曾按下（记忆），补发一个对侧元素；Iambic A 不补发。
  - 记忆只记录"当前元素的对侧桨"。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_keyer.c`：
```c
#include "test_util.h"
#include "keyer.h"

/* 把 keyer 从 t0 运行到 t1（含），返回输出为 true 的毫秒数 */
static int run_count_on(keyer_t *k, uint32_t t0, uint32_t t1)
{
    int on = 0;
    for (uint32_t t = t0; t <= t1; t++) if (keyer_tick(k, t)) on++;
    return on;
}

TEST(straight_key_follows_input)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_STRAIGHT, 60);
    keyer_set_input(&k, true, false);
    ASSERT_TRUE(keyer_tick(&k, 0));
    keyer_set_input(&k, false, false);
    ASSERT_TRUE(!keyer_tick(&k, 1));
}

TEST(iambic_single_dit_then_gap)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, true, false);
    keyer_tick(&k, 0);
    keyer_set_input(&k, false, false);   /* 立刻松开，仍应发完整 1 个 dit */
    int on = run_count_on(&k, 1, 300);
    ASSERT_EQ_INT(on, 59);   /* t=0..59 共 60ms 为 on，t=0 已在上面 tick 过 */
    ASSERT_TRUE(!k.key_out);
}

TEST(iambic_held_dah_repeats)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, false, true);
    /* dah 180 + gap 60 = 240ms 周期；持续 480ms 应有 2 个 dah = 360ms on */
    int on = run_count_on(&k, 0, 479);
    ASSERT_EQ_INT(on, 360);
}

TEST(iambic_squeeze_alternates)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_A, 60);
    keyer_set_input(&k, true, true);
    /* dit(60)+gap(60)+dah(180)+gap(60) = 360ms 周期，on = 240 */
    int on = run_count_on(&k, 0, 359);
    ASSERT_EQ_INT(on, 240);
    ASSERT_EQ_INT(k.cur_el, 1);   /* 刚发完 dah */
}

TEST(iambic_b_sends_memorized_opposite)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, false, true);      /* 开始 dah */
    run_count_on(&k, 0, 50);
    keyer_set_input(&k, true, true);       /* dah 期间按下 dit */
    run_count_on(&k, 51, 100);
    keyer_set_input(&k, false, false);     /* 全部松开 */
    int on = run_count_on(&k, 101, 600);
    /* 剩余 dah 79ms(101..179) + 记忆 dit 60ms = 139 */
    ASSERT_EQ_INT(on, 139);
}

TEST(iambic_a_does_not_send_memorized)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_A, 60);
    keyer_set_input(&k, false, true);
    run_count_on(&k, 0, 50);
    keyer_set_input(&k, true, true);
    run_count_on(&k, 51, 100);
    keyer_set_input(&k, false, false);
    int on = run_count_on(&k, 101, 600);
    ASSERT_EQ_INT(on, 79);
}

int main(void)
{
    RUN(straight_key_follows_input);
    RUN(iambic_single_dit_then_gap);
    RUN(iambic_held_dah_repeats);
    RUN(iambic_squeeze_alternates);
    RUN(iambic_b_sends_memorized_opposite);
    RUN(iambic_a_does_not_send_memorized);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `keyer.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/keyer.h`：
```c
#ifndef KEYER_H
#define KEYER_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KEYER_MODE_STRAIGHT = 0,
    KEYER_MODE_IAMBIC_A = 1,
    KEYER_MODE_IAMBIC_B = 2,
} keyer_mode_t;

enum { KEYER_ST_IDLE = 0, KEYER_ST_ELEMENT = 1, KEYER_ST_GAP = 2 };
enum { KEYER_EL_DIT = 0, KEYER_EL_DAH = 1 };

typedef struct {
    keyer_mode_t mode;
    uint16_t dit_ms;
    bool dit_in, dah_in;
    bool key_out;
    int state;
    int cur_el;
    bool mem_dit, mem_dah;
    uint32_t el_end_ms;
} keyer_t;

void keyer_init(keyer_t *k, keyer_mode_t mode, uint16_t dit_ms);
void keyer_set_input(keyer_t *k, bool dit, bool dah);
/** 每 1~10ms 调用一次。返回当前输出电平。 */
bool keyer_tick(keyer_t *k, uint32_t now_ms);

#endif
```

`firmware/cw-trainer/main/core/keyer.c`：
```c
#include "keyer.h"

void keyer_init(keyer_t *k, keyer_mode_t mode, uint16_t dit_ms)
{
    k->mode = mode;
    k->dit_ms = dit_ms ? dit_ms : 60;
    k->dit_in = k->dah_in = false;
    k->key_out = false;
    k->state = KEYER_ST_IDLE;
    k->cur_el = KEYER_EL_DIT;
    k->mem_dit = k->mem_dah = false;
    k->el_end_ms = 0;
}

void keyer_set_input(keyer_t *k, bool dit, bool dah)
{
    k->dit_in = dit;
    k->dah_in = dah;
}

static void start_element(keyer_t *k, int el, uint32_t now)
{
    k->cur_el = el;
    k->state = KEYER_ST_ELEMENT;
    k->key_out = true;
    k->el_end_ms = now + (el == KEYER_EL_DIT ? k->dit_ms : (uint32_t)k->dit_ms * 3);
    k->mem_dit = k->mem_dah = false;
}

bool keyer_tick(keyer_t *k, uint32_t now)
{
    if (k->mode == KEYER_MODE_STRAIGHT) {
        k->key_out = k->dit_in;
        return k->key_out;
    }

    /* 记忆：当前元素的对侧桨在元素/间隔期间被按下 */
    if (k->state != KEYER_ST_IDLE) {
        if (k->cur_el == KEYER_EL_DIT && k->dah_in) k->mem_dah = true;
        if (k->cur_el == KEYER_EL_DAH && k->dit_in) k->mem_dit = true;
    }

    switch (k->state) {
    case KEYER_ST_IDLE:
        if (k->dit_in) start_element(k, KEYER_EL_DIT, now);
        else if (k->dah_in) start_element(k, KEYER_EL_DAH, now);
        break;

    case KEYER_ST_ELEMENT:
        if (now >= k->el_end_ms) {
            k->key_out = false;
            k->state = KEYER_ST_GAP;
            k->el_end_ms = now + k->dit_ms;
        }
        break;

    case KEYER_ST_GAP:
        if (now >= k->el_end_ms) {
            int next = -1;
            if (k->dit_in && k->dah_in) next = (k->cur_el == KEYER_EL_DIT) ? KEYER_EL_DAH : KEYER_EL_DIT;
            else if (k->dit_in) next = KEYER_EL_DIT;
            else if (k->dah_in) next = KEYER_EL_DAH;
            else if (k->mode == KEYER_MODE_IAMBIC_B) {
                if (k->mem_dit) next = KEYER_EL_DIT;
                else if (k->mem_dah) next = KEYER_EL_DAH;
            }
            if (next >= 0) start_element(k, next, now);
            else { k->state = KEYER_ST_IDLE; k->mem_dit = k->mem_dah = false; }
        }
        break;
    }
    return k->key_out;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `test_keyer` 报 `6 tests, 0 failed`。若 `iambic_single_dit_then_gap` 差 1，检查测试里 t=0 已 tick 一次的边界（元素 on 区间为 [0,60)）。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/keyer.* firmware/cw-trainer/test/test_keyer.c
git commit -m "CW：新增电键状态机（直键 / Iambic A / B）"
```

---

### Task 5: cw_decoder 实时解码

**Files:**
- Create: `firmware/cw-trainer/main/core/cw_decoder.h`, `firmware/cw-trainer/main/core/cw_decoder.c`
- Test: `firmware/cw-trainer/test/test_cw_decoder.c`

**Interfaces:**
- Consumes: `morse_table_decode()`。
- Produces:
  ```c
  typedef struct {
      uint16_t dit_ref_ms;       /* 点参考时长（自适应） */
      bool key_down;
      uint32_t last_edge_ms;
      char pattern[8];           /* 当前字符已收集的点划 */
      uint8_t pattern_len;
      bool char_pending;         /* pattern 非空且尚未输出 */
      bool word_gap_emitted;
  } cw_decoder_t;
  void cw_decoder_init(cw_decoder_t *d, uint16_t dit_ref_ms);
  void cw_decoder_key(cw_decoder_t *d, bool down, uint32_t now_ms);   /* 电平变化时调用 */
  /* 周期调用；若有字符产出写入 *out（' ' 表示词间隔），返回 1，否则 0 */
  int cw_decoder_poll(cw_decoder_t *d, uint32_t now_ms, char *out);
  const char *cw_decoder_pattern(const cw_decoder_t *d);   /* 当前未完成字符的点划串 */
  ```
  判定：元素时长 < 2×dit_ref → 点，否则划；每个点样本以 1/4 权重更新 dit_ref（钳到 20..400）。抬键后间隔 ≥ 2×dit_ref 输出字符；≥ 5×dit_ref 再输出一个 `' '`（只一次）。无法识别的点划串输出 `'?'`。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_cw_decoder.c`：
```c
#include "test_util.h"
#include "cw_decoder.h"
#include <string.h>

/* 用理想时序把 pattern 敲进解码器，返回 t 结束时刻 */
static uint32_t send_pattern(cw_decoder_t *d, const char *pat, uint32_t t, uint16_t dit)
{
    for (const char *p = pat; *p; p++) {
        cw_decoder_key(d, true, t);
        t += (*p == '.') ? dit : dit * 3;
        cw_decoder_key(d, false, t);
        t += dit;
    }
    return t;
}

static int collect(cw_decoder_t *d, uint32_t from, uint32_t to, char *buf, int cap)
{
    int n = 0; char c;
    for (uint32_t t = from; t <= to; t += 5) {
        if (cw_decoder_poll(d, t, &c) && n < cap - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

TEST(decodes_single_char_after_gap)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = send_pattern(&d, "-.-", 0, 60);   /* K */
    char buf[8];
    collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "K ");
}

TEST(pattern_visible_while_pending)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    cw_decoder_key(&d, true, 0);
    cw_decoder_key(&d, false, 60);
    cw_decoder_key(&d, true, 120);
    cw_decoder_key(&d, false, 300);
    ASSERT_EQ_STR(cw_decoder_pattern(&d), ".-");
}

TEST(decodes_word_with_char_gaps)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = 0;
    const char *word = "CQ";
    char buf[16]; int n = 0;
    for (const char *p = word; *p; p++) {
        const char *pat = (*p == 'C') ? "-.-." : "--.-";
        t = send_pattern(&d, pat, t, 60);
        t += 120;  /* 字符间总 3 dit：send_pattern 末尾已含 1 dit */
        n += collect(&d, t - 120, t, buf + n, (int)sizeof buf - n);
    }
    n += collect(&d, t, t + 1000, buf + n, (int)sizeof buf - n);
    ASSERT_EQ_STR(buf, "CQ ");
}

TEST(tolerates_20_percent_jitter)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    /* S = ... 用 72ms 的点（+20%）；O = --- 用 144ms 的划（-20%） */
    uint32_t t = 0;
    for (int i = 0; i < 3; i++) { cw_decoder_key(&d, true, t); t += 72; cw_decoder_key(&d, false, t); t += 60; }
    /* 自适应后 dit_ref≈66：字符判定阈值≈132ms，词间阈值≈330ms；窗口取 200ms 只出字符不出空格 */
    t += 200;
    char buf[8]; collect(&d, t - 200, t, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "S");
    for (int i = 0; i < 3; i++) { cw_decoder_key(&d, true, t); t += 144; cw_decoder_key(&d, false, t); t += 60; }
    collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "O ");
}

TEST(unknown_pattern_gives_question_mark)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = send_pattern(&d, "........", 0, 60);
    char buf[8]; collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "? ");
}

int main(void)
{
    RUN(decodes_single_char_after_gap);
    RUN(pattern_visible_while_pending);
    RUN(decodes_word_with_char_gaps);
    RUN(tolerates_20_percent_jitter);
    RUN(unknown_pattern_gives_question_mark);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `cw_decoder.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/cw_decoder.h`：
```c
#ifndef CW_DECODER_H
#define CW_DECODER_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t dit_ref_ms;
    bool key_down;
    uint32_t last_edge_ms;
    char pattern[8];
    uint8_t pattern_len;
    bool char_pending;
    bool word_gap_emitted;
} cw_decoder_t;

void cw_decoder_init(cw_decoder_t *d, uint16_t dit_ref_ms);
void cw_decoder_set_dit_ref(cw_decoder_t *d, uint16_t dit_ref_ms);
void cw_decoder_key(cw_decoder_t *d, bool down, uint32_t now_ms);
int cw_decoder_poll(cw_decoder_t *d, uint32_t now_ms, char *out);
const char *cw_decoder_pattern(const cw_decoder_t *d);

#endif
```

`firmware/cw-trainer/main/core/cw_decoder.c`：
```c
#include "cw_decoder.h"
#include "morse_table.h"

#define DIT_REF_MIN 20
#define DIT_REF_MAX 400

void cw_decoder_init(cw_decoder_t *d, uint16_t dit_ref_ms)
{
    d->dit_ref_ms = dit_ref_ms ? dit_ref_ms : 60;
    d->key_down = false;
    d->last_edge_ms = 0;
    d->pattern[0] = '\0';
    d->pattern_len = 0;
    d->char_pending = false;
    d->word_gap_emitted = true;   /* 开机不先输出空格 */
}

void cw_decoder_set_dit_ref(cw_decoder_t *d, uint16_t dit_ref_ms)
{
    if (dit_ref_ms) d->dit_ref_ms = dit_ref_ms;
}

/* 只有"像点"的样本才更新基准。
 * 过长的样本（把划误判成点的边缘样本）会把基准自我强化地推高，
 * 过短的样本（接触弹跳、单个 tick 的误触）会把它拽到下限，
 * 两者最终都会让所有元素被判成同一种。窗口取 0.5x..1.5x 当前基准。 */
static bool sample_is_plausible_dit(const cw_decoder_t *d, uint32_t len)
{
    uint32_t ref = d->dit_ref_ms;
    return len * 2 >= ref && len * 2 <= ref * 3;
}

static void adapt(cw_decoder_t *d, uint32_t dit_sample)
{
    uint32_t ref = ((uint32_t)d->dit_ref_ms * 3 + dit_sample) / 4;
    if (ref < DIT_REF_MIN) ref = DIT_REF_MIN;
    if (ref > DIT_REF_MAX) ref = DIT_REF_MAX;
    d->dit_ref_ms = (uint16_t)ref;
}

void cw_decoder_key(cw_decoder_t *d, bool down, uint32_t now)
{
    if (down == d->key_down) return;
    if (!down) {
        uint32_t len = now - d->last_edge_ms;
        char el;
        if (len < (uint32_t)d->dit_ref_ms * 2) {
            el = '.';
            if (sample_is_plausible_dit(d, len)) adapt(d, len);
        } else {
            el = '-';
        }
        if (d->pattern_len < sizeof(d->pattern) - 1) {
            d->pattern[d->pattern_len++] = el;
            d->pattern[d->pattern_len] = '\0';
        }
        d->char_pending = true;
        d->word_gap_emitted = false;
    }
    d->key_down = down;
    d->last_edge_ms = now;
}

int cw_decoder_poll(cw_decoder_t *d, uint32_t now, char *out)
{
    if (d->key_down) return 0;
    uint32_t gap = now - d->last_edge_ms;
    if (d->char_pending && gap >= (uint32_t)d->dit_ref_ms * 2) {
        char c = morse_table_decode(d->pattern);
        *out = c ? c : '?';
        d->pattern[0] = '\0';
        d->pattern_len = 0;
        d->char_pending = false;
        return 1;
    }
    if (!d->char_pending && !d->word_gap_emitted && gap >= (uint32_t)d->dit_ref_ms * 5) {
        *out = ' ';
        d->word_gap_emitted = true;
        return 1;
    }
    return 0;
}

const char *cw_decoder_pattern(const cw_decoder_t *d) { return d->pattern; }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `test_cw_decoder` 报 `5 tests, 0 failed`。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/cw_decoder.* firmware/cw-trainer/test/test_cw_decoder.c
git commit -m "CW：新增实时解码器"
```

---

### Task 6: rng + koch_engine

**Files:**
- Create: `firmware/cw-trainer/main/core/rng.h`, `firmware/cw-trainer/main/core/koch_engine.h`, `firmware/cw-trainer/main/core/koch_engine.c`
- Test: `firmware/cw-trainer/test/test_koch_engine.c`

**Interfaces:**
- Produces:
  ```c
  /* rng.h（header-only） */
  typedef struct { uint32_t s; } rng_t;
  static inline void rng_seed(rng_t *r, uint32_t seed);
  static inline uint32_t rng_next(rng_t *r);                 /* xorshift32 */
  static inline uint32_t rng_below(rng_t *r, uint32_t n);    /* [0, n) */

  /* koch_engine.h */
  #define KOCH_LESSON_MIN 1
  #define KOCH_LESSON_MAX 39
  #define KOCH_GROUP_LEN 5
  #define KOCH_PASS_PERCENT 90
  extern const char KOCH_ORDER[41];   /* "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X" */
  /* 第 lesson 课的字符集：KOCH_ORDER 的前 lesson+1 个字符（第 1 课 = "KM"，第 39 课 = 全部 40 字符）。out 至少 42 字节。返回长度。 */
  int koch_charset(uint8_t lesson, char *out);
  /* 第 lesson 课新引入的字符（第 1 课返回 'M'）。 */
  char koch_new_char(uint8_t lesson);
  /* 生成 5 字符组，保证至少含一个本课新字符。out 至少 6 字节。 */
  void koch_make_group(uint8_t lesson, rng_t *rng, char *out);
  bool koch_passed(uint8_t percent);
  /* 比较答案与目标，返回正确字符数；mismatch 掩码：bit i = 第 i 位错 */
  int koch_score_group(const char *target, const char *answer, uint8_t *mismatch);
  ```

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_koch_engine.c`：
```c
#include "test_util.h"
#include "koch_engine.h"
#include <string.h>

TEST(charset_grows_with_lesson)
{
    char cs[48];
    ASSERT_EQ_INT(koch_charset(1, cs), 2);
    ASSERT_EQ_STR(cs, "KM");
    ASSERT_EQ_INT(koch_charset(7, cs), 8);
    ASSERT_EQ_STR(cs, "KMRSUAPT");
    ASSERT_EQ_INT(koch_charset(39, cs), 40);
    ASSERT_EQ_INT(koch_charset(0, cs), 2);     /* 钳到 1 */
    ASSERT_EQ_INT(koch_charset(99, cs), 40);   /* 钳到 39 */
}

TEST(new_char)
{
    ASSERT_EQ_INT(koch_new_char(1), 'M');
    ASSERT_EQ_INT(koch_new_char(7), 'T');
}

TEST(group_uses_charset_and_contains_new_char)
{
    rng_t r; rng_seed(&r, 12345);
    char cs[48]; koch_charset(7, cs);
    for (int i = 0; i < 200; i++) {
        char g[8]; koch_make_group(7, &r, g);
        ASSERT_EQ_INT((int)strlen(g), 5);
        ASSERT_TRUE(strchr(g, 'T') != NULL);
        for (int j = 0; j < 5; j++) ASSERT_TRUE(strchr(cs, g[j]) != NULL);
    }
}

TEST(pass_threshold)
{
    ASSERT_TRUE(koch_passed(90));
    ASSERT_TRUE(koch_passed(100));
    ASSERT_TRUE(!koch_passed(89));
}

TEST(score_group)
{
    uint8_t mm;
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KMRSU", &mm), 5);
    ASSERT_EQ_INT(mm, 0);
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KMRSA", &mm), 4);
    ASSERT_EQ_INT(mm, 0x10);
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KM", &mm), 2);   /* 短答案缺位算错 */
    ASSERT_EQ_INT(mm, 0x1C);
}

int main(void)
{
    RUN(charset_grows_with_lesson);
    RUN(new_char);
    RUN(group_uses_charset_and_contains_new_char);
    RUN(pass_threshold);
    RUN(score_group);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `koch_engine.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/rng.h`：
```c
#ifndef RNG_H
#define RNG_H
#include <stdint.h>

typedef struct { uint32_t s; } rng_t;

static inline void rng_seed(rng_t *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }

static inline uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}

static inline uint32_t rng_below(rng_t *r, uint32_t n) { return n ? rng_next(r) % n : 0; }

#endif
```

`firmware/cw-trainer/main/core/koch_engine.h`：
```c
#ifndef KOCH_ENGINE_H
#define KOCH_ENGINE_H
#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

#define KOCH_LESSON_MIN   1
#define KOCH_LESSON_MAX   39
#define KOCH_GROUP_LEN    5
#define KOCH_PASS_PERCENT 90

extern const char KOCH_ORDER[41];

int  koch_charset(uint8_t lesson, char *out);
char koch_new_char(uint8_t lesson);
void koch_make_group(uint8_t lesson, rng_t *rng, char *out);
bool koch_passed(uint8_t percent);
int  koch_score_group(const char *target, const char *answer, uint8_t *mismatch);

#endif
```

`firmware/cw-trainer/main/core/koch_engine.c`：
```c
#include "koch_engine.h"
#include <string.h>

const char KOCH_ORDER[41] = "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X";

static uint8_t clamp_lesson(uint8_t l)
{
    if (l < KOCH_LESSON_MIN) return KOCH_LESSON_MIN;
    if (l > KOCH_LESSON_MAX) return KOCH_LESSON_MAX;
    return l;
}

int koch_charset(uint8_t lesson, char *out)
{
    int n = clamp_lesson(lesson) + 1;
    memcpy(out, KOCH_ORDER, (size_t)n);
    out[n] = '\0';
    return n;
}

char koch_new_char(uint8_t lesson) { return KOCH_ORDER[clamp_lesson(lesson)]; }

void koch_make_group(uint8_t lesson, rng_t *rng, char *out)
{
    char cs[48];
    int n = koch_charset(lesson, cs);
    for (int i = 0; i < KOCH_GROUP_LEN; i++) out[i] = cs[rng_below(rng, (uint32_t)n)];
    out[rng_below(rng, KOCH_GROUP_LEN)] = koch_new_char(lesson);
    out[KOCH_GROUP_LEN] = '\0';
}

bool koch_passed(uint8_t percent) { return percent >= KOCH_PASS_PERCENT; }

int koch_score_group(const char *target, const char *answer, uint8_t *mismatch)
{
    int correct = 0;
    uint8_t mm = 0;
    size_t alen = strlen(answer);
    for (int i = 0; target[i] && i < 8; i++) {
        if ((size_t)i < alen && answer[i] == target[i]) correct++;
        else mm |= (uint8_t)(1u << i);
    }
    if (mismatch) *mismatch = mm;
    return correct;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `test_koch_engine` 报 `5 tests, 0 failed`。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/rng.h firmware/cw-trainer/main/core/koch_engine.* firmware/cw-trainer/test/test_koch_engine.c
git commit -m "CW：新增 Koch 课程引擎"
```

---

### Task 7: callsign_gen（呼号、干扰项、RST）

**Files:**
- Create: `firmware/cw-trainer/main/core/callsign_gen.h`, `firmware/cw-trainer/main/core/callsign_gen.c`
- Test: `firmware/cw-trainer/test/test_callsign_gen.c`

**Interfaces:**
- Consumes: `rng.h`。
- Produces:
  ```c
  #define CALLSIGN_MAX 8   /* 含结尾 0 */
  /* 用 charset 中的字母/数字生成 "前缀(1~2 字母)+数字+后缀(1~3 字母)"。
     charset 若字母 < 4 或数字 < 1 返回 false，且 out 不变。 */
  bool callsign_gen_random(const char *charset, rng_t *rng, char out[CALLSIGN_MAX]);
  /* 生成 3 个与 target 互不相同的干扰项：每个替换 1 个字符为易混字符。 */
  void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX]);
  /* RST：从 {599,579,559,539,449,339} 中选 target 与 3 个不同干扰项。out[0] 为正确项。 */
  void callsign_gen_rst(rng_t *rng, char out[4][4]);
  /* 完整字符集（A-Z0-9） */
  extern const char CALLSIGN_FULL_CHARSET[37];
  ```
  易混表（双向）：`S↔H`、`U↔V`、`E↔I`、`N↔M`、`D↔B`、`G↔W`、`K↔R`、`5↔S`、`0↔O`、`1↔J`、`9↔6`、`2↔3`。若某位字符不在表里，则换成 charset 内随机另一字符（呼号有效字符集 = target 中字符 + 易混字符）。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_callsign_gen.c`：
```c
#include "test_util.h"
#include "callsign_gen.h"
#include <string.h>
#include <ctype.h>

TEST(random_callsign_has_valid_shape)
{
    rng_t r; rng_seed(&r, 7);
    char cs[CALLSIGN_MAX];
    for (int i = 0; i < 300; i++) {
        ASSERT_TRUE(callsign_gen_random(CALLSIGN_FULL_CHARSET, &r, cs));
        size_t len = strlen(cs);
        ASSERT_TRUE(len >= 3 && len <= 6);
        /* 恰好一个数字，且不在首位也不在末位 */
        int digits = 0, dpos = -1;
        for (size_t j = 0; j < len; j++) if (isdigit((unsigned char)cs[j])) { digits++; dpos = (int)j; }
        ASSERT_EQ_INT(digits, 1);
        ASSERT_TRUE(dpos >= 1 && dpos <= 2 && dpos < (int)len - 1);
    }
}

TEST(random_callsign_respects_charset)
{
    rng_t r; rng_seed(&r, 9);
    char cs[CALLSIGN_MAX];
    const char *charset = "KMRSU5";
    for (int i = 0; i < 100; i++) {
        ASSERT_TRUE(callsign_gen_random(charset, &r, cs));
        for (const char *p = cs; *p; p++) ASSERT_TRUE(strchr(charset, *p) != NULL);
    }
}

TEST(random_callsign_rejects_small_charset)
{
    rng_t r; rng_seed(&r, 1);
    char cs[CALLSIGN_MAX] = "XX";
    ASSERT_TRUE(!callsign_gen_random("KMR5", &r, cs));   /* 3 个字母不够 */
    ASSERT_TRUE(!callsign_gen_random("KMRSU", &r, cs));  /* 没数字 */
    ASSERT_EQ_STR(cs, "XX");
}

TEST(distractors_distinct_and_one_char_apart)
{
    rng_t r; rng_seed(&r, 3);
    char d[3][CALLSIGN_MAX];
    for (int i = 0; i < 100; i++) {
        callsign_gen_distractors("BG5ESN", &r, d);
        for (int a = 0; a < 3; a++) {
            ASSERT_TRUE(strcmp(d[a], "BG5ESN") != 0);
            ASSERT_EQ_INT((int)strlen(d[a]), 6);
            int diff = 0;
            for (int j = 0; j < 6; j++) if (d[a][j] != "BG5ESN"[j]) diff++;
            ASSERT_EQ_INT(diff, 1);
            for (int b = a + 1; b < 3; b++) ASSERT_TRUE(strcmp(d[a], d[b]) != 0);
        }
    }
}

TEST(rst_options_distinct)
{
    rng_t r; rng_seed(&r, 5);
    char o[4][4];
    for (int i = 0; i < 50; i++) {
        callsign_gen_rst(&r, o);
        for (int a = 0; a < 4; a++) {
            ASSERT_EQ_INT((int)strlen(o[a]), 3);
            for (int b = a + 1; b < 4; b++) ASSERT_TRUE(strcmp(o[a], o[b]) != 0);
        }
    }
}

int main(void)
{
    RUN(random_callsign_has_valid_shape);
    RUN(random_callsign_respects_charset);
    RUN(random_callsign_rejects_small_charset);
    RUN(distractors_distinct_and_one_char_apart);
    RUN(rst_options_distinct);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `callsign_gen.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/callsign_gen.h`：
```c
#ifndef CALLSIGN_GEN_H
#define CALLSIGN_GEN_H
#include <stdbool.h>
#include "rng.h"

#define CALLSIGN_MAX 8

extern const char CALLSIGN_FULL_CHARSET[37];

bool callsign_gen_random(const char *charset, rng_t *rng, char out[CALLSIGN_MAX]);
void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX]);
void callsign_gen_rst(rng_t *rng, char out[4][4]);

#endif
```

`firmware/cw-trainer/main/core/callsign_gen.c`：
```c
#include "callsign_gen.h"
#include <string.h>

const char CALLSIGN_FULL_CHARSET[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static const char CONFUSABLE[][2] = {
    {'S','H'}, {'U','V'}, {'E','I'}, {'N','M'}, {'D','B'}, {'G','W'},
    {'K','R'}, {'5','S'}, {'0','O'}, {'1','J'}, {'9','6'}, {'2','3'},
};

static const char *RST_POOL[] = { "599", "579", "559", "539", "449", "339" };
#define RST_POOL_N 6

static int split(const char *charset, char *letters, char *digits)
{
    int nl = 0, nd = 0;
    for (const char *p = charset; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') letters[nl++] = *p;
        else if (*p >= '0' && *p <= '9') digits[nd++] = *p;
    }
    letters[nl] = '\0'; digits[nd] = '\0';
    return nl;
}

bool callsign_gen_random(const char *charset, rng_t *rng, char out[CALLSIGN_MAX])
{
    char letters[40], digits[12];
    int nl = split(charset, letters, digits);
    int nd = (int)strlen(digits);
    if (nl < 4 || nd < 1) return false;

    int prefix = 1 + (int)rng_below(rng, 2);       /* 1..2 */
    int suffix = 1 + (int)rng_below(rng, 3);       /* 1..3 */
    int i = 0;
    for (int k = 0; k < prefix; k++) out[i++] = letters[rng_below(rng, (uint32_t)nl)];
    out[i++] = digits[rng_below(rng, (uint32_t)nd)];
    for (int k = 0; k < suffix; k++) out[i++] = letters[rng_below(rng, (uint32_t)nl)];
    out[i] = '\0';
    return true;
}

static char confusable_of(char c, rng_t *rng)
{
    char cands[4]; int n = 0;
    for (size_t i = 0; i < sizeof(CONFUSABLE) / sizeof(CONFUSABLE[0]); i++) {
        if (CONFUSABLE[i][0] == c) cands[n++] = CONFUSABLE[i][1];
        else if (CONFUSABLE[i][1] == c) cands[n++] = CONFUSABLE[i][0];
    }
    if (n == 0) return '\0';
    return cands[rng_below(rng, (uint32_t)n)];
}

static char any_other(char c, rng_t *rng)
{
    char r;
    do { r = CALLSIGN_FULL_CHARSET[rng_below(rng, 36)]; } while (r == c);
    return r;
}

void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX])
{
    size_t len = strlen(target);
    int made = 0;
    int guard = 0;
    while (made < 3 && guard++ < 200) {
        char cand[CALLSIGN_MAX];
        strncpy(cand, target, CALLSIGN_MAX - 1);
        cand[CALLSIGN_MAX - 1] = '\0';
        size_t pos = rng_below(rng, (uint32_t)len);
        char rep = confusable_of(target[pos], rng);
        if (!rep) rep = any_other(target[pos], rng);
        /* 保持"恰好一个数字"的形状：数字位只换数字，字母位只换字母 */
        bool tgt_digit = (target[pos] >= '0' && target[pos] <= '9');
        bool rep_digit = (rep >= '0' && rep <= '9');
        if (tgt_digit != rep_digit) rep = tgt_digit ? (char)('0' + rng_below(rng, 10)) : (char)('A' + rng_below(rng, 26));
        if (rep == target[pos]) continue;
        cand[pos] = rep;
        bool dup = false;
        for (int i = 0; i < made; i++) if (strcmp(out[i], cand) == 0) dup = true;
        if (dup) continue;
        strcpy(out[made++], cand);
    }
    /* 极端情况兜底：依次改第 0、1、2 位 */
    for (; made < 3; made++) {
        strcpy(out[made], target);
        out[made][made % len] = any_other(target[made % len], rng);
    }
}

void callsign_gen_rst(rng_t *rng, char out[4][4])
{
    int idx[RST_POOL_N];
    for (int i = 0; i < RST_POOL_N; i++) idx[i] = i;
    for (int i = RST_POOL_N - 1; i > 0; i--) {
        int j = (int)rng_below(rng, (uint32_t)i + 1);
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (int i = 0; i < 4; i++) strcpy(out[i], RST_POOL[idx[i]]);
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: `test_callsign_gen` 报 `5 tests, 0 failed`。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/callsign_gen.* firmware/cw-trainer/test/test_callsign_gen.c
git commit -m "CW：新增呼号生成与易混干扰项"
```

---

### Task 8: trainer 练习会话状态机

**Files:**
- Create: `firmware/cw-trainer/main/core/trainer.h`, `firmware/cw-trainer/main/core/trainer.c`
- Test: `firmware/cw-trainer/test/test_trainer.c`

**Interfaces:**
- Consumes: `koch_engine.h`、`callsign_gen.h`、`rng.h`。
- Produces:
  ```c
  typedef enum { TRAINER_MODE_KOCH = 0, TRAINER_MODE_CALLSIGN = 1, TRAINER_MODE_QSO = 2 } trainer_mode_t;
  #define TRAINER_ROUNDS 10
  #define TRAINER_MAX_ERRORS 16

  typedef struct { char expected; char got; } trainer_error_t;   /* got == '_' 表示缺位 */

  typedef struct {
      trainer_mode_t mode;
      uint8_t lesson;                  /* Koch 课次；呼号/QSO 模式用于字符集 */
      bool charset_fallback;           /* 呼号/QSO：字符集不足，已回退到全集（UI 显示提示） */
      rng_t rng;
      uint8_t round;                   /* 0..TRAINER_ROUNDS，当前第几组（0 基） */
      uint8_t correct_rounds;
      char target[CALLSIGN_MAX];       /* Koch：5 字符组；呼号：呼号；QSO：呼号 */
      char rst[4];                     /* QSO 正确 RST */
      char options[4][CALLSIGN_MAX];   /* 呼号/QSO 呼号选项（含正确项，顺序已打乱） */
      char rst_options[4][4];          /* QSO RST 选项（已打乱） */
      char play_text[48];              /* 本轮应播放的文本 */
      trainer_error_t errors[TRAINER_MAX_ERRORS];
      uint8_t error_count;
      bool finished;
  } trainer_t;

  /* my_callsign 只在 QSO 模式播放文本中用到 */
  void trainer_start(trainer_t *t, trainer_mode_t mode, uint8_t lesson, uint32_t seed);
  /* 提交答案。Koch：5 字符串；呼号：所选呼号；QSO："呼号 RST"（空格分隔）。返回本轮是否全对，并把逐字错误累加到 errors。 */
  bool trainer_submit(trainer_t *t, const char *answer);
  /* 进入下一组；若已满 TRAINER_ROUNDS 组则置 finished 并返回 false。 */
  bool trainer_next(trainer_t *t);
  uint8_t trainer_percent(const trainer_t *t);   /* correct_rounds*100/TRAINER_ROUNDS */
  /* 生成本轮的 target / options / play_text（trainer_start 与 trainer_next 内部调用） */
  ```
  播放文本：Koch = target；呼号 = target；QSO = `"CQ CQ DE <target> K"`（RST 在第二段：`" UR RST <rst> <rst> BK"`，合并为一个字符串，`play_text` 里两段之间用空格）。

- [ ] **Step 1: 写失败测试**

`firmware/cw-trainer/test/test_trainer.c`：
```c
#include "test_util.h"
#include "trainer.h"
#include <string.h>

TEST(koch_ten_rounds_and_percent)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, 42);
    ASSERT_EQ_INT((int)strlen(t.target), 5);
    ASSERT_EQ_STR(t.play_text, t.target);
    int rounds = 0;
    do {
        rounds++;
        bool ok = trainer_submit(&t, t.target);   /* 全部答对 */
        ASSERT_TRUE(ok);
    } while (trainer_next(&t));
    ASSERT_EQ_INT(rounds, TRAINER_ROUNDS);
    ASSERT_TRUE(t.finished);
    ASSERT_EQ_INT(trainer_percent(&t), 100);
    ASSERT_EQ_INT(t.error_count, 0);
}

TEST(koch_records_errors)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, 42);
    char wrong[8]; strcpy(wrong, t.target);
    wrong[4] = (wrong[4] == 'K') ? 'M' : 'K';
    ASSERT_TRUE(!trainer_submit(&t, wrong));
    ASSERT_EQ_INT(t.error_count, 1);
    ASSERT_EQ_INT(t.errors[0].expected, t.target[4]);
    ASSERT_EQ_INT(t.errors[0].got, wrong[4]);
    ASSERT_TRUE(!trainer_submit(&t, "KM"));        /* 重复提交同一轮不应发生，但要健壮：缺位记 '_' */
}

TEST(callsign_mode_options_contain_target)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 39, 1);
    ASSERT_TRUE(!t.charset_fallback);
    int found = 0;
    for (int i = 0; i < 4; i++) if (strcmp(t.options[i], t.target) == 0) found++;
    ASSERT_EQ_INT(found, 1);
    ASSERT_EQ_STR(t.play_text, t.target);
    ASSERT_TRUE(trainer_submit(&t, t.target));
    trainer_next(&t);
    ASSERT_TRUE(!trainer_submit(&t, "ZZ9ZZ"));
    ASSERT_TRUE(t.error_count >= 1);
}

TEST(callsign_mode_falls_back_when_charset_small)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 1, 1);   /* 第 1 课只有 K M */
    ASSERT_TRUE(t.charset_fallback);
    ASSERT_TRUE(strlen(t.target) >= 3);
}

TEST(qso_mode_play_text_and_answer)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_QSO, 39, 8);
    ASSERT_TRUE(strstr(t.play_text, "CQ CQ DE ") == t.play_text);
    ASSERT_TRUE(strstr(t.play_text, t.target) != NULL);
    ASSERT_TRUE(strstr(t.play_text, t.rst) != NULL);
    ASSERT_TRUE(strstr(t.play_text, " BK") != NULL);
    char ans[16]; snprintf(ans, sizeof ans, "%s %s", t.target, t.rst);
    ASSERT_TRUE(trainer_submit(&t, ans));
    trainer_next(&t);
    snprintf(ans, sizeof ans, "%s 000", t.target);
    ASSERT_TRUE(!trainer_submit(&t, ans));
}

int main(void)
{
    RUN(koch_ten_rounds_and_percent);
    RUN(koch_records_errors);
    RUN(callsign_mode_options_contain_target);
    RUN(callsign_mode_falls_back_when_charset_small);
    RUN(qso_mode_play_text_and_answer);
    TEST_MAIN_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `make -C firmware/cw-trainer/test`
Expected: 编译错误，找不到 `trainer.h`。

- [ ] **Step 3: 实现**

`firmware/cw-trainer/main/core/trainer.h`：
```c
#ifndef TRAINER_H
#define TRAINER_H
#include <stdbool.h>
#include <stdint.h>
#include "rng.h"
#include "callsign_gen.h"

typedef enum { TRAINER_MODE_KOCH = 0, TRAINER_MODE_CALLSIGN = 1, TRAINER_MODE_QSO = 2 } trainer_mode_t;

#define TRAINER_ROUNDS     10
#define TRAINER_MAX_ERRORS 16

typedef struct { char expected; char got; } trainer_error_t;

typedef struct {
    trainer_mode_t mode;
    uint8_t lesson;
    bool charset_fallback;
    rng_t rng;
    uint8_t round;
    uint8_t correct_rounds;
    char target[CALLSIGN_MAX];
    char rst[4];
    char options[4][CALLSIGN_MAX];
    char rst_options[4][4];
    char play_text[48];
    trainer_error_t errors[TRAINER_MAX_ERRORS];
    uint8_t error_count;
    bool finished;
} trainer_t;

void    trainer_start(trainer_t *t, trainer_mode_t mode, uint8_t lesson, uint32_t seed);
bool    trainer_submit(trainer_t *t, const char *answer);
bool    trainer_next(trainer_t *t);
uint8_t trainer_percent(const trainer_t *t);

#endif
```

`firmware/cw-trainer/main/core/trainer.c`：
```c
#include "trainer.h"
#include "koch_engine.h"
#include <stdio.h>
#include <string.h>

static void add_error(trainer_t *t, char expected, char got)
{
    if (t->error_count < TRAINER_MAX_ERRORS) {
        t->errors[t->error_count].expected = expected;
        t->errors[t->error_count].got = got;
        t->error_count++;
    }
}

static void shuffle_options(trainer_t *t, char (*opts)[CALLSIGN_MAX], int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)rng_below(&t->rng, (uint32_t)i + 1);
        if (i == j) continue;   /* 同址 memcpy 同样是 restrict 违规 */
        char tmp[CALLSIGN_MAX];
        memcpy(tmp, opts[i], CALLSIGN_MAX);
        memcpy(opts[i], opts[j], CALLSIGN_MAX);
        memcpy(opts[j], tmp, CALLSIGN_MAX);
    }
}

static void gen_callsign_round(trainer_t *t)
{
    char cs[48];
    koch_charset(t->lesson, cs);
    t->charset_fallback = false;
    if (!callsign_gen_random(cs, &t->rng, t->target)) {
        t->charset_fallback = true;
        callsign_gen_random(CALLSIGN_FULL_CHARSET, &t->rng, t->target);
    }
    char d[3][CALLSIGN_MAX];
    callsign_gen_distractors(t->target, &t->rng, d);
    strcpy(t->options[0], t->target);
    for (int i = 0; i < 3; i++) strcpy(t->options[i + 1], d[i]);
    shuffle_options(t, t->options, 4);
}

static void gen_round(trainer_t *t)
{
    switch (t->mode) {
    case TRAINER_MODE_KOCH:
        koch_make_group(t->lesson, &t->rng, t->target);
        strcpy(t->play_text, t->target);
        break;
    case TRAINER_MODE_CALLSIGN:
        gen_callsign_round(t);
        strcpy(t->play_text, t->target);
        break;
    case TRAINER_MODE_QSO: {
        gen_callsign_round(t);
        char rsts[4][4];
        callsign_gen_rst(&t->rng, rsts);
        strcpy(t->rst, rsts[0]);
        for (int i = 0; i < 4; i++) strcpy(t->rst_options[i], rsts[i]);
        /* 打乱 RST 选项（rsts[0] 是正确项）。
         * i == j 时必须跳过：strcpy 的两个参数是 restrict 限定的，
         * 源和目的同址属于未定义行为，在部分 libc（如 macOS 的加固版）上会直接 trap。 */
        for (int i = 3; i > 0; i--) {
            int j = (int)rng_below(&t->rng, (uint32_t)i + 1);
            if (i == j) continue;
            char tmp[4]; strcpy(tmp, t->rst_options[i]);
            strcpy(t->rst_options[i], t->rst_options[j]);
            strcpy(t->rst_options[j], tmp);
        }
        snprintf(t->play_text, sizeof t->play_text, "CQ CQ DE %s K UR RST %s %s BK", t->target, t->rst, t->rst);
        break;
    }
    }
}

void trainer_start(trainer_t *t, trainer_mode_t mode, uint8_t lesson, uint32_t seed)
{
    memset(t, 0, sizeof *t);
    t->mode = mode;
    t->lesson = lesson;
    rng_seed(&t->rng, seed);
    gen_round(t);
}

bool trainer_submit(trainer_t *t, const char *answer)
{
    bool ok;
    if (t->mode == TRAINER_MODE_KOCH) {
        uint8_t mm = 0;
        int correct = koch_score_group(t->target, answer, &mm);
        size_t alen = strlen(answer);
        for (int i = 0; t->target[i]; i++) {
            if (mm & (1u << i)) add_error(t, t->target[i], (size_t)i < alen ? answer[i] : '_');
        }
        ok = (correct == KOCH_GROUP_LEN);
    } else if (t->mode == TRAINER_MODE_CALLSIGN) {
        ok = strcmp(answer, t->target) == 0;
        if (!ok) {
            /* 记录第一个不同的字符 */
            size_t i = 0;
            while (t->target[i] && answer[i] && t->target[i] == answer[i]) i++;
            add_error(t, t->target[i] ? t->target[i] : '_', answer[i] ? answer[i] : '_');
        }
    } else {
        char call[CALLSIGN_MAX] = {0}, rst[4] = {0};
        sscanf(answer, "%7s %3s", call, rst);
        bool call_ok = strcmp(call, t->target) == 0;
        bool rst_ok = strcmp(rst, t->rst) == 0;
        if (!call_ok) {
            size_t i = 0;
            while (t->target[i] && call[i] && t->target[i] == call[i]) i++;
            add_error(t, t->target[i] ? t->target[i] : '_', call[i] ? call[i] : '_');
        }
        if (!rst_ok) add_error(t, t->rst[1], rst[1] ? rst[1] : '_');
        ok = call_ok && rst_ok;
    }
    if (ok) t->correct_rounds++;
    return ok;
}

bool trainer_next(trainer_t *t)
{
    if (t->round + 1 >= TRAINER_ROUNDS) {
        t->finished = true;
        return false;
    }
    t->round++;
    gen_round(t);
    return true;
}

uint8_t trainer_percent(const trainer_t *t)
{
    return (uint8_t)((uint32_t)t->correct_rounds * 100u / TRAINER_ROUNDS);
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `make -C firmware/cw-trainer/test`
Expected: 所有 `test_*` 通过；`test_trainer` 报 `5 tests, 0 failed`。注意 `koch_records_errors` 中第二次 `trainer_submit` 会再次累加错误，这是测试故意验证健壮性，不是 bug。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main/core/trainer.* firmware/cw-trainer/test/test_trainer.c
git commit -m "CW：新增练习会话状态机"
```

---

### Task 9: cw_settings（NVS 持久化）

**Files:**
- Create: `firmware/cw-trainer/main/cw_settings.h`, `firmware/cw-trainer/main/cw_settings.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`（SRCS 追加 `cw_settings.c` 与 `core/*.c`）、`firmware/cw-trainer/main/main.c`

**Interfaces:**
- Produces:
  ```c
  #define CW_SETTINGS_VERSION 1
  typedef struct {
      uint32_t version;
      uint8_t  char_wpm;          /* 5..40 默认 20 */
      uint8_t  eff_wpm;           /* 5..char_wpm 默认 8 */
      uint16_t tone_hz;           /* 400..1000 默认 600 */
      uint8_t  volume;            /* 0..100 默认 70 */
      uint8_t  key_input;         /* 0 触摸屏 1 外接口 */
      uint8_t  keyer_mode;        /* keyer_mode_t 值，默认 2 (Iambic B) */
      uint8_t  paddle_swap;       /* 0/1 */
      char     callsign[12];      /* 默认 "BI8SYN" */
      uint8_t  koch_lesson;       /* 1..39 默认 1（40 字符 Koch 序列共 39 课） */
      uint8_t  koch_last_pct[41]; /* 下标 = 课次，0xFF 表示未练过 */
      uint8_t  brightness;        /* 10..100 默认 80 */
  } cw_settings_t;
  esp_err_t cw_settings_init(void);       /* 读 NVS；无/版本不符 → 默认值并写回，置 was_reset */
  cw_settings_t *cw_settings_get(void);   /* 可直接改字段，改完调 save */
  esp_err_t cw_settings_save(void);
  esp_err_t cw_settings_reset(void);      /* 恢复出厂：默认值 + 保存 */
  bool cw_settings_was_reset(void);       /* 本次启动是否因损坏/版本不符而重置（首次启动不算） */
  ```

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/cw_settings.h`：
```c
#ifndef CW_SETTINGS_H
#define CW_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CW_SETTINGS_VERSION 1
#define CW_KEY_INPUT_TOUCH  0
#define CW_KEY_INPUT_EXT    1

typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    uint8_t  key_input;
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
} cw_settings_t;

esp_err_t      cw_settings_init(void);
cw_settings_t *cw_settings_get(void);
esp_err_t      cw_settings_save(void);
esp_err_t      cw_settings_reset(void);
bool           cw_settings_was_reset(void);

#endif
```

`firmware/cw-trainer/main/cw_settings.c`：
```c
#include "cw_settings.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "cw_settings";
#define NVS_NS  "cwtrn"
#define NVS_KEY "cfg"

static cw_settings_t s_cfg;
static bool s_was_reset = false;

static void set_defaults(cw_settings_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = CW_SETTINGS_VERSION;
    c->char_wpm = 20;
    c->eff_wpm = 8;
    c->tone_hz = 600;
    c->volume = 70;
    c->key_input = CW_KEY_INPUT_TOUCH;
    c->keyer_mode = 2;
    c->paddle_swap = 0;
    strcpy(c->callsign, "BI8SYN");
    c->koch_lesson = 1;
    memset(c->koch_last_pct, 0xFF, sizeof c->koch_last_pct);
    c->brightness = 80;
}

static esp_err_t write_blob(const cw_settings_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY, c, sizeof *c);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t cw_settings_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        set_defaults(&s_cfg);
        return err;
    }
    size_t len = sizeof s_cfg;
    err = nvs_get_blob(h, NVS_KEY, &s_cfg, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no config, first run");
        set_defaults(&s_cfg);
        return write_blob(&s_cfg);
    }
    if (err != ESP_OK || len != sizeof s_cfg || s_cfg.version != CW_SETTINGS_VERSION) {
        ESP_LOGW(TAG, "config invalid (err=%s len=%u ver=%lu), reset",
                 esp_err_to_name(err), (unsigned)len, (unsigned long)s_cfg.version);
        set_defaults(&s_cfg);
        s_was_reset = true;
        return write_blob(&s_cfg);
    }
    return ESP_OK;
}

cw_settings_t *cw_settings_get(void) { return &s_cfg; }

esp_err_t cw_settings_save(void)
{
    esp_err_t err = write_blob(&s_cfg);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t cw_settings_reset(void)
{
    set_defaults(&s_cfg);
    return write_blob(&s_cfg);
}

bool cw_settings_was_reset(void) { return s_was_reset; }
```

- [ ] **Step 2: 接入 CMake 与 main.c**

`firmware/cw-trainer/main/CMakeLists.txt` 的 SRCS 改为：
```cmake
    SRCS
        "main.c"
        "cw_settings.c"
        "core/morse_table.c"
        "core/morse_timing.c"
        "core/keyer.c"
        "core/cw_decoder.c"
        "core/koch_engine.c"
        "core/callsign_gen.c"
        "core/trainer.c"
        "fonts/font20.c"
        "fonts/font28.c"
        "fonts/EN_BIG.c"
```

`main.c`：在 `nvs_init();` 之后加
```c
    ESP_ERROR_CHECK(cw_settings_init());
    ESP_LOGI(TAG, "cfg: wpm=%u/%u tone=%u lesson=%u reset=%d",
             cw_settings_get()->char_wpm, cw_settings_get()->eff_wpm,
             cw_settings_get()->tone_hz, cw_settings_get()->koch_lesson,
             cw_settings_was_reset());
```
并 `#include "cw_settings.h"`。

- [ ] **Step 3: 编译**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build`
Expected: `Project build complete.`

- [ ] **Step 4: 烧录并看日志（设备验证）**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh flash monitor`
Expected: 串口出现 `cw_settings: no config, first run` 与 `cfg: wpm=20/8 tone=600 lesson=1 reset=0`；再按 RESET 重启，第二次不再出现 `first run`。Ctrl+] 退出。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：新增 NVS 设置模块"
```

---

### Task 10: tone_gen 低延迟侧音

**Files:**
- Create: `firmware/cw-trainer/main/tone_gen.h`, `firmware/cw-trainer/main/tone_gen.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`（追加 `tone_gen.c`）、`firmware/cw-trainer/main/main.c`

**Interfaces:**
- Produces:
  ```c
  esp_err_t tone_gen_init(void);            /* I2S0 内置 DAC，16kHz，DMA 3×64 样本 ≈ 12ms */
  bool tone_gen_is_ok(void);                /* init 失败时 false，UI 据此显示"音频异常" */
  void tone_gen_set(bool on);               /* 任意上下文可调用（原子 flag） */
  void tone_gen_set_freq(uint16_t hz);
  void tone_gen_set_volume(uint8_t percent);
  ```
  实现：一个优先级 `configMAX_PRIORITIES - 2` 的任务循环生成 64 样本块：相位累加正弦（查表 256 点，int16），5ms 线性起落包络（80 样本），静音时写 0（DAC 中点 0x8000）并关功放（GPIO4 = MUTE）；发音时开功放。功放开关有 200ms 关断滞后避免爆音。

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/tone_gen.h`：
```c
#ifndef TONE_GEN_H
#define TONE_GEN_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t tone_gen_init(void);
bool      tone_gen_is_ok(void);
void      tone_gen_set(bool on);
void      tone_gen_set_freq(uint16_t hz);
void      tone_gen_set_volume(uint8_t percent);

#endif
```

`firmware/cw-trainer/main/tone_gen.c`：
```c
#include "tone_gen.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "board_config.h"

static const char *TAG = "tone_gen";

#define SAMPLE_RATE     16000
#define BLOCK_SAMPLES   64          /* 4ms */
#define DMA_BUF_COUNT   3
#define RAMP_SAMPLES    80          /* 5ms */
#define AMP_OFF_DELAY_BLOCKS 50     /* 200ms */
#define SINE_N          256

static int16_t s_sine[SINE_N];
static volatile bool s_on = false;
static volatile uint32_t s_phase_inc = 0;   /* Q16.16 表索引步进 */
static volatile uint16_t s_amp = 0;         /* 0..32767 */
static bool s_ok = false;

static void amp_enable(bool en)
{
    gpio_set_level(BOARD_AUDIO_EN_GPIO, en ? BOARD_AUDIO_EN_ACTIVE : BOARD_AUDIO_MUTE_LEVEL);
}

static void tone_task(void *arg)
{
    (void)arg;
    static uint16_t buf[BLOCK_SAMPLES * 2];   /* L/R 交错 */
    uint32_t phase = 0;
    int32_t env = 0;                          /* 0..RAMP_SAMPLES */
    int off_blocks = AMP_OFF_DELAY_BLOCKS;
    bool amp_on = false;

    while (true) {
        bool on = s_on;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            if (on && env < RAMP_SAMPLES) env++;
            else if (!on && env > 0) env--;
            int32_t v = 0;
            if (env > 0) {
                int32_t s = s_sine[(phase >> 16) & (SINE_N - 1)];
                v = (s * (int32_t)s_amp / 32767) * env / RAMP_SAMPLES;
                phase += s_phase_inc;
            }
            uint16_t w = (uint16_t)(v + 32768);
            buf[i * 2] = w;
            buf[i * 2 + 1] = w;
        }
        bool active = on || env > 0;
        if (active) { off_blocks = 0; if (!amp_on) { amp_enable(true); amp_on = true; } }
        else if (amp_on && ++off_blocks >= AMP_OFF_DELAY_BLOCKS) { amp_enable(false); amp_on = false; }

        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, sizeof buf, &written, portMAX_DELAY);
    }
}

esp_err_t tone_gen_init(void)
{
    for (int i = 0; i < SINE_N; i++) s_sine[i] = (int16_t)(sinf(2.0f * (float)M_PI * i / SINE_N) * 32767.0f);
    tone_gen_set_freq(600);
    tone_gen_set_volume(70);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_AUDIO_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    amp_enable(false);

    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = BLOCK_SAMPLES,
        .use_apll = false,
        .tx_desc_auto_clear = true,
    };
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_driver_install: %s", esp_err_to_name(err)); return err; }
    err = i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_set_dac_mode: %s", esp_err_to_name(err)); i2s_driver_uninstall(I2S_NUM_0); return err; }
    i2s_zero_dma_buffer(I2S_NUM_0);

    if (xTaskCreatePinnedToCore(tone_task, "tone", 3072, NULL, configMAX_PRIORITIES - 2, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_FAIL;
    }
    s_ok = true;
    ESP_LOGI(TAG, "ready: %d Hz, dma %d x %d samples", SAMPLE_RATE, DMA_BUF_COUNT, BLOCK_SAMPLES);
    return ESP_OK;
}

bool tone_gen_is_ok(void) { return s_ok; }
void tone_gen_set(bool on) { s_on = on; }

void tone_gen_set_freq(uint16_t hz)
{
    if (hz < 300) hz = 300;
    if (hz > 1200) hz = 1200;
    s_phase_inc = (uint32_t)(((uint64_t)hz * SINE_N << 16) / SAMPLE_RATE);
}

void tone_gen_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* 对数感：percent^2 */
    s_amp = (uint16_t)((uint32_t)32767 * percent * percent / 10000);
}
```

- [ ] **Step 2: 接入 main.c 做一次开机自检**

`main.c`：`#include "tone_gen.h"`，在 `cw_settings_init` 之后、`lv_init()` 之前：
```c
    if (tone_gen_init() == ESP_OK) {
        tone_gen_set_freq(cw_settings_get()->tone_hz);
        tone_gen_set_volume(cw_settings_get()->volume);
        tone_gen_set(true);  vTaskDelay(pdMS_TO_TICKS(100));
        tone_gen_set(false);
    }
```
CMake SRCS 追加 `"tone_gen.c"`。

- [ ] **Step 3: 编译 + 烧录听音**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected: 日志 `tone_gen: ready`，开机后扬声器/耳机口发出约 100ms 的 600Hz 短音，无爆音。若无声：确认 `BOARD_AUDIO_EN_ACTIVE` 为 0（低电平开功放，与 fmo-companion 一致）。

- [ ] **Step 4: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：新增 I2S DAC 低延迟侧音"
```

---

### Task 11: cw_player 文本发报

**Files:**
- Create: `firmware/cw-trainer/main/cw_player.h`, `firmware/cw-trainer/main/cw_player.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`、`firmware/cw-trainer/main/main.c`

**Interfaces:**
- Consumes: `tone_gen_set()`、`morse_table_lookup()`、`morse_timing_t`。
- Produces:
  ```c
  typedef enum { CW_PLAYER_EV_CHAR_START, CW_PLAYER_EV_DONE, CW_PLAYER_EV_STOPPED } cw_player_event_t;
  typedef void (*cw_player_cb_t)(cw_player_event_t ev, char c, void *user);  /* 在 esp_timer 任务上下文回调 */
  esp_err_t cw_player_init(void);
  void cw_player_set_timing(const morse_timing_t *t);
  /* 播放文本（复制到内部 64 字节缓冲）。正在播放时先 stop。 */
  esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user);
  void cw_player_stop(void);       /* 立即静音，回调 STOPPED */
  bool cw_player_is_playing(void);
  ```
  实现：esp_timer one-shot 链。状态 `{idx, pat_idx, key_on}`：每步要么开音（元素时长）要么关音（间隔）。字符内间隔 intra_gap；字符间 char_gap；空格 word_gap（空格前的 char_gap 不再叠加：遇空格时用 `word_gap - char_gap` 再补）。未知字符跳过。播放前先 500ms 静音前导。

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/cw_player.h`：
```c
#ifndef CW_PLAYER_H
#define CW_PLAYER_H
#include <stdbool.h>
#include "esp_err.h"
#include "morse_timing.h"

typedef enum { CW_PLAYER_EV_CHAR_START, CW_PLAYER_EV_DONE, CW_PLAYER_EV_STOPPED } cw_player_event_t;
typedef void (*cw_player_cb_t)(cw_player_event_t ev, char c, void *user);

esp_err_t cw_player_init(void);
void      cw_player_set_timing(const morse_timing_t *t);
esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user);
void      cw_player_stop(void);
bool      cw_player_is_playing(void);

#endif
```

`firmware/cw-trainer/main/cw_player.c`：
```c
#include "cw_player.h"
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "tone_gen.h"
#include "morse_table.h"

static const char *TAG = "cw_player";
#define TEXT_MAX 64
#define LEAD_IN_MS 500

static esp_timer_handle_t s_timer;
static morse_timing_t s_t = { 60, 180, 60, 180, 420 };
static char s_text[TEXT_MAX];
static int s_idx;               /* 当前字符下标 */
static const char *s_pat;       /* 当前字符点划串 */
static int s_pat_idx;
static bool s_key_on;
static volatile bool s_playing;
static cw_player_cb_t s_cb;
static void *s_user;

static void arm(uint32_t ms) { esp_timer_start_once(s_timer, (uint64_t)ms * 1000); }

static void finish(cw_player_event_t ev)
{
    tone_gen_set(false);
    s_playing = false;
    if (s_cb) s_cb(ev, 0, s_user);
}

/* 前进到下一个可发字符；返回 false 表示文本结束 */
static bool next_char(void)
{
    while (s_text[s_idx]) {
        char c = s_text[s_idx];
        if (c == ' ') {
            s_idx++;
            arm(s_t.word_gap_ms - s_t.char_gap_ms);
            return true;
        }
        s_pat = morse_table_lookup(c);
        if (s_pat) {
            s_pat_idx = 0;
            if (s_cb) s_cb(CW_PLAYER_EV_CHAR_START, c, s_user);
            s_key_on = true;
            tone_gen_set(true);
            arm(s_pat[0] == '.' ? s_t.dit_ms : s_t.dah_ms);
            s_idx++;
            return true;
        }
        s_idx++;   /* 未知字符跳过 */
    }
    return false;
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (!s_playing) return;

    if (s_key_on) {
        /* 元素结束 → 关音，安排间隔 */
        tone_gen_set(false);
        s_key_on = false;
        s_pat_idx++;
        if (s_pat && s_pat[s_pat_idx]) arm(s_t.intra_gap_ms);
        else { s_pat = NULL; arm(s_t.char_gap_ms); }
        return;
    }
    if (s_pat) {
        /* 字符内下一个元素 */
        s_key_on = true;
        tone_gen_set(true);
        arm(s_pat[s_pat_idx] == '.' ? s_t.dit_ms : s_t.dah_ms);
        return;
    }
    if (!next_char()) finish(CW_PLAYER_EV_DONE);
}

esp_err_t cw_player_init(void)
{
    const esp_timer_create_args_t a = { .callback = timer_cb, .name = "cw_player" };
    return esp_timer_create(&a, &s_timer);
}

void cw_player_set_timing(const morse_timing_t *t) { s_t = *t; }

esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user)
{
    cw_player_stop();
    strncpy(s_text, text, TEXT_MAX - 1);
    s_text[TEXT_MAX - 1] = '\0';
    s_idx = 0; s_pat = NULL; s_pat_idx = 0; s_key_on = false;
    s_cb = cb; s_user = user;
    s_playing = true;
    ESP_LOGD(TAG, "play \"%s\"", s_text);
    arm(LEAD_IN_MS);
    return ESP_OK;
}

void cw_player_stop(void)
{
    if (!s_playing) return;
    esp_timer_stop(s_timer);
    finish(CW_PLAYER_EV_STOPPED);
}

bool cw_player_is_playing(void) { return s_playing; }
```

- [ ] **Step 2: 接入 main.c 开机播 "PARIS" 替代 Task 10 的短音**

`main.c`：`#include "cw_player.h"`、`#include "morse_timing.h"`；把 Task 10 Step 2 的 `tone_gen_set(true)…set(false)` 三行替换为：
```c
        morse_timing_t tm;
        morse_timing_compute(cw_settings_get()->char_wpm, cw_settings_get()->eff_wpm, &tm);
        ESP_ERROR_CHECK(cw_player_init());
        cw_player_set_timing(&tm);
        cw_player_play("PARIS", NULL, NULL);
```
CMake SRCS 追加 `"cw_player.c"`。

- [ ] **Step 3: 编译 + 烧录**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected: 开机 0.5s 后听到 20WPM 的 `PARIS`（`.--. .- .-. .. ...`），节奏均匀、无断续。

- [ ] **Step 4: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：新增 esp_timer 驱动的文本发报"
```

---

### Task 12: ui_async、backlight、battery（设备层小模块）

**Files:**
- Create: `firmware/cw-trainer/main/ui_async.h/.c`, `firmware/cw-trainer/main/backlight.h/.c`, `firmware/cw-trainer/main/battery.h/.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```c
  /* ui_async.h：把回调投递到 LVGL 上下文（lv_async_call 封装，任意任务可调） */
  typedef void (*ui_async_fn_t)(void *arg);
  void ui_async_post(ui_async_fn_t fn, void *arg);

  /* backlight.h：GPIO21 LEDC PWM */
  void backlight_init(void);
  void backlight_set(uint8_t percent);   /* 0..100 */

  /* battery.h：GPIO34 ADC1_CH6，10s 一次 */
  typedef void (*battery_cb_t)(uint8_t percent, uint16_t mv);   /* 在 battery 任务上下文回调 */
  esp_err_t battery_start(battery_cb_t cb);
  uint8_t battery_percent(void);
  ```

- [ ] **Step 1: 实现 ui_async**

`firmware/cw-trainer/main/ui_async.h`：
```c
#ifndef UI_ASYNC_H
#define UI_ASYNC_H
typedef void (*ui_async_fn_t)(void *arg);
/** 从任意任务/定时器投递到 LVGL 主循环执行。 */
void ui_async_post(ui_async_fn_t fn, void *arg);
#endif
```

`firmware/cw-trainer/main/ui_async.c`：
```c
#include "ui_async.h"
#include <stdlib.h>
#include "lvgl.h"

typedef struct { ui_async_fn_t fn; void *arg; } msg_t;

static void trampoline(void *p)
{
    msg_t *m = p;
    m->fn(m->arg);
    free(m);
}

void ui_async_post(ui_async_fn_t fn, void *arg)
{
    msg_t *m = malloc(sizeof *m);
    if (!m) return;
    m->fn = fn; m->arg = arg;
    lv_async_call(trampoline, m);
}
```

- [ ] **Step 2: 实现 backlight**

`firmware/cw-trainer/main/backlight.h`：
```c
#ifndef BACKLIGHT_H
#define BACKLIGHT_H
#include <stdint.h>
void backlight_init(void);
void backlight_set(uint8_t percent);
#endif
```

`firmware/cw-trainer/main/backlight.c`：
```c
#include "backlight.h"
#include "driver/ledc.h"
#include "board_config.h"

#define BL_TIMER   LEDC_TIMER_0
#define BL_MODE    LEDC_LOW_SPEED_MODE
#define BL_CH      LEDC_CHANNEL_0
#define BL_RES     LEDC_TIMER_10_BIT
#define BL_MAX     ((1U << 10) - 1U)

void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = BL_MODE, .timer_num = BL_TIMER, .duty_resolution = BL_RES,
        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = BOARD_LCD_BL_GPIO, .speed_mode = BL_MODE, .channel = BL_CH,
        .timer_sel = BL_TIMER, .duty = BL_MAX, .hpoint = 0,
    };
    ledc_channel_config(&c);
}

void backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledc_set_duty(BL_MODE, BL_CH, BL_MAX * percent / 100);
    ledc_update_duty(BL_MODE, BL_CH);
}
```

- [ ] **Step 3: 实现 battery**

`firmware/cw-trainer/main/battery.h`：
```c
#ifndef BATTERY_H
#define BATTERY_H
#include <stdint.h>
#include "esp_err.h"
typedef void (*battery_cb_t)(uint8_t percent, uint16_t mv);
esp_err_t battery_start(battery_cb_t cb);
uint8_t   battery_percent(void);
#endif
```

`firmware/cw-trainer/main/battery.c`：
```c
#include "battery.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "board_config.h"

static const char *TAG = "battery";
#define SAMPLES    16
#define SCALE_PERMILLE 2000   /* 1:1 分压，与 fmo-companion 一致 */
#define EMPTY_MV   3000
#define FULL_MV    4180
#define PERIOD_MS  10000

static esp_adc_cal_characteristics_t s_cal;
static battery_cb_t s_cb;
static uint8_t s_percent = 100;

static uint8_t to_percent(uint32_t mv)
{
    if (mv <= EMPTY_MV) return 0;
    if (mv >= FULL_MV) return 100;
    return (uint8_t)((mv - EMPTY_MV) * 100 / (FULL_MV - EMPTY_MV));
}

static void task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t sum = 0;
        for (int i = 0; i < SAMPLES; i++) sum += (uint32_t)adc1_get_raw(BOARD_BATTERY_ADC_CH);
        uint32_t adc_mv = esp_adc_cal_raw_to_voltage(sum / SAMPLES, &s_cal);
        uint32_t mv = adc_mv * SCALE_PERMILLE / 1000;
        s_percent = to_percent(mv);
        ESP_LOGI(TAG, "%lu mV, %u%%", (unsigned long)mv, s_percent);
        if (s_cb) s_cb(s_percent, (uint16_t)mv);
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

esp_err_t battery_start(battery_cb_t cb)
{
    s_cb = cb;
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(BOARD_BATTERY_ADC_CH, ADC_ATTEN_DB_11));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_cal);
    return xTaskCreate(task, "battery", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

uint8_t battery_percent(void) { return s_percent; }
```

- [ ] **Step 4: CMake 追加并编译**

SRCS 追加 `"ui_async.c" "backlight.c" "battery.c"`。
Run: `PROJECT=firmware/cw-trainer tools/idf.sh build`
Expected: `Project build complete.`（legacy ADC 会有 deprecation 警告，可忽略；若警告被当错误，在 `main/CMakeLists.txt` 末尾加 `target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations)`）。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：新增 ui_async、背光、电量模块"
```

---

### Task 13: key_input（触摸桨 / 外接口 → keyer → tone + decoder）

**Files:**
- Create: `firmware/cw-trainer/main/key_input.h`, `firmware/cw-trainer/main/key_input.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `keyer_t`、`cw_decoder_t`、`tone_gen_set()`、`cw_settings_get()`。
- Produces:
  ```c
  /* 解码结果回调：在 key_input 任务上下文，c == ' ' 表示词间隔。UI 需经 ui_async_post。 */
  typedef void (*key_input_char_cb_t)(char c, void *user);
  esp_err_t key_input_init(void);                 /* 创建 5ms tick 任务（挂起状态） */
  /* 按当前设置（输入源、电键类型、对调、WPM）重新配置并启动；退出电键页时 stop */
  void key_input_start(key_input_char_cb_t cb, void *user);
  void key_input_stop(void);
  /* 触摸桨调用：paddle 0 = 左块 1 = 右块（未考虑对调，模块内处理） */
  void key_input_touch(int paddle, bool pressed);
  /* 当前未完成字符的点划串（UI 轮询显示） */
  const char *key_input_pattern(void);
  bool key_input_key_down(void);                  /* 当前是否在发音（UI 高亮） */
  ```
  外接口：`CW_KEY_INPUT_EXT` 时每 tick 读 GPIO22/27（内部上拉，低有效），10ms 去抖（连续 2 tick 一致才采纳）。直键模式下只用 dit 线（GPIO22）/ 左块。

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/key_input.h`：
```c
#ifndef KEY_INPUT_H
#define KEY_INPUT_H
#include <stdbool.h>
#include "esp_err.h"

typedef void (*key_input_char_cb_t)(char c, void *user);

esp_err_t   key_input_init(void);
void        key_input_start(key_input_char_cb_t cb, void *user);
void        key_input_stop(void);
void        key_input_touch(int paddle, bool pressed);
const char *key_input_pattern(void);
bool        key_input_key_down(void);

#endif
```

`firmware/cw-trainer/main/key_input.c`：
```c
#include "key_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "board_config.h"
#include "cw_settings.h"
#include "keyer.h"
#include "cw_decoder.h"
#include "tone_gen.h"

#define TICK_MS 5

static TaskHandle_t s_task;
static volatile bool s_running;
static keyer_t s_keyer;
static cw_decoder_t s_dec;
static volatile bool s_touch[2];
static bool s_ext_last[2], s_ext_stable[2];
static key_input_char_cb_t s_cb;
static void *s_user;
static bool s_last_out;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void read_inputs(bool *dit, bool *dah)
{
    const cw_settings_t *c = cw_settings_get();
    bool a, b;
    if (c->key_input == CW_KEY_INPUT_EXT) {
        bool r0 = gpio_get_level(BOARD_KEY_DIT_GPIO) == 0;
        bool r1 = gpio_get_level(BOARD_KEY_DAH_GPIO) == 0;
        if (r0 == s_ext_last[0]) s_ext_stable[0] = r0; s_ext_last[0] = r0;
        if (r1 == s_ext_last[1]) s_ext_stable[1] = r1; s_ext_last[1] = r1;
        a = s_ext_stable[0]; b = s_ext_stable[1];
    } else {
        a = s_touch[0]; b = s_touch[1];
    }
    if (c->paddle_swap) { bool t = a; a = b; b = t; }
    *dit = a; *dah = b;
}

static void task(void *arg)
{
    (void)arg;
    while (true) {
        if (!s_running) { vTaskSuspend(NULL); continue; }
        uint32_t t = now_ms();
        bool dit, dah;
        read_inputs(&dit, &dah);
        keyer_set_input(&s_keyer, dit, dah);
        bool out = keyer_tick(&s_keyer, t);
        if (out != s_last_out) {
            tone_gen_set(out);
            cw_decoder_key(&s_dec, out, t);
            s_last_out = out;
        }
        char c;
        if (cw_decoder_poll(&s_dec, t, &c) && s_cb) s_cb(c, s_user);
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t key_input_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_KEY_DIT_GPIO) | (1ULL << BOARD_KEY_DAH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    keyer_init(&s_keyer, KEYER_MODE_IAMBIC_B, 60);
    cw_decoder_init(&s_dec, 60);
    return xTaskCreatePinnedToCore(task, "key_input", 3072, NULL, configMAX_PRIORITIES - 3, &s_task, 1) == pdPASS ? ESP_OK : ESP_FAIL;
}

void key_input_start(key_input_char_cb_t cb, void *user)
{
    const cw_settings_t *c = cw_settings_get();
    uint16_t dit = (uint16_t)(1200 / (c->char_wpm ? c->char_wpm : 20));
    keyer_init(&s_keyer, (keyer_mode_t)c->keyer_mode, dit);
    cw_decoder_init(&s_dec, dit);
    s_touch[0] = s_touch[1] = false;
    s_last_out = false;
    s_cb = cb; s_user = user;
    s_running = true;
    vTaskResume(s_task);
}

void key_input_stop(void)
{
    s_running = false;
    tone_gen_set(false);
    s_cb = NULL;
}

void key_input_touch(int paddle, bool pressed)
{
    if (paddle < 0 || paddle > 1) return;
    s_touch[paddle] = pressed;
}

const char *key_input_pattern(void) { return cw_decoder_pattern(&s_dec); }
bool key_input_key_down(void) { return s_last_out; }
```

- [ ] **Step 2: CMake 追加并编译**

SRCS 追加 `"key_input.c"`。
Run: `PROJECT=firmware/cw-trainer tools/idf.sh build`
Expected: `Project build complete.`

- [ ] **Step 3: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：新增电键输入适配（触摸桨 / 外接口）"
```

---

### Task 14: ui_common（导航、顶栏、按钮、字体）+ ui_home

**Files:**
- Create: `firmware/cw-trainer/main/ui/ui_common.h`, `firmware/cw-trainer/main/ui/ui_common.c`, `firmware/cw-trainer/main/ui/ui_home.h`, `firmware/cw-trainer/main/ui/ui_home.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`、`firmware/cw-trainer/main/main.c`

**Interfaces:**
- Produces（ui_common.h）：
  ```c
  LV_FONT_DECLARE(font20); LV_FONT_DECLARE(font28); LV_FONT_DECLARE(EN_BIG);
  #define UI_FONT_CN   (&font20)
  #define UI_FONT_MID  (&font28)
  #define UI_FONT_BIG  (&EN_BIG)
  #define UI_TOPBAR_H  28
  #define UI_GRAY_BG   lv_color_hex(0xEEEEEE)
  #define UI_GRAY_LINE lv_color_hex(0x000000)

  typedef lv_obj_t *(*ui_screen_builder_t)(void);
  /* 创建并切换到新屏幕，异步删除旧屏幕（可在旧屏幕的事件回调里调用）。所有页面通过它导航。 */
  void ui_nav_show(ui_screen_builder_t builder);
  /* 新建空白全屏容器：白底、无内边距、无滚动 */
  lv_obj_t *ui_screen_create(void);
  /* 顶栏：左返回（back_cb 为 NULL 不显示）、中标题。返回顶栏对象，可继续往右侧加控件 */
  lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
  /* 灰阶按钮：黑边白底，按下灰底；文字用 UI_FONT_CN */
  lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
  /* 大块可点区域（键盘键/选项块）：同 ui_button 但 font 可指定 */
  lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user);
  /* 顶部提示条（notice）：灰底黑字一行，返回对象；用 lv_obj_add_flag(HIDDEN) 隐藏 */
  lv_obj_t *ui_notice(lv_obj_t *parent, const char *text);
  /* 简易确认框：覆盖全屏，两个按钮 */
  void ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user);
  ```
  ui_home.h：`lv_obj_t *ui_home_create(void);`、`void ui_home_set_battery(uint8_t percent);`（可在任何时候调用，主页不在前台时忽略）。
  主页每秒更新时钟（`time()` 未同步时显示 `--:--`）；闲置 120s（`lv_disp_get_inactive_time(NULL)`）背光 0，任意触摸恢复到设置亮度。

- [ ] **Step 1: 实现 ui_common**

`firmware/cw-trainer/main/ui/ui_common.h`：
```c
#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "lvgl.h"

LV_FONT_DECLARE(font20);
LV_FONT_DECLARE(font28);
LV_FONT_DECLARE(EN_BIG);
#define UI_FONT_CN   (&font20)
#define UI_FONT_MID  (&font28)
#define UI_FONT_BIG  (&EN_BIG)
#define UI_TOPBAR_H  28
#define UI_GRAY_BG   lv_color_hex(0xEEEEEE)
#define UI_GRAY_DOWN lv_color_hex(0x999999)
#define UI_BLACK     lv_color_hex(0x000000)
#define UI_WHITE     lv_color_hex(0xFFFFFF)

typedef lv_obj_t *(*ui_screen_builder_t)(void);

void      ui_nav_show(ui_screen_builder_t builder);
lv_obj_t *ui_screen_create(void);
lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user);
lv_obj_t *ui_notice(lv_obj_t *parent, const char *text);
void      ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user);

#endif
```

`firmware/cw-trainer/main/ui/ui_common.c`：
```c
#include "ui_common.h"

void ui_nav_show(ui_screen_builder_t builder)
{
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *scr = builder();
    lv_scr_load(scr);
    /* 通常在旧屏幕某个子对象的事件回调里被调用，必须异步删除 */
    if (old && old != scr) lv_obj_del_async(old);
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_WHITE, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(scr, UI_BLACK, 0);
    lv_obj_set_style_text_font(scr, UI_FONT_CN, 0);
    return scr;
}

static void plain(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    plain(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_WHITE, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, UI_BLACK, 0);

    int x = 6;
    if (back_cb) {
        lv_obj_t *back = lv_label_create(bar);
        lv_label_set_text(back, "<");
        lv_obj_set_style_text_font(back, UI_FONT_MID, 0);
        lv_obj_set_size(back, 36, UI_TOPBAR_H);
        lv_obj_set_style_pad_left(back, 6, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(back, 8);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
        x = 40;
    }
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return bar;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    plain(b);
    lv_obj_set_style_bg_color(b, UI_WHITE, 0);
    lv_obj_set_style_bg_color(b, UI_GRAY_DOWN, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, UI_WHITE, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, UI_BLACK, 0);
    lv_obj_set_style_border_color(b, UI_GRAY_DOWN, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, UI_BLACK, 0);
    lv_obj_set_style_text_color(b, UI_GRAY_DOWN, LV_STATE_DISABLED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = make_btn(parent, text, UI_FONT_CN, cb, user);
    lv_obj_set_height(b, 36);
    return b;
}

lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    return make_btn(parent, text, font, cb, user);
}

lv_obj_t *ui_notice(lv_obj_t *parent, const char *text)
{
    lv_obj_t *n = lv_obj_create(parent);
    plain(n);
    lv_obj_set_size(n, LV_PCT(100), 22);
    lv_obj_set_pos(n, 0, UI_TOPBAR_H);
    lv_obj_set_style_bg_color(n, UI_GRAY_BG, 0);
    lv_obj_set_style_border_width(n, 1, 0);
    lv_obj_set_style_border_side(n, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(n, UI_BLACK, 0);
    lv_obj_t *l = lv_label_create(n);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    return n;
}

static void confirm_close(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_del_async(overlay);
}

void ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user)
{
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    plain(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, UI_WHITE, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);

    lv_obj_t *dlg = lv_obj_create(overlay);
    plain(dlg);
    lv_obj_set_size(dlg, 260, 110);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, UI_WHITE, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, UI_BLACK, 0);

    lv_obj_t *l = lv_label_create(dlg);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *no = ui_button(dlg, btn_no, confirm_close, overlay);
    lv_obj_set_width(no, 110);
    lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 12, -12);

    lv_obj_t *yes = ui_button(dlg, btn_yes, yes_cb, user);
    lv_obj_set_width(yes, 110);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
}
```

- [ ] **Step 2: 实现 ui_home**

`firmware/cw-trainer/main/ui/ui_home.h`：
```c
#ifndef UI_HOME_H
#define UI_HOME_H
#include "lvgl.h"
#include <stdint.h>
lv_obj_t *ui_home_create(void);
void      ui_home_set_battery(uint8_t percent);
#endif
```

`firmware/cw-trainer/main/ui/ui_home.c`：
```c
#include "ui_home.h"
#include <stdio.h>
#include <time.h>
#include "ui_common.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "ui_keyer.h"
#include "ui_settings.h"
#include "cw_settings.h"
#include "koch_engine.h"
#include "backlight.h"
#include "battery.h"

#define IDLE_OFF_MS 120000

static lv_obj_t *s_scr, *s_clock, *s_bat;
static lv_timer_t *s_timer;
static bool s_dimmed;
static bool s_reset_notice_shown;

static void tick(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year > 100) lv_label_set_text_fmt(s_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else lv_label_set_text(s_clock, "--:--");

    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (!s_dimmed && idle > IDLE_OFF_MS) { backlight_set(0); s_dimmed = true; }
    else if (s_dimmed && idle < 1000) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    lv_timer_del(s_timer);
    s_timer = NULL; s_scr = NULL;
    if (s_dimmed) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void go_koch(lv_event_t *e)     { (void)e; ui_preview_open(cw_settings_get()->koch_lesson, false); }
static void go_call(lv_event_t *e)     { (void)e; ui_practice_open(TRAINER_MODE_CALLSIGN); }
static void go_qso(lv_event_t *e)      { (void)e; ui_practice_open(TRAINER_MODE_QSO); }
static void go_keyer(lv_event_t *e)    { (void)e; ui_nav_show(ui_keyer_create); }
static void go_settings(lv_event_t *e) { (void)e; ui_nav_show(ui_settings_create); }

lv_obj_t *ui_home_create(void)
{
    const cw_settings_t *cfg = cw_settings_get();
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "", NULL);
    s_clock = lv_label_create(bar);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *gear = lv_label_create(bar);
    lv_label_set_text(gear, "设置");
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 10);
    lv_obj_add_event_cb(gear, go_settings, LV_EVENT_CLICKED, NULL);

    s_bat = lv_label_create(bar);
    lv_obj_align(s_bat, LV_ALIGN_RIGHT_MID, -60, 0);
    ui_home_set_battery(battery_percent());

    int top = UI_TOPBAR_H;
    if (cw_settings_was_reset() && !s_reset_notice_shown) {
        ui_notice(s_scr, "进度已重置（数据损坏或版本不符）");
        s_reset_notice_shown = true;
        top += 22;
    }

    /* 左：Koch 卡片 */
    lv_obj_t *card = lv_btn_create(s_scr);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, UI_WHITE, 0);
    lv_obj_set_style_bg_color(card, UI_GRAY_BG, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(card, UI_BLACK, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_pos(card, 0, top);
    lv_obj_set_size(card, 200, 240 - top);
    lv_obj_add_event_cb(card, go_koch, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l1 = lv_label_create(card);
    lv_obj_set_style_text_font(l1, UI_FONT_MID, 0);
    lv_obj_set_style_text_color(l1, UI_BLACK, 0);
    lv_label_set_text_fmt(l1, "KOCH 第 %u 课", cfg->koch_lesson);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);

    char cs[48]; koch_charset(cfg->koch_lesson, cs);
    lv_obj_t *l2 = lv_label_create(card);
    lv_obj_set_style_text_color(l2, UI_BLACK, 0);
    lv_obj_set_width(l2, 184);
    lv_label_set_long_mode(l2, LV_LABEL_LONG_WRAP);
    if (cfg->koch_lesson == 1 && cfg->koch_last_pct[1] == 0xFF) lv_label_set_text_fmt(l2, "%s", cs);
    else lv_label_set_text_fmt(l2, "已解锁 %s", cs);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t *l3 = lv_label_create(card);
    lv_obj_set_style_text_color(l3, UI_BLACK, 0);
    uint8_t last = cfg->koch_last_pct[cfg->koch_lesson];
    if (last == 0xFF) lv_label_set_text(l3, "未开始");
    else lv_label_set_text_fmt(l3, "上次 %u%%", last);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 0, 96);

    lv_obj_t *l4 = lv_label_create(card);
    lv_obj_set_style_text_font(l4, UI_FONT_MID, 0);
    lv_obj_set_style_text_color(l4, UI_BLACK, 0);
    lv_label_set_text(l4, last == 0xFF ? "开始练习 >" : "继续练习 >");
    lv_obj_align(l4, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* 右：三个入口 */
    const char *names[3] = { "呼号抄收 >", "模拟 QSO >", "电键 >" };
    lv_event_cb_t cbs[3] = { go_call, go_qso, go_keyer };
    int h = (240 - top) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = ui_cell(s_scr, names[i], UI_FONT_CN, cbs[i], NULL);
        lv_obj_set_pos(c, 200, top + i * h);
        lv_obj_set_size(c, 120, h);
        lv_obj_set_style_border_side(c, i < 2 ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_NONE, 0);
    }

    s_dimmed = false;
    s_timer = lv_timer_create(tick, 1000, NULL);
    tick(s_timer);
    return s_scr;
}

void ui_home_set_battery(uint8_t percent)
{
    if (!s_scr || !s_bat) return;
    if (percent < 15) lv_label_set_text_fmt(s_bat, "电 %u%% !", percent);
    else lv_label_set_text_fmt(s_bat, "电 %u%%", percent);
}
```

- [ ] **Step 3: 先给其它页面建桩，让本任务可编译**

创建下列头文件与最小实现（后续任务替换内容）：

`ui/ui_preview.h`：
```c
#ifndef UI_PREVIEW_H
#define UI_PREVIEW_H
#include <stdbool.h>
#include <stdint.h>
/* 打开第 lesson 课的课前预告；review=true（巩固练习）时跳过预告直接进练习 */
void ui_preview_open(uint8_t lesson, bool review);
#endif
```
`ui/ui_preview.c`（桩）：
```c
#include "ui_preview.h"
#include "ui_practice.h"
void ui_preview_open(uint8_t lesson, bool review) { (void)lesson; (void)review; ui_practice_open(TRAINER_MODE_KOCH); }
```
`ui/ui_practice.h`：
```c
#ifndef UI_PRACTICE_H
#define UI_PRACTICE_H
#include "trainer.h"
void ui_practice_open(trainer_mode_t mode);
#endif
```
`ui/ui_practice.c`（桩）：
```c
#include "ui_practice.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static lv_obj_t *build(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "练习（桩）", back); return s; }
void ui_practice_open(trainer_mode_t mode) { (void)mode; ui_nav_show(build); }
```
`ui/ui_keyer.h`：
```c
#ifndef UI_KEYER_H
#define UI_KEYER_H
#include "lvgl.h"
lv_obj_t *ui_keyer_create(void);
#endif
```
`ui/ui_keyer.c`（桩）：
```c
#include "ui_keyer.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
lv_obj_t *ui_keyer_create(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "电键（桩）", back); return s; }
```
`ui/ui_settings.h`：
```c
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H
#include "lvgl.h"
lv_obj_t *ui_settings_create(void);
#endif
```
`ui/ui_settings.c`（桩）：
```c
#include "ui_settings.h"
#include "ui_common.h"
#include "ui_home.h"
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
lv_obj_t *ui_settings_create(void) { lv_obj_t *s = ui_screen_create(); ui_topbar(s, "设置（桩）", back); return s; }
```

- [ ] **Step 4: main.c 改为完整启动顺序**

`firmware/cw-trainer/main/main.c` 全文替换为：
```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "cw_player.h"
#include "key_input.h"
#include "backlight.h"
#include "battery.h"
#include "ui_async.h"
#include "ui_common.h"
#include "ui_home.h"
#include "morse_timing.h"

static const char *TAG = "main";

static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

static void battery_ui(void *arg) { ui_home_set_battery((uint8_t)(uintptr_t)arg); }
static void battery_cb(uint8_t percent, uint16_t mv) { (void)mv; ui_async_post(battery_ui, (void *)(uintptr_t)percent); }

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    nvs_init();
    ESP_ERROR_CHECK(cw_settings_init());
    const cw_settings_t *cfg = cw_settings_get();
    ESP_LOGI(TAG, "cfg: wpm=%u/%u tone=%u lesson=%u reset=%d",
             cfg->char_wpm, cfg->eff_wpm, cfg->tone_hz, cfg->koch_lesson, cw_settings_was_reset());

    /* 音频 */
    if (tone_gen_init() == ESP_OK) {
        tone_gen_set_freq(cfg->tone_hz);
        tone_gen_set_volume(cfg->volume);
    }
    morse_timing_t tm;
    morse_timing_compute(cfg->char_wpm, cfg->eff_wpm, &tm);
    ESP_ERROR_CHECK(cw_player_init());
    cw_player_set_timing(&tm);
    ESP_ERROR_CHECK(key_input_init());

    /* 显示 */
    backlight_init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    const esp_timer_create_args_t tick_args = { .callback = &lv_tick_task, .name = "lv_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));
    backlight_set(cfg->brightness);

    ui_nav_show(ui_home_create);
    ESP_ERROR_CHECK(battery_start(battery_cb));
    ESP_LOGI(TAG, "ui ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_timer_handler();
    }
}
```
CMake SRCS 追加 `"ui/ui_common.c" "ui/ui_home.c" "ui/ui_preview.c" "ui/ui_practice.c" "ui/ui_keyer.c" "ui/ui_settings.c"`。

- [ ] **Step 5: 编译 + 烧录 + 目视**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected：主页出现——顶栏 `--:--`、`电 xx%`、齿轮；左侧 Koch 卡片"KOCH 第 1 课 / KM / 未开始 / 开始练习 >"；右侧三个入口。点每个入口进入对应桩页面，`<` 回主页。放置 2 分钟后背光熄灭，触摸恢复。

- [ ] **Step 6: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：UI 公共组件与主页，其余页面先建桩"
```

---

### Task 15: ui_result + ui_preview + ui_practice（三模式）

**Files:**
- Create: `firmware/cw-trainer/main/ui/ui_result.h`, `firmware/cw-trainer/main/ui/ui_result.c`
- Replace: `firmware/cw-trainer/main/ui/ui_preview.c`, `firmware/cw-trainer/main/ui/ui_practice.c`
- Modify: `firmware/cw-trainer/main/CMakeLists.txt`（追加 `ui/ui_result.c`）

**Interfaces:**
- Consumes: `trainer_t`、`cw_player_play/stop`、`koch_engine`、`cw_settings`、`ui_common`、`ui_async_post`。
- Produces:
  ```c
  /* ui_result.h */
  /* 展示 trainer 结果。Koch 模式达标时把 koch_last_pct[lesson] 写入 NVS；"进入下一课"把 koch_lesson+1 存盘并打开预告 */
  void ui_result_open(const trainer_t *t);
  /* ui_practice.h 追加 */
  void ui_practice_open_lesson(uint8_t lesson);   /* Koch 指定课次（预告页调用） */
  ```
  练习页会话对象 `static trainer_t s_tr;` 生命周期跨页面（结算页读它）。

  练习页状态机（`s_phase`）：`PLAYING`（键盘禁用，播放完 → `INPUT`）、`INPUT`、`FEEDBACK`（显示 800ms 后 `trainer_next()`：有下一组 → PLAYING；否则 → 结算页）。播放完成回调来自 esp_timer 上下文，必须 `ui_async_post` 后再改 UI。

- [ ] **Step 1: ui_result**

`firmware/cw-trainer/main/ui/ui_result.h`：
```c
#ifndef UI_RESULT_H
#define UI_RESULT_H
#include "trainer.h"
void ui_result_open(const trainer_t *t);
#endif
```

`firmware/cw-trainer/main/ui/ui_result.c`：
```c
#include "ui_result.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "cw_settings.h"
#include "koch_engine.h"

static const trainer_t *s_t;

static void go_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void again(lv_event_t *e)
{
    (void)e;
    if (s_t->mode == TRAINER_MODE_KOCH) ui_preview_open(s_t->lesson, true);
    else ui_practice_open(s_t->mode);
}
static void next_lesson(lv_event_t *e)
{
    (void)e;
    cw_settings_t *c = cw_settings_get();
    if (c->koch_lesson < KOCH_LESSON_MAX) { c->koch_lesson++; cw_settings_save(); }
    ui_preview_open(c->koch_lesson, false);
}

static lv_obj_t *build(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "本轮结果", NULL);

    uint8_t pct = trainer_percent(s_t);
    bool koch = s_t->mode == TRAINER_MODE_KOCH;
    bool pass = koch && koch_passed(pct);

    lv_obj_t *big = lv_label_create(s);
    lv_obj_set_style_text_font(big, UI_FONT_BIG, 0);
    lv_label_set_text_fmt(big, "%u%%", pct);
    lv_obj_align(big, LV_ALIGN_TOP_LEFT, 16, 40);

    lv_obj_t *cnt = lv_label_create(s);
    lv_label_set_text_fmt(cnt, "%u / %u 正确", s_t->correct_rounds, TRAINER_ROUNDS);
    lv_obj_align(cnt, LV_ALIGN_TOP_LEFT, 16, 130);

    char err[96] = "";
    if (s_t->error_count == 0) strcpy(err, "全对");
    else {
        strcpy(err, "错字: ");
        for (int i = 0; i < s_t->error_count && strlen(err) < 80; i++) {
            char one[8]; snprintf(one, sizeof one, "%c>%c ", s_t->errors[i].expected, s_t->errors[i].got);
            strcat(err, one);
        }
    }
    lv_obj_t *el = lv_label_create(s);
    lv_obj_set_width(el, 160);
    lv_label_set_long_mode(el, LV_LABEL_LONG_WRAP);
    lv_label_set_text(el, err);
    lv_obj_align(el, LV_ALIGN_TOP_LEFT, 150, 40);

    if (koch) {
        lv_obj_t *pl = lv_label_create(s);
        if (pass) lv_label_set_text_fmt(pl, "达标，可进第 %u 课", (unsigned)(s_t->lesson + 1));
        else lv_label_set_text(pl, "再练练，90% 以上解锁下一课");
        lv_obj_set_width(pl, 160);
        lv_label_set_long_mode(pl, LV_LABEL_LONG_WRAP);
        lv_obj_align(pl, LV_ALIGN_TOP_LEFT, 150, 110);
    }

    int n = (pass && s_t->lesson < KOCH_LESSON_MAX) ? 3 : 2;
    int w = (320 - 12 * (n + 1)) / n, x = 12;
    if (n == 3) {
        char t[24]; snprintf(t, sizeof t, "进入第%u课", (unsigned)(s_t->lesson + 1));
        lv_obj_t *b = ui_button(s, t, next_lesson, NULL);
        lv_obj_set_width(b, w); lv_obj_set_pos(b, x, 196); x += w + 12;
    }
    lv_obj_t *b1 = ui_button(s, "再来一轮", again, NULL);
    lv_obj_set_width(b1, w); lv_obj_set_pos(b1, x, 196); x += w + 12;
    lv_obj_t *b2 = ui_button(s, "返回主页", go_home, NULL);
    lv_obj_set_width(b2, w); lv_obj_set_pos(b2, x, 196);
    return s;
}

void ui_result_open(const trainer_t *t)
{
    s_t = t;
    if (t->mode == TRAINER_MODE_KOCH) {
        cw_settings_t *c = cw_settings_get();
        c->koch_last_pct[t->lesson] = trainer_percent(t);
        cw_settings_save();
    }
    ui_nav_show(build);
}
```

- [ ] **Step 2: ui_preview（替换桩）**

`firmware/cw-trainer/main/ui/ui_preview.c`：
```c
#include "ui_preview.h"
#include <stdio.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_practice.h"
#include "ui_async.h"
#include "cw_player.h"
#include "koch_engine.h"
#include "morse_table.h"

#define REPEATS 3

static uint8_t s_lesson;
static lv_obj_t *s_scr, *s_status;
static int s_played;

static void play_one(void);

static void on_ui_done(void *arg)
{
    (void)arg;
    if (!s_scr) return;
    s_played++;
    if (s_played < REPEATS) play_one();
    else lv_label_set_text(s_status, "播放完毕");
}

static void player_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) ui_async_post(on_ui_done, NULL);
}

static void play_one(void)
{
    char t[2] = { koch_new_char(s_lesson), 0 };
    lv_label_set_text_fmt(s_status, "正在播放 %d / %d", s_played + 1, REPEATS);
    cw_player_play(t, player_cb, NULL);
}

static void on_delete(lv_event_t *e) { (void)e; cw_player_stop(); s_scr = NULL; }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void replay(lv_event_t *e) { (void)e; s_played = 0; play_one(); }
static void start(lv_event_t *e) { (void)e; ui_practice_open_lesson(s_lesson); }

static lv_obj_t *build(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16]; snprintf(title, sizeof title, "第 %u 课", s_lesson);
    ui_topbar(s_scr, title, back);

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "新字母");
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 40, 80);

    char nc = koch_new_char(s_lesson);
    lv_obj_t *big = lv_label_create(s_scr);
    lv_obj_set_style_text_font(big, UI_FONT_BIG, 0);
    lv_label_set_text_fmt(big, "%c", nc);
    lv_obj_align(big, LV_ALIGN_TOP_MID, 20, 36);

    lv_obj_t *pat = lv_label_create(s_scr);
    lv_obj_set_style_text_font(pat, UI_FONT_MID, 0);
    lv_label_set_text(pat, morse_table_lookup(nc));
    lv_obj_align(pat, LV_ALIGN_TOP_MID, 20, 120);

    s_status = lv_label_create(s_scr);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_t *b1 = ui_button(s_scr, "再听一遍", replay, NULL);
    lv_obj_set_width(b1, 130); lv_obj_set_pos(b1, 24, 196);
    lv_obj_t *b2 = ui_button(s_scr, "开始练习", start, NULL);
    lv_obj_set_width(b2, 130); lv_obj_set_pos(b2, 166, 196);

    s_played = 0;
    play_one();
    return s_scr;
}

void ui_preview_open(uint8_t lesson, bool review)
{
    s_lesson = lesson;
    if (review) { ui_practice_open_lesson(lesson); return; }
    ui_nav_show(build);
}
```

- [ ] **Step 3: ui_practice（替换桩）**

`firmware/cw-trainer/main/ui/ui_practice.h`：
```c
#ifndef UI_PRACTICE_H
#define UI_PRACTICE_H
#include <stdint.h>
#include "trainer.h"
void ui_practice_open(trainer_mode_t mode);       /* 呼号 / QSO；Koch 用当前课次 */
void ui_practice_open_lesson(uint8_t lesson);     /* Koch 指定课次 */
#endif
```

`firmware/cw-trainer/main/ui/ui_practice.c`：
```c
#include "ui_practice.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_result.h"
#include "ui_async.h"
#include "cw_player.h"
#include "cw_settings.h"
#include "koch_engine.h"
#include "tone_gen.h"

#define FEEDBACK_MS 800
#define SMALL_KBD_MAX 12   /* 键数 ≤ 12 用左右分栏，否则全宽键盘 */

typedef enum { PH_PLAYING, PH_INPUT, PH_FEEDBACK } phase_t;

static trainer_t s_tr;
static phase_t s_phase;
static lv_obj_t *s_scr, *s_prog, *s_hint, *s_input, *s_fb, *s_replay;
static lv_obj_t *s_keys[44];
static int s_nkeys;
static lv_obj_t *s_opts[4], *s_rst_opts[4], *s_submit;
static int s_sel_call = -1, s_sel_rst = -1;
static char s_buf[8];
static lv_timer_t *s_fb_timer;

static void start_round(void);

/* ---- 工具 ---- */
static void set_keys_enabled(bool en)
{
    for (int i = 0; i < s_nkeys; i++) {
        if (en) lv_obj_clear_state(s_keys[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_keys[i], LV_STATE_DISABLED);
    }
    for (int i = 0; i < 4; i++) {
        if (s_opts[i]) { if (en) lv_obj_clear_state(s_opts[i], LV_STATE_DISABLED); else lv_obj_add_state(s_opts[i], LV_STATE_DISABLED); }
        if (s_rst_opts[i]) { if (en) lv_obj_clear_state(s_rst_opts[i], LV_STATE_DISABLED); else lv_obj_add_state(s_rst_opts[i], LV_STATE_DISABLED); }
    }
    if (s_submit) { if (en) lv_obj_clear_state(s_submit, LV_STATE_DISABLED); else lv_obj_add_state(s_submit, LV_STATE_DISABLED); }
    if (s_replay) { if (en) lv_obj_clear_state(s_replay, LV_STATE_DISABLED); else lv_obj_add_state(s_replay, LV_STATE_DISABLED); }
}

static void render_input(void)
{
    if (s_tr.mode != TRAINER_MODE_KOCH) return;
    char shown[16] = "";
    for (int i = 0; i < KOCH_GROUP_LEN; i++) {
        char c = (i < (int)strlen(s_buf)) ? s_buf[i] : '_';
        size_t l = strlen(shown);
        shown[l] = c; shown[l + 1] = ' '; shown[l + 2] = 0;
    }
    lv_label_set_text(s_input, shown);
}

static void set_hint(const char *t) { if (s_hint) lv_label_set_text(s_hint, t); }

/* ---- 播放 ---- */
static void on_play_done_ui(void *arg)
{
    (void)arg;
    if (!s_scr || s_phase != PH_PLAYING) return;
    s_phase = PH_INPUT;
    set_hint(s_tr.mode == TRAINER_MODE_KOCH ? "请输入听到的 5 个字符" :
             s_tr.mode == TRAINER_MODE_CALLSIGN ? "请选择听到的呼号" : "请选择呼号与 RST");
    set_keys_enabled(true);
}

static void player_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) ui_async_post(on_play_done_ui, NULL);
}

static void play(void)
{
    s_phase = PH_PLAYING;
    set_hint("正在播放中");
    set_keys_enabled(false);
    cw_player_play(s_tr.play_text, player_cb, NULL);
}

static void replay_cb(lv_event_t *e) { (void)e; if (s_phase == PH_INPUT) play(); }

/* ---- 判分 ---- */
static void after_feedback(lv_timer_t *t)
{
    (void)t;
    s_fb_timer = NULL;
    if (!s_scr) return;
    if (trainer_next(&s_tr)) start_round();
    else ui_result_open(&s_tr);
}

static void submit(const char *answer)
{
    if (s_phase != PH_INPUT) return;
    bool ok = trainer_submit(&s_tr, answer);
    s_phase = PH_FEEDBACK;
    set_keys_enabled(false);
    lv_obj_clear_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    if (s_input) lv_obj_add_flag(s_input, LV_OBJ_FLAG_HIDDEN);
    if (s_tr.mode == TRAINER_MODE_KOCH) {
        if (ok) lv_label_set_text_fmt(s_fb, "正确  %s", s_tr.target);
        else {
            char marks[8]; size_t al = strlen(answer);
            for (int i = 0; i < KOCH_GROUP_LEN; i++) marks[i] = ((size_t)i < al && answer[i] == s_tr.target[i]) ? '-' : '^';
            marks[KOCH_GROUP_LEN] = 0;
            lv_label_set_text_fmt(s_fb, "错误  %s\n      %s\n正确  %s", answer, marks, s_tr.target);
        }
    } else if (s_tr.mode == TRAINER_MODE_CALLSIGN) {
        if (ok) lv_label_set_text(s_fb, "正确");
        else lv_label_set_text_fmt(s_fb, "错误，正确是 %s", s_tr.target);
    } else {
        if (ok) lv_label_set_text(s_fb, "正确");
        else lv_label_set_text_fmt(s_fb, "错误，正确是 %s %s", s_tr.target, s_tr.rst);
    }
    s_fb_timer = lv_timer_create(after_feedback, FEEDBACK_MS, NULL);
    lv_timer_set_repeat_count(s_fb_timer, 1);
}

/* ---- Koch 键盘 ---- */
static void key_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT) return;
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    if (strcmp(k, "退格") == 0) { size_t l = strlen(s_buf); if (l) s_buf[l - 1] = 0; }
    else if (strcmp(k, "确定") == 0) { if (strlen(s_buf) == KOCH_GROUP_LEN) submit(s_buf); return; }
    else if (strlen(s_buf) < KOCH_GROUP_LEN) { size_t l = strlen(s_buf); s_buf[l] = k[0]; s_buf[l + 1] = 0; }
    render_input();
}

static void build_koch(lv_obj_t *s)
{
    char cs[48]; int n = koch_charset(s_tr.lesson, cs);
    int total = n + 2;
    bool split = total <= SMALL_KBD_MAX;
    int cols = split ? 4 : (total <= 24 ? 6 : 8);
    int rows = (total + cols - 1) / cols;
    int kx = split ? 170 : 0, ky = split ? UI_TOPBAR_H : UI_TOPBAR_H + 36;
    int kw = split ? 150 : 320, kh = 240 - ky;
    int cw = kw / cols, ch = kh / rows;

    if (split) {
        s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, UI_TOPBAR_H + 8); lv_obj_set_width(s_hint, 156);
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
        s_input = lv_label_create(s); lv_obj_set_style_text_font(s_input, UI_FONT_MID, 0);
        lv_obj_set_pos(s_input, 8, UI_TOPBAR_H + 60);
        s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 8, UI_TOPBAR_H + 60); lv_obj_set_width(s_fb, 156);
        s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_width(s_replay, 100); lv_obj_set_pos(s_replay, 8, 196);
        lv_obj_t *line = lv_obj_create(s); lv_obj_set_size(line, 1, 240 - UI_TOPBAR_H); lv_obj_set_pos(line, 168, UI_TOPBAR_H);
        lv_obj_set_style_bg_color(line, UI_BLACK, 0); lv_obj_set_style_border_width(line, 0, 0); lv_obj_set_style_radius(line, 0, 0);
    } else {
        s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, UI_TOPBAR_H + 8); lv_obj_set_width(s_hint, 110);
        s_input = lv_label_create(s); lv_obj_set_style_text_font(s_input, UI_FONT_MID, 0);
        lv_obj_set_pos(s_input, 124, UI_TOPBAR_H + 2);
        s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 124, UI_TOPBAR_H + 6); lv_obj_set_width(s_fb, 130);
        s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_size(s_replay, 56, 30); lv_obj_set_pos(s_replay, 258, UI_TOPBAR_H + 3);
    }
    lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);

    s_nkeys = 0;
    for (int i = 0; i < total; i++) {
        char t[8];
        if (i < n) { t[0] = cs[i]; t[1] = 0; }
        else strcpy(t, (i == n) ? "退格" : "确定");
        lv_obj_t *k = ui_cell(s, t, (i < n) ? UI_FONT_MID : UI_FONT_CN, key_cb, NULL);
        lv_obj_set_pos(k, kx + (i % cols) * cw, ky + (i / cols) * ch);
        lv_obj_set_size(k, cw, ch);
        s_keys[s_nkeys++] = k;
    }
}

/* ---- 呼号 / QSO ---- */
static void opt_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_tr.mode == TRAINER_MODE_CALLSIGN) { submit(s_tr.options[i]); return; }
    s_sel_call = i;
    for (int k = 0; k < 4; k++) lv_obj_set_style_bg_color(s_opts[k], k == i ? UI_GRAY_DOWN : UI_WHITE, 0);
}
static void rst_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel_rst = i;
    for (int k = 0; k < 4; k++) lv_obj_set_style_bg_color(s_rst_opts[k], k == i ? UI_GRAY_DOWN : UI_WHITE, 0);
}
static void submit_qso_cb(lv_event_t *e)
{
    (void)e;
    if (s_sel_call < 0 || s_sel_rst < 0) return;
    char ans[16]; snprintf(ans, sizeof ans, "%s %s", s_tr.options[s_sel_call], s_tr.rst_options[s_sel_rst]);
    submit(ans);
}

static void build_choice(lv_obj_t *s)
{
    int top = UI_TOPBAR_H;
    if (s_tr.charset_fallback) { ui_notice(s, "建议先练 Koch，当前使用全部字母"); top += 22; }
    s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, top + 10); lv_obj_set_width(s_hint, 220);
    s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_size(s_replay, 70, 30); lv_obj_set_pos(s_replay, 242, top + 5);
    s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 8, top + 10); lv_obj_set_width(s_fb, 230);
    lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    int y = top + 40;

    if (s_tr.mode == TRAINER_MODE_CALLSIGN) {
        int h = (240 - y) / 2;
        for (int i = 0; i < 4; i++) {
            s_opts[i] = ui_cell(s, s_tr.options[i], UI_FONT_MID, opt_cb, (void *)(intptr_t)i);
            lv_obj_set_pos(s_opts[i], (i % 2) * 160, y + (i / 2) * h);
            lv_obj_set_size(s_opts[i], 160, h);
        }
        return;
    }
    lv_obj_t *l1 = lv_label_create(s); lv_label_set_text(l1, "对方呼号"); lv_obj_set_pos(l1, 8, y);
    for (int i = 0; i < 4; i++) {
        s_opts[i] = ui_cell(s, s_tr.options[i], UI_FONT_CN, opt_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(s_opts[i], 8 + i * 77, y + 24); lv_obj_set_size(s_opts[i], 74, 40);
    }
    lv_obj_t *l2 = lv_label_create(s); lv_label_set_text(l2, "RST"); lv_obj_set_pos(l2, 8, y + 70);
    for (int i = 0; i < 4; i++) {
        s_rst_opts[i] = ui_cell(s, s_tr.rst_options[i], UI_FONT_CN, rst_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(s_rst_opts[i], 8 + i * 77, y + 94); lv_obj_set_size(s_rst_opts[i], 74, 40);
    }
    s_submit = ui_button(s, "提交", submit_qso_cb, NULL);
    lv_obj_set_width(s_submit, 100); lv_obj_set_pos(s_submit, 212, 200);
}

/* ---- 页面 ---- */
static void exit_yes(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void back(lv_event_t *e) { (void)e; ui_confirm("退出后本轮不计入进度", "继续练", "退出", exit_yes, NULL); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    cw_player_stop();
    if (s_fb_timer) { lv_timer_del(s_fb_timer); s_fb_timer = NULL; }
    s_scr = NULL; s_hint = s_input = s_fb = s_replay = s_submit = NULL;
    memset(s_opts, 0, sizeof s_opts); memset(s_rst_opts, 0, sizeof s_rst_opts); s_nkeys = 0;
}

static void start_round(void)
{
    s_buf[0] = 0; s_sel_call = s_sel_rst = -1;
    lv_label_set_text_fmt(s_prog, "%u/%u", s_tr.round + 1, TRAINER_ROUNDS);
    if (s_fb) lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    if (s_input) { lv_obj_clear_flag(s_input, LV_OBJ_FLAG_HIDDEN); render_input(); }
    /* 选项文本随每轮更新 */
    for (int i = 0; i < 4; i++) {
        if (s_opts[i]) { lv_label_set_text(lv_obj_get_child(s_opts[i], 0), s_tr.options[i]); lv_obj_set_style_bg_color(s_opts[i], UI_WHITE, 0); }
        if (s_rst_opts[i]) { lv_label_set_text(lv_obj_get_child(s_rst_opts[i], 0), s_tr.rst_options[i]); lv_obj_set_style_bg_color(s_rst_opts[i], UI_WHITE, 0); }
    }
    play();
}

static lv_obj_t *build(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16];
    if (s_tr.mode == TRAINER_MODE_KOCH) snprintf(title, sizeof title, "第 %u 课", s_tr.lesson);
    else strcpy(title, s_tr.mode == TRAINER_MODE_CALLSIGN ? "呼号抄收" : "模拟 QSO");
    lv_obj_t *bar = ui_topbar(s_scr, title, back);
    s_prog = lv_label_create(bar);
    lv_obj_align(s_prog, LV_ALIGN_RIGHT_MID, -8, 0);

    if (!tone_gen_is_ok()) ui_notice(s_scr, "音频异常，无法播放，请退出重试");

    if (s_tr.mode == TRAINER_MODE_KOCH) build_koch(s_scr);
    else build_choice(s_scr);

    start_round();
    return s_scr;
}

static void open_with(trainer_mode_t mode, uint8_t lesson)
{
    trainer_start(&s_tr, mode, lesson, (uint32_t)esp_timer_get_time());
    ui_nav_show(build);
}

void ui_practice_open(trainer_mode_t mode) { open_with(mode, cw_settings_get()->koch_lesson); }
void ui_practice_open_lesson(uint8_t lesson) { open_with(TRAINER_MODE_KOCH, lesson); }
```

- [ ] **Step 4: CMake 追加、编译、烧录验证三条流程**

SRCS 追加 `"ui/ui_result.c"`。
Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected：
1. 主页 → 开始练习 → 预告页播 `M` 三遍并显示 `--` → 开始练习 → 播放完键盘启用 → 敲 5 个 → ✓ → 反馈 0.8s → 下一组 … 10 组后结算页显示百分比；全对时出现"进入第2课"，点击后主页显示"第 2 课"。
2. 主页 → 呼号抄收 → 第 1 课时顶部出现"建议先练 Koch"提示 → 播放完点选 → 反馈 → … → 结算无"下一课"按钮。
3. 主页 → 模拟 QSO → 听整句 → 选呼号 + RST → 提交。
4. 练习中点 `<` 出现确认框；"退出"回主页且主页"上次"不变。

- [ ] **Step 5: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：课前预告、听写练习（Koch/呼号/QSO）与结算页"
```

---

### Task 16: ui_keyer 电键页

**Files:**
- Replace: `firmware/cw-trainer/main/ui/ui_keyer.c`

**Interfaces:**
- Consumes: `key_input_*`、`cw_settings`、`tone_gen_is_ok()`、`ui_async_post`。

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/ui/ui_keyer.c`：
```c
#include "ui_keyer.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_async.h"
#include "key_input.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "keyer.h"

#define TEXT_MAX 40   /* 2 行 × 20 字符（等宽 font28 约 16px/字） */

static lv_obj_t *s_scr, *s_text, *s_seq, *s_pad[2];
static char s_buf[TEXT_MAX + 1];
static lv_timer_t *s_timer;

static void append_ui(void *arg)
{
    char c = (char)(intptr_t)arg;
    if (!s_scr) return;
    size_t l = strlen(s_buf);
    if (l >= TEXT_MAX) { memmove(s_buf, s_buf + 1, TEXT_MAX); l = TEXT_MAX - 1; }
    s_buf[l] = c; s_buf[l + 1] = 0;
    lv_label_set_text(s_text, s_buf);
}

static void char_cb(char c, void *user) { (void)user; ui_async_post(append_ui, (void *)(intptr_t)c); }

static void poll(lv_timer_t *t)
{
    (void)t;
    const char *p = key_input_pattern();
    /* 点划之间加空格显示，如 ". - . ." */
    char shown[24] = ""; size_t n = 0;
    for (; *p && n < sizeof shown - 3; p++) { shown[n++] = *p; shown[n++] = ' '; shown[n] = 0; }
    lv_label_set_text(s_seq, shown);
    bool down = key_input_key_down();
    if (cw_settings_get()->key_input == CW_KEY_INPUT_EXT)
        for (int i = 0; i < 2; i++) if (s_pad[i]) lv_obj_set_style_bg_color(s_pad[i], down ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void pad_cb(lv_event_t *e)
{
    int paddle = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) key_input_touch(paddle, true);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) key_input_touch(paddle, false);
}

static void clear_cb(lv_event_t *e) { (void)e; s_buf[0] = 0; lv_label_set_text(s_text, ""); }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void on_delete(lv_event_t *e) { (void)e; key_input_stop(); lv_timer_del(s_timer); s_timer = NULL; s_scr = NULL; }

lv_obj_t *ui_keyer_create(void)
{
    const cw_settings_t *cfg = cw_settings_get();
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16]; snprintf(title, sizeof title, "%u WPM", cfg->char_wpm);
    lv_obj_t *bar = ui_topbar(s_scr, title, back);
    lv_obj_t *clr = ui_button(bar, "清屏", clear_cb, NULL);
    lv_obj_set_size(clr, 60, 24);
    lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -4, 0);

    int top = UI_TOPBAR_H;
    if (!tone_gen_is_ok()) { ui_notice(s_scr, "音频异常，侧音不可用"); top += 22; }

    s_text = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_text, UI_FONT_MID, 0);
    lv_obj_set_pos(s_text, 8, top + 4);
    lv_obj_set_size(s_text, 304, 60);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    s_buf[0] = 0;

    s_seq = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_seq, UI_FONT_MID, 0);
    lv_obj_align(s_seq, LV_ALIGN_TOP_MID, 0, top + 66);

    int py = 120, ph = 240 - py;
    bool straight = cfg->keyer_mode == KEYER_MODE_STRAIGHT;
    bool ext = cfg->key_input == CW_KEY_INPUT_EXT;
    if (ext) {
        lv_obj_t *l = lv_label_create(s_scr); lv_label_set_text(l, "外接电键已就绪"); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, py + 8);
        for (int i = 0; i < (straight ? 1 : 2); i++) {
            s_pad[i] = ui_cell(s_scr, straight ? "键" : (i == 0 ? "点" : "划"), UI_FONT_MID, NULL, NULL);
            lv_obj_set_size(s_pad[i], 90, 60);
            lv_obj_set_pos(s_pad[i], straight ? 115 : 60 + i * 110, py + 44);
            lv_obj_clear_flag(s_pad[i], LV_OBJ_FLAG_CLICKABLE);
        }
    } else if (straight) {
        s_pad[0] = ui_cell(s_scr, "直键（按住）", UI_FONT_MID, NULL, NULL);
        lv_obj_set_pos(s_pad[0], 0, py); lv_obj_set_size(s_pad[0], 320, ph);
        lv_obj_add_event_cb(s_pad[0], pad_cb, LV_EVENT_ALL, (void *)0);
        s_pad[1] = NULL;
    } else {
        for (int i = 0; i < 2; i++) {
            s_pad[i] = ui_cell(s_scr, i == 0 ? "点" : "划", UI_FONT_MID, NULL, NULL);
            lv_obj_set_pos(s_pad[i], i * 160, py); lv_obj_set_size(s_pad[i], 160, ph);
            lv_obj_add_event_cb(s_pad[i], pad_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        }
    }

    key_input_start(char_cb, NULL);
    s_timer = lv_timer_create(poll, 50, NULL);
    return s_scr;
}
```

- [ ] **Step 2: 编译 + 烧录验证**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected：电键页按左块立刻有侧音且只发 1 个点、按住连发；右块划；同时按交替；松手后 2 dit 内解码出字符显示到文本区，`· −` 序列实时更新；`清屏` 清空；`<` 回主页并静音。侧音按下到出声的延迟主观上无感（<20ms）。

- [ ] **Step 3: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：电键页（触摸桨 / 直键 / 外接指示）"
```

---

### Task 17: ui_settings（分组列表、数值调整、单选、恢复出厂）

**Files:**
- Replace: `firmware/cw-trainer/main/ui/ui_settings.c`

**Interfaces:**
- Consumes: `cw_settings`、`cw_player`（试听）、`tone_gen_set_freq/volume`、`backlight_set`、`morse_timing`。
- 设置项表（组、名、类型）：
  - 音频：字符速度（数值 5..40 步 1，试听）、有效速度（5..char_wpm）、音调（400..1000 步 50）、音量（0..100 步 10）
  - 电键：电键输入（单选 触摸屏/外接口）、电键类型（单选 直键/Iambic A/Iambic B）、桨键左右对调（开关）
  - 常规：我的呼号（呼号编辑键盘 4×10）、Koch 课次（1..39）、屏幕亮度（10..100 步 10，实时生效）、恢复出厂设置、版本
- 每次修改立即 `cw_settings_save()` 并应用到 tone_gen / cw_player / backlight。

- [ ] **Step 1: 实现**

`firmware/cw-trainer/main/ui/ui_settings.c`：
```c
#include "ui_settings.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "cw_settings.h"
#include "cw_player.h"
#include "tone_gen.h"
#include "backlight.h"
#include "morse_timing.h"
#include "koch_engine.h"

#define APP_VERSION "0.1.0"

typedef enum { IT_NUM, IT_CHOICE, IT_TOGGLE, IT_CALLSIGN, IT_RESET, IT_INFO } item_type_t;
typedef struct {
    const char *name; item_type_t type;
    int min, max, step; const char *unit;
    const char *const *choices; int nchoices;
    bool listen;   /* 数值页显示"试听" */
} item_t;

static const char *const KEY_INPUTS[] = { "触摸屏", "外接口" };
static const char *const KEYER_MODES[] = { "直键", "Iambic A", "Iambic B" };

static const item_t AUDIO[] = {
    { "字符速度", IT_NUM, 5, 40, 1, " WPM", NULL, 0, true },
    { "有效速度", IT_NUM, 5, 40, 1, " WPM", NULL, 0, true },
    { "音调",     IT_NUM, 400, 1000, 50, " Hz", NULL, 0, true },
    { "音量",     IT_NUM, 0, 100, 10, "%", NULL, 0, true },
};
static const item_t KEY[] = {
    { "电键输入", IT_CHOICE, 0, 0, 0, "", KEY_INPUTS, 2, false },
    { "电键类型", IT_CHOICE, 0, 0, 0, "", KEYER_MODES, 3, false },
    { "桨键左右对调", IT_TOGGLE, 0, 0, 0, "", NULL, 0, false },
};
static const item_t GEN[] = {
    { "我的呼号", IT_CALLSIGN, 0, 0, 0, "", NULL, 0, false },
    { "Koch 课次", IT_NUM, 1, 39, 1, "", NULL, 0, false },
    { "屏幕亮度", IT_NUM, 10, 100, 10, "%", NULL, 0, false },
    { "恢复出厂设置", IT_RESET, 0, 0, 0, "", NULL, 0, false },
    { "版本", IT_INFO, 0, 0, 0, "", NULL, 0, false },
};
static const item_t *const GROUPS[3] = { AUDIO, KEY, GEN };
static const int GROUP_N[3] = { 4, 3, 5 };
static const char *const GROUP_NAMES[3] = { "音频", "电键", "常规" };

static int s_group = 0;
static const item_t *s_item;
static lv_obj_t *s_scr, *s_list, *s_tabs[3];
static lv_obj_t *s_val_label;
static char s_call_buf[12];

/* ---- 读写设置字段 ---- */
static int get_val(const item_t *it)
{
    cw_settings_t *c = cw_settings_get();
    if (!strcmp(it->name, "字符速度")) return c->char_wpm;
    if (!strcmp(it->name, "有效速度")) return c->eff_wpm;
    if (!strcmp(it->name, "音调")) return c->tone_hz;
    if (!strcmp(it->name, "音量")) return c->volume;
    if (!strcmp(it->name, "电键输入")) return c->key_input;
    if (!strcmp(it->name, "电键类型")) return c->keyer_mode;
    if (!strcmp(it->name, "桨键左右对调")) return c->paddle_swap;
    if (!strcmp(it->name, "Koch 课次")) return c->koch_lesson;
    if (!strcmp(it->name, "屏幕亮度")) return c->brightness;
    return 0;
}

static void apply_audio(void)
{
    cw_settings_t *c = cw_settings_get();
    morse_timing_t tm; morse_timing_compute(c->char_wpm, c->eff_wpm, &tm);
    cw_player_set_timing(&tm);
    tone_gen_set_freq(c->tone_hz);
    tone_gen_set_volume(c->volume);
}

static void set_val(const item_t *it, int v)
{
    cw_settings_t *c = cw_settings_get();
    if (!strcmp(it->name, "字符速度")) { c->char_wpm = (uint8_t)v; if (c->eff_wpm > c->char_wpm) c->eff_wpm = c->char_wpm; }
    else if (!strcmp(it->name, "有效速度")) { if (v > c->char_wpm) v = c->char_wpm; c->eff_wpm = (uint8_t)v; }
    else if (!strcmp(it->name, "音调")) c->tone_hz = (uint16_t)v;
    else if (!strcmp(it->name, "音量")) c->volume = (uint8_t)v;
    else if (!strcmp(it->name, "电键输入")) c->key_input = (uint8_t)v;
    else if (!strcmp(it->name, "电键类型")) c->keyer_mode = (uint8_t)v;
    else if (!strcmp(it->name, "桨键左右对调")) c->paddle_swap = (uint8_t)v;
    else if (!strcmp(it->name, "Koch 课次")) c->koch_lesson = (uint8_t)v;
    else if (!strcmp(it->name, "屏幕亮度")) { c->brightness = (uint8_t)v; backlight_set(c->brightness); }
    apply_audio();
    cw_settings_save();
}

static void fmt_val(const item_t *it, char *out, size_t n)
{
    cw_settings_t *c = cw_settings_get();
    switch (it->type) {
    case IT_NUM: snprintf(out, n, "%d%s", get_val(it), it->unit); break;
    case IT_CHOICE: snprintf(out, n, "%s", it->choices[get_val(it)]); break;
    case IT_TOGGLE: snprintf(out, n, "%s", get_val(it) ? "开" : "关"); break;
    case IT_CALLSIGN: snprintf(out, n, "%s", c->callsign); break;
    case IT_INFO: snprintf(out, n, "%s", APP_VERSION); break;
    default: out[0] = 0;
    }
}

/* ---- 子页面 ---- */
static void back_to_settings(lv_event_t *e) { (void)e; ui_nav_show(ui_settings_create); }

static void adj_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = get_val(s_item) + d * s_item->step;
    if (v < s_item->min) v = s_item->min;
    if (v > s_item->max) v = s_item->max;
    set_val(s_item, v);
    char b[16]; fmt_val(s_item, b, sizeof b);
    lv_label_set_text(s_val_label, b);
}
static void listen_cb(lv_event_t *e) { (void)e; cw_player_play("PARIS", NULL, NULL); }

static lv_obj_t *build_adjust(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, s_item->name, back_to_settings);
    lv_obj_t *minus = ui_cell(s, "-", UI_FONT_BIG, adj_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(minus, 64, 64); lv_obj_set_pos(minus, 24, 70);
    lv_obj_t *plus = ui_cell(s, "+", UI_FONT_BIG, adj_cb, (void *)(intptr_t)1);
    lv_obj_set_size(plus, 64, 64); lv_obj_set_pos(plus, 232, 70);
    s_val_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_val_label, UI_FONT_MID, 0);
    char b[16]; fmt_val(s_item, b, sizeof b);
    lv_label_set_text(s_val_label, b);
    lv_obj_align(s_val_label, LV_ALIGN_TOP_MID, 0, 88);
    if (s_item->listen) {
        lv_obj_t *l = ui_button(s, "试听", listen_cb, NULL);
        lv_obj_set_width(l, 120); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 170);
    }
    return s;
}

static void choice_cb(lv_event_t *e)
{
    set_val(s_item, (int)(intptr_t)lv_event_get_user_data(e));
    ui_nav_show(ui_settings_create);
}
static lv_obj_t *build_choice(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, s_item->name, back_to_settings);
    int cur = get_val(s_item);
    for (int i = 0; i < s_item->nchoices; i++) {
        char t[24]; snprintf(t, sizeof t, "%s %s", i == cur ? "*" : "  ", s_item->choices[i]);
        lv_obj_t *c = ui_cell(s, t, UI_FONT_CN, choice_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(c, 0, UI_TOPBAR_H + i * 52); lv_obj_set_size(c, 320, 52);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_BOTTOM, 0);
    }
    return s;
}

static void reset_yes(lv_event_t *e) { (void)e; cw_settings_reset(); apply_audio(); backlight_set(cw_settings_get()->brightness); ui_nav_show(ui_home_create); }
static lv_obj_t *build_reset(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "恢复出厂设置", back_to_settings);
    lv_obj_t *l = lv_label_create(s);
    lv_label_set_text(l, "将清除 Koch 进度和全部设置，不可恢复");
    lv_obj_set_width(l, 280); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t *no = ui_button(s, "取消", back_to_settings, NULL); lv_obj_set_width(no, 120); lv_obj_set_pos(no, 30, 160);
    lv_obj_t *yes = ui_button(s, "确认清除", reset_yes, NULL); lv_obj_set_width(yes, 120); lv_obj_set_pos(yes, 170, 160);
    return s;
}

/* 呼号编辑：4 行 × 9 列 + 退格/确定 */
static lv_obj_t *s_call_label;
static void call_key_cb(lv_event_t *e)
{
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    size_t l = strlen(s_call_buf);
    if (!strcmp(k, "退格")) { if (l) s_call_buf[l - 1] = 0; }
    else if (!strcmp(k, "确定")) {
        if (l >= 3) { strcpy(cw_settings_get()->callsign, s_call_buf); cw_settings_save(); }
        ui_nav_show(ui_settings_create); return;
    } else if (l < 10) { s_call_buf[l] = k[0]; s_call_buf[l + 1] = 0; }
    lv_label_set_text(s_call_label, s_call_buf);
}
static lv_obj_t *build_callsign(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "我的呼号", back_to_settings);
    strcpy(s_call_buf, cw_settings_get()->callsign);
    s_call_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_call_label, UI_FONT_MID, 0);
    lv_label_set_text(s_call_label, s_call_buf);
    lv_obj_align(s_call_label, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + 2);
    const char *keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int cols = 10, cw = 32, ch = 42, y0 = UI_TOPBAR_H + 40;
    for (int i = 0; i < 36; i++) {
        char t[2] = { keys[i], 0 };
        lv_obj_t *k = ui_cell(s, t, UI_FONT_CN, call_key_cb, NULL);
        lv_obj_set_pos(k, (i % cols) * cw, y0 + (i / cols) * ch); lv_obj_set_size(k, cw, ch);
    }
    lv_obj_t *bs = ui_cell(s, "退格", UI_FONT_CN, call_key_cb, NULL);
    lv_obj_set_pos(bs, 6 * cw, y0 + 3 * ch); lv_obj_set_size(bs, 2 * cw, ch);
    lv_obj_t *ok = ui_cell(s, "确定", UI_FONT_CN, call_key_cb, NULL);
    lv_obj_set_pos(ok, 8 * cw, y0 + 3 * ch); lv_obj_set_size(ok, 2 * cw, ch);
    return s;
}

/* ---- 主设置页 ---- */
static void item_cb(lv_event_t *e)
{
    s_item = lv_event_get_user_data(e);
    switch (s_item->type) {
    case IT_NUM: ui_nav_show(build_adjust); break;
    case IT_CHOICE: ui_nav_show(build_choice); break;
    case IT_TOGGLE: set_val(s_item, !get_val(s_item)); ui_nav_show(ui_settings_create); break;
    case IT_CALLSIGN: ui_nav_show(build_callsign); break;
    case IT_RESET: ui_nav_show(build_reset); break;
    default: break;
    }
}

static void fill_list(void)
{
    lv_obj_clean(s_list);
    for (int i = 0; i < GROUP_N[s_group]; i++) {
        const item_t *it = &GROUPS[s_group][i];
        char v[24]; fmt_val(it, v, sizeof v);
        char t[48]; snprintf(t, sizeof t, "%s", it->name);
        lv_obj_t *row = ui_cell(s_list, t, UI_FONT_CN, it->type == IT_INFO ? NULL : item_cb, (void *)it);
        lv_obj_set_size(row, 260, 42);
        lv_obj_set_pos(row, 0, i * 42);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text_fmt(vl, "%s %s", v, it->type == IT_INFO ? "" : ">");
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    for (int g = 0; g < 3; g++) lv_obj_set_style_bg_color(s_tabs[g], g == s_group ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void tab_cb(lv_event_t *e) { s_group = (int)(intptr_t)lv_event_get_user_data(e); fill_list(); }
static void back_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

lv_obj_t *ui_settings_create(void)
{
    s_scr = ui_screen_create();
    ui_topbar(s_scr, "设置", back_home);
    int h = (240 - UI_TOPBAR_H) / 3;
    for (int g = 0; g < 3; g++) {
        s_tabs[g] = ui_cell(s_scr, GROUP_NAMES[g], UI_FONT_CN, tab_cb, (void *)(intptr_t)g);
        lv_obj_set_pos(s_tabs[g], 0, UI_TOPBAR_H + g * h); lv_obj_set_size(s_tabs[g], 60, h);
    }
    s_list = lv_obj_create(s_scr);
    lv_obj_set_pos(s_list, 60, UI_TOPBAR_H); lv_obj_set_size(s_list, 260, 240 - UI_TOPBAR_H);
    lv_obj_set_style_pad_all(s_list, 0, 0); lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_border_width(s_list, 1, 0); lv_obj_set_style_border_side(s_list, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(s_list, UI_BLACK, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    fill_list();
    return s_scr;
}
```

- [ ] **Step 2: 编译 + 烧录验证**

Run: `PROJECT=firmware/cw-trainer tools/idf.sh build flash monitor`
Expected：设置页三组切换；字符速度 ± 后"试听"播 PARIS 速度随之变化；有效速度不能超过字符速度；音调/音量立即生效；电键类型改直键后电键页变整块；桨键对调后左右互换；呼号键盘可编辑并保存；Koch 课次改 7 后主页显示第 7 课且键盘 10 键；亮度实时变化；恢复出厂后回到主页"第 1 课 · 未开始"。重启后所有设置保留。

- [ ] **Step 3: 提交**

```bash
git add firmware/cw-trainer/main
git commit -m "CW：设置页（分组列表、数值调整、单选、呼号编辑、恢复出厂）"
```

---

### Task 18: 收尾——文档、README、全量验证

**Files:**
- Create: `firmware/cw-trainer/README.md`
- Modify: `README.md`（根）、`docs/build-setup.md`

- [ ] **Step 1: 写工程 README**

`firmware/cw-trainer/README.md`：
````markdown
# CW Trainer

ESP32-2432S0248R 上的 CW（莫尔斯电码）练习机固件。设计见 `docs/superpowers/specs/2026-08-25-cw-trainer-design.md`。

## 构建 / 烧录

```bash
PROJECT=firmware/cw-trainer tools/idf.sh build
PROJECT=firmware/cw-trainer tools/idf.sh flash monitor
```

NVS 命名空间 `cwtrn`，与 fmo-companion 互不影响。

## 主机测试

```bash
make -C firmware/cw-trainer/test
```

`main/core/` 下的模块不依赖 ESP-IDF，可在 macOS 直接编译测试。

## 结构

- `main/core/` 莫尔斯表、时序、电键状态机、解码器、Koch 引擎、呼号生成、练习会话
- `main/*.c` 侧音（I2S DAC）、文本发报、电键输入、NVS 设置、电量、背光
- `main/ui/` LVGL 页面（阶段 1 为灰阶版）

## 外接电键

设置 → 电键 → 电键输入 = 外接口。接线：CN3 IO22 = 点（直键也用此线），IO27 = 划，GND 公共；低电平有效，内部上拉。
````

- [ ] **Step 2: 更新根 README 与 build-setup**

根 `README.md` "现状"列表末尾追加：
```markdown
- 🔄 **CW 练习机固件** → `firmware/cw-trainer`（独立固件，Koch 听写 / 呼号抄收 / 触摸桨电键，阶段 1 灰阶 UI）
```
`docs/build-setup.md` "多固件开发"一节的示例后追加一行：
```markdown
现有工程：`firmware/fmo-companion`（默认）、`firmware/cw-trainer`、`firmware/sdfont-bench`。
```

- [ ] **Step 3: 全量验证**

Run: `make -C firmware/cw-trainer/test && PROJECT=firmware/cw-trainer tools/idf.sh build`
Expected: 7 个测试程序全部 `0 failed`；固件编译通过。烧录后按 spec §5 状态清单逐项目视：首次启动 / 进度重置（可用 `tools/esp.sh erase_region 0x9000 0x6000` 后重启触发首次启动路径）/ 低电（拔 USB 用电池跑到 <15%，可选）/ 练习各状态 / 结算各状态 / 电键三种布局。

- [ ] **Step 4: 提交**

```bash
git add firmware/cw-trainer/README.md README.md docs/build-setup.md
git commit -m "CW：工程文档与总览更新"
```

---

## 阶段 2（另开计划）

视觉风格：确定风格关键词与 1 个 accent 色后，在灰阶版上应用；不改交互结构。触发条件：阶段 1 在设备上真实用一轮并确认好用。
