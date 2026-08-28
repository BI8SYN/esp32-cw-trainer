# 发布规则

适用于 `firmware/cw-trainer`（`fmo-companion` 目前不走 OTA）。

## 版本号

`vMAJOR.MINOR.PATCH`，写在工程的 `CMakeLists.txt` 的 `PROJECT_VER`。

**这是唯一权威的版本号**——它被嵌进 bin，设备的设置页和启动日志显示的都是它。
仓库 tag 不是：两个固件共用同一个 tag 命名空间，对不上号。

| 位 | 什么时候动 | 例 |
|---|---|---|
| PATCH | 修 bug、调文案、改样式。用户的使用方式不变 | 修第一声破音 → v1.3.1 |
| MINOR | 加了新功能或新页面。老功能照旧 | 加训练统计 → v1.4.0 |
| MAJOR | 不兼容的改动，或者一次大的重做 | 暂无 |

**PATCH 位要真的用起来。** 之前几次修 bug 都跳了 MINOR（1.1→1.2→1.3），
那是错的——版本号本身要能告诉人「这次是修东西还是加东西」。

## 每个版本都要有版本说明

放在 `docs/releases/<工程名>-<版本>.md`。**没有它 `publish_ota.sh` 会拒绝发布。**

设备上看不到这份说明（裁剪后的字库放不下那么多字，见
`tools/check_charset.py`），但仓库里必须留下这一版改了什么——出问题时要靠它
定位是哪一版引入的。

模板：

```markdown
# cw-trainer v1.3.1

日期：2026-08-28

## 修复
- 电键第一声破音：功放平时关断，原来等第一个点到来才开，它还在上电稳定的
  过程中音频就已经在播了。改为进入会发声的页面时提前上电。

## 已知问题
- （没有就写「无」）
```

## 默认发 OTA 包

```bash
PROJECT=firmware/cw-trainer tools/idf.sh build
tools/publish_ota.sh firmware/cw-trainer
```

脚本会上传 `cw-trainer-<版本>.bin` 并更新清单 `cw-trainer.json`。设备端在
设置 → 常规 → 固件升级 里检查和升级。

固件文件名带版本号是有意的：能同时保留多个版本以便回退，出问题时看文件名
就知道设备装的是哪个。

## 只有明确要求时才打分发包

分发包是给**没有网、或者第一次刷机**的人用的——完整的 bootloader + 分区表 +
app，加一份烧录说明，压成 zip。

```bash
tools/package_release.sh firmware/cw-trainer
```

输出到 `~/Desktop/cw-trainer-<版本>/` 并压成同名 zip。

**不要默认打分发包。** 日常迭代一律走 OTA——分发包要对方手工烧录，成本高得多。

## 发布前的自检

```bash
python3 tools/check_charset.py      # 新文案有没有用到裁剪字库之外的字
make -C firmware/cw-trainer/test    # 主机测试
PROJECT=firmware/cw-trainer tools/idf.sh build
```

字库那条特别容易漏：用到表外的字**不会有编译错误，只会在屏幕上渲染成空白**，
而那种问题只有对着真屏幕才看得出来。
