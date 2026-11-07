# 🤝 参与贡献

欢迎给 evergo 主线 Linux 提交镜像、文档或内核改动！这个仓库负责把内核打包成**可刷写的成品镜像**。

## 📜 基本约定

- 本仓库与内核仓库均以 **GPL-3.0-or-later** 发布，提交即表示你同意以该许可证发布；
- 提交前请先看目标仓库的**最新 `master`**，避免覆盖他人刚合入的提交；
- 一个提交只做一件事，提交信息写清「为什么」和「上机验证了什么」；
- 不提交调试打印、死代码和自动生成的大文件。

## 🌿 分支与提交

| 类型 | 建议前缀 | 例子 |
|:---|:---|:---|
| 内核/驱动 | `arm64: dts:` / `cpufreq:` / `drm/…:` | `arm64: dts: mt6833-evergo: fix the swapped volume keys` |
| 构建脚本 | `build:` | `build: compile the kernel once for both RAM variants` |
| 文档 | `docs:` | `docs: update the hardware support matrix` |
| CI | `ci:` | `ci: cache the Alpine initramfs rootfs` |

## 🧹 本地检查

提交前至少跑一遍：

```bash
# shell 语法
for f in build.sh scripts/*.sh; do bash -n "$f"; done

# Python 语法
python3 -m py_compile scripts/*.py

# workflow YAML 语法
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build.yml'))"
```

有真机的话，尽量在手机上验证一遍对应的驱动/镜像，并把结果写进 PR/提交信息。

## 🧩 内核改动

- 内核仓库：[Evergo-Mainline/linux_evergo](https://github.com/Evergo-Mainline/linux_evergo)
- 只往上游/仓库推**已上机验证**的提交；实验性内容放分支；
- DTS 变更按 board / SoC 分开，方便其它 MT6833 机型复用；
- 改完内核后在构建仓库里更新 `KERNEL_COMMIT` pin，并同步 `docs/KERNEL.md`。

## 🧪 测试清单

| 项目 | 建议 |
|:---|:---|
| 启动 | 冷启动 + 重启各一次 |
| 显示/触摸 | 息屏、唤醒、旋转 |
| 网络 | USB NCM、Wi-Fi、`evergo-wifi status` |
| 电源 | 充电、电量计、`upower -d` |
| 温控 | `stress-ng` 5 分钟看频率/温度是否正常回落 |
| 休眠 | `echo mem > /sys/power/state`，检查 `suspend_stats` |

## 🐞 报问题

请附上：

1. 机型（6 GiB / 8 GiB、面板 Tianma / CSOT）；
2. 刷的镜像名（如 `boot-ubuntu-desktop-6gib.img`）;
3. `dmesg`、`journalctl -b` 相关片段（崩了的看 [docs/PANIC.md](docs/PANIC.md)）；
4. 复现步骤。
