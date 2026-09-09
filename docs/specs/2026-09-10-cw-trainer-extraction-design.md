# cw-trainer 抽离为独立开源仓库

2026-09-10 定稿。

把 `firmware/cw-trainer` 从本仓库抽出，成为公开仓库
`Johnson-LYS/esp32-cw-trainer`，并由 GitHub Actions 按 tag 构建产物、创建 release。

## 为什么不能直接把本仓库公开

`backup/stock/` 里的原厂固件整片镜像含**明文 WiFi 密码**（原厂固件把凭据存在
NVS 里，根 `README.md` 自己写了这条警告）。这些镜像在历史里，删掉文件不够。
所以抽离不是「改个可见性」，而是**重建一份只含该公开内容的历史**。

同时历史里还埋着若干与 cw-trainer 无关、也不该公开的东西：另一个项目
`firmware/vibebox` 与 `host/`（Go）、`components/trombik__esp_wireguard`、
早期 `main/fonts/lv_customer_font_simhei_*.c`（SimHei 是微软字体）、厂商开发文档
PDF、以及 `tools/morsekob/`（wire 服务器与机器人的服务端代码，含部署主机信息）。
过滤必须是**白名单**，不是黑名单。

## 目标结构

工程本体提到仓库根 —— ESP-IDF 的惯例，`idf.py build` 直接在根目录跑，
`EXTRA_COMPONENT_DIRS` 那段跨目录胶水整段消失。

```
esp32-cw-trainer/
├── CMakeLists.txt          PROJECT_VER 仍是权威版本号，但 tag 现在必须等于它
├── partitions.csv  sdkconfig.defaults
├── main/                   原样，main/core/ 与 main/ui/ 路径不变
├── test/                   13 个主机测试、127 个用例
├── components/
│   ├── lvgl/               裁剪后约 16M（原 99M）
│   ├── lvgl_esp32_drivers/  lv_porting/
│   └── fonts/              只留 cw-trainer 编译用的 4 个，约 1M（原 10M）
├── tools/
│   └── font-src/font20.c   7M 全量字库，不参与编译，只作 subset_font.py 的原料
├── docs/                   releases/ design/ specs/ plans/ + 硬件、构建、发布流程
├── .github/workflows/      ci.yml  release.yml
└── LICENSE  NOTICE.md  README.md
```

`tools/font-src/font20.c` 是有意保留的 7M：cw-trainer 编译时用的是 356K 的裁剪版
`font20_cwtrn.c`，全量字库只在**改中文文案后重新裁剪**时需要。扔掉它，别人 fork
之后改一句中文，`check_charset.py` 会拦下来而他无从补救。挪出编译路径、名字上
讲清它是原料不是产物。

## 抽离方式：git filter-repo 白名单

在 `git clone --no-local` 出来的副本上跑，原仓库不动。

**保留并改写路径**

| 原路径 | 新路径 |
|---|---|
| `firmware/cw-trainer/` | 仓库根 |
| `fonts/` | `components/fonts/` |
| `fonts/font20.c` | `tools/font-src/font20.c` |
| `docs/superpowers/specs/` | `docs/specs/` |
| `docs/superpowers/plans/` | `docs/plans/` |
| `components/{lvgl,lvgl_esp32_drivers,lv_porting}/`、`tools/` | 原位 |

**保留的 docs**：`releases/`、`design/cw-trainer-*.html`、`hardware.md`、
`build-setup.md`、`release-process.md`、`optimization-backlog.md`、
`upstream-source.md`（这份是上游 MIT 的溯源证据，`NOTICE.md` 要引它）、
`IMG_1302.jpeg`（盒子内部照，`hardware.md` 引用它，也给 README 一张真机图）。

**从全部历史剔除**：`backup/`、`firmware/{fmo-companion,sdfont-bench,vibebox}`、
`host/`、`components/trombik__esp_wireguard`、早期根 `main/`（fmo 布局）、
`tools/morsekob/`、厂商 PDF、`docs/{stock-firmware,firmware-dump,vendor-manual,changes,ui-redesign-spec}.md`、
`docs/specs/2026-08-28-cwbot-llm.md`（服务端）、`dist/`、
`fonts/{clock_font,status_font}.c`（cw-trainer 不用）、
`components/lvgl/{demos,examples,tests,docs,scripts,.github,…}`。

`dist/` 整个不带 —— 分发包以后的家是 GitHub Releases，仓库里不该再存一份 5M 的
zip。`docs/release-process.md` 里「分发包随源码一起提交」那条规则据此改写。

跨两个固件的提交被改写后只剩 cw 那一半，个别提交信息会显得没头没尾。这是保留
107 条提交历史的既定代价，不修。

### 推送前三道验收

不通过就退回「全新初始提交」方案。

1. `git log --all --diff-filter=A --name-only` 全量列出历史里出现过的文件名，人工过一遍
2. 对重写后的**全部 git 对象**扫敏感串：WiFi SSID/密码、`backup`、私钥头、`.bin` 整片镜像
3. `git count-objects -vH` 确认体积落在 25M 上下

## 需要改的代码

- `CMakeLists.txt`：删 `EXTRA_COMPONENT_DIRS` 整段（`components/` 已在根，IDF 自动发现）
- `main/CMakeLists.txt`：`../../../tools/gen_probe_assets.py` → `../tools/`
- `components/fonts/CMakeLists.txt`：`if(CMAKE_PROJECT_NAME STREQUAL "cw-trainer")` 分支拆掉，只剩直路；`subset_font.py` 的原料路径改成 `tools/font-src/font20.c`
- `components/lvgl/env_support/cmake/esp.cmake`：glob 里去掉 `demos/*.c`。**这行现在是无条件的** —— 当前构建把 music/benchmark 全套 demo 都编了（`build/` 里有 `lv_demo_music_main.c.obj`），只是没被链接进去。删掉后每次构建省下可观时间。这是对 vendored LVGL 的本地改动，记入 `NOTICE.md`
- `tools/idf.sh`：`PROJECT` 变量整个消失（只剩一个工程）
- `tools/package_release.sh`：要能在 CI 的 Linux 容器里跑 —— `shasum` → `sha256sum` 兼容、IDF 已在 PATH 时不重复 `source export.sh`、拿不到 IDF git 版本时退到 `$IDF_VERSION`
- `tools/{check_charset,subset_font,gen_*}.py`：跟着新布局改路径

`sdkconfig.defaults` **一个字不动**。它是 IDF 5.5.1 导出的全量 dump，不好看，但
改它就等于改固件配置；本次目标是抽离，不是重构。CI 用同一个文件、同一个 IDF
版本，编出来才是同一个东西 —— 打第一个 tag 前拿 CI 产物与本地
`dist/cw-trainer-v1.5.1` 的 bin 比大小和分区布局，对得上再继续。

## GitHub Actions

### `ci.yml` — push / PR 到 main

三个并行 job：

1. `make -C test`（13 个主机测试，`main/core/` 不依赖 ESP-IDF）
2. `tools/check_charset.py` + `tools/check_palette.py` 静态自检
3. `espressif/esp-idf-ci-action@v1`（`esp_idf_version: v5.5.4`，`target: esp32`）跑一次构建

### `release.yml` — push tag `v*`

1. **tag == `PROJECT_VER`**，不等就失败。这是抽离白送的新不变量：老仓库两个固件
   共用 tag 命名空间，`release-process.md` 用三段话解释「tag 不是权威版本号」，
   现在这三段可以删了
2. `docs/releases/cw-trainer-<tag>.md` 必须存在。沿用 `publish_ota.sh` 那条「没有
   版本说明不让发」的规矩，把它从发布脚本挪进 CI
3. 主机测试 → 构建 → `merge-bin` → `package_release.sh`
4. `softprops/action-gh-release` 上传资产，release body 取版本说明正文

资产名全部 ASCII —— GitHub 会把资产名里的非 ASCII 替换成点，所以「合并版」三个字
只能活在 zip 内部：

- `cw-trainer-<ver>.zip`（完整分发包，内部文件名保持中文）
- `cw-trainer-<ver>-merged.bin`（整机烧录）
- `cw-trainer-<ver>.bin`（OTA 用那份）
- `SHA256SUMS.txt`

同时提供 `workflow_dispatch` 入口（带 `dry_run`）：跑完构建与打包、产物作为
workflow artifact 上传、**不创建 release**。这样发布流水线可以在不制造一个假版本
的前提下验证。

**OTA 发布不进 CI。** 那需要把服务器 SSH 私钥放进公开仓库的 secrets，为省一条
命令不值得。`publish_ota.sh` 继续本地手动跑，release notes 留一行提示。

## 开源门面

- **LICENSE**：MIT，两行版权 —— `Copyright (c) 2026 BI8SIG`（上游，移植层/驱动/
  字体源出于它）与本项目作者
- **NOTICE.md**：LVGL 8.3.11（MIT，含我们对 `esp.cmake` 的改动）、
  lvgl_esp32_drivers（MIT）、上游 `zhaozhengde/esp32-fmo-companion` commit
  `8d372f9`（MIT），以及字体来源：`font28cn` 出自思源宋（OFL）有据可查，
  `font28`/`EN_BIG`/`font20` 出自「Lvgl Font Tool V0.4」，**底层字体来源不明**，
  随上游 MIT 一并继承。这是公开这个仓库唯一的法律软肋，写清楚比藏着强；
  backlog 记一条「用思源黑（OFL）重新生成全部字库」
- **README**：顶部一段英文简介（CW 训练器在国际圈有受众），正文中文 —— 项目所有
  文档都是中文，硬翻一遍既费事又会走样。内容取自现成的 `dist-readme.md`（功能、
  烧录步骤已很完整）与 `docs/hardware.md` 的 GPIO 表
- `hardware.md` 里「整理自厂商 PDF」那句要改写：PDF 不随仓库分发

## 老仓库收尾

新仓库 CI 跑通之后才动。删 `firmware/cw-trainer`、`docs/releases/`、
`docs/design/cw-trainer-*`、`dist/`、只服务 cw 的那批工具、`fonts/` 里的
`font20_cwtrn.c` 与 `font28cn.c`，`fonts/CMakeLists.txt` 的分支拆掉，
`docs/release-process.md` 移交新仓库，README 加指针。

老仓库继续私有 —— `backup/stock/` 的明文密码没变。

## 验收标准

- 新仓库 `idf.py build` 通过，app bin 大小与本地 v1.5.1 产物一致（±1KB）
- `make -C test` 13 个测试全过、无警告
- 三道泄密验收全过，`git count-objects -vH` ≤ 30M
- `ci.yml` 在 push 后变绿
- `release.yml` 的 `workflow_dispatch` dry-run 产出四个资产
- 老仓库删干净后 `PROJECT=firmware/fmo-companion tools/idf.sh build` 仍通过
