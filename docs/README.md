# 文档索引

## 现行文档（会跟着代码维护）

| 文件 | 内容 |
|---|---|
| `hardware.md` | 板卡、GPIO 映射、外设、接口座子规格、本机改装 |
| `build-setup.md` | ESP-IDF v5.5.4 环境搭建（含两个装完就踩的坑）、波特率为什么读写不同 |
| `release-process.md` | 版本号语义、版本说明要求、发版流程、分发包、OTA、回退的代价 |
| `engineering-notes.md` | **踩出来的坑**：侧音采样率的硬下限、中文字体用错会静默变空白、屏幕旋转必须改两处、外接电键的选脚理由与门闩机制、CWCom 协议的隐性约束 |
| `upstream-source.md` | 上游 esp32-fmo-companion 的溯源与 MIT 许可证据（四条独立证据） |
| `optimization-backlog.md` | 查证清楚但当前不做的优化项，以及**已被实测否决**的方案——附否决理由，免得再走一遍 |

## 历史记录（写完就不再改）

| 目录 | 内容 |
|---|---|
| `specs/` | 各功能的设计文档，按日期命名 |
| `plans/` | 实施计划，逐任务列出要建哪些文件、跑哪条命令、怎么验收 |
| `releases/` | 每个版本的版本说明。CI 拿它当 GitHub Release 的正文 |
| `design/` | 视觉方向与交互原型（HTML，浏览器打开） |

**`specs/` 与 `plans/` 里的路径是抽离成独立仓库之前的布局**，那时这个固件住在
一个多固件仓库的 `firmware/cw-trainer/` 下面。对应关系：

| 文档里写的 | 现在的位置 |
|---|---|
| `firmware/cw-trainer/` | 仓库根 |
| `firmware/cw-trainer/main/` | `main/` |
| `fonts/` | `components/fonts/`（全量字库在 `tools/font-src/`） |
| `docs/superpowers/specs/`、`docs/superpowers/plans/` | `docs/specs/`、`docs/plans/` |
| `make -C firmware/cw-trainer/test` | `make -C test` |
| `PROJECT=firmware/cw-trainer tools/idf.sh build` | `tools/idf.sh build` |

这些文档**没有跟着改写**——它们记的是当时做了什么，改路径等于伪造记录。抽离
本身的设计见 `specs/2026-09-10-cw-trainer-extraction-design.md`。

同理，**v1.5.1 及之前的 tag 在本仓库里构建不出来**。抽离时字体组件去掉了本固件
从不使用的 `clock_font.c` / `status_font.c`（那是同一块板上另一个固件的字体，
只是当年两个固件共用一个字体组件），而那时的 `components/fonts/CMakeLists.txt`
还在引用它们；工程根的 `EXTRA_COMPONENT_DIRS` 也还指向抽离前的多固件布局。这些
tag 留着是为了 `git log` / `git blame` 能一路追溯，不是为了从它构建。各版本的
产物见 [Releases](../../releases)——v1.4.2 / v1.5.0 / v1.5.1 附着当年发布的分发
包，更早的版本当年没打包（「每版必打」是 v1.5.1 才定的规矩）。

同理，它们提到的 `firmware/fmo-companion`（同一块板子上的另一个固件）和
`tools/morsekob/`（wire 服务器的服务端代码）都不在本仓库，前者见
`upstream-source.md`。
