<div align="center">

# 📱 红米 Note 11T 5G · 多发行版 Linux

**把手机变成一台 106 GiB 存储、SSH 可登、Snap 可用的 Linux 电脑**

[![设备](https://img.shields.io/badge/设备-evergo%20%2F%20everpal-FF6900?style=for-the-badge&logo=xiaomi&logoColor=white)](https://github.com/Evergo-Mainline/evergo-distro-builder)
[![SoC](https://img.shields.io/badge/SoC-MT6833%20天玑810-00A3E0?style=for-the-badge)](https://github.com/Evergo-Mainline/evergo-distro-builder)
[![内核](https://img.shields.io/badge/内核-7.3.0--rc4-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://github.com/Evergo-Mainline/linux_evergo)
[![发行版](https://img.shields.io/badge/发行版-Arch%20%7C%20Debian%20%7C%20Ubuntu%20%7C%20Fedora-1793D1?style=for-the-badge)](https://github.com/Evergo-Mainline/evergo-distro-builder/releases)
[![License](https://img.shields.io/badge/License-GPL--3.0--or--later-2ea44f?style=for-the-badge&logo=gnu&logoColor=white)](LICENSE)
[![CI](https://img.shields.io/github/actions/workflow/status/Evergo-Mainline/evergo-distro-builder/build.yml?style=for-the-badge&label=CI)](https://github.com/Evergo-Mainline/evergo-distro-builder/actions)

</div>

> 🍥 跑在手机裸硬件上的 **mainline Linux** —— 不是 Android，也不是套壳。
> 基于 [Evergo-Mainline/linux_evergo](https://github.com/Evergo-Mainline/linux_evergo) 主线内核，本仓库负责把它打包成**刷完就能用**的成品镜像。

---

## 📑 目录

- [✨ 特性](#-特性)
- [🚀 快速开始](#-快速开始)
- [🖼️ 镜像矩阵](#️-镜像矩阵)
- [🎛️ 硬件支持](#️-硬件支持)
- [⚡ 性能数据](#-性能数据)
- [🛠️ 自行构建](#️-自行构建)
- [📖 文档](#-文档)
- [🧰 常用命令](#-常用命令)
- [⚠️ 警告](#️-警告)
- [🙏 致谢](#-致谢)
- [📜 License](#-license)

---

## ✨ 特性

| | |
|:---|:---|
| 📱 **完整 Linux** | Arch / Debian / Ubuntu / Fedora 四发行版，`systemd` + 包管理器 + SSH，开箱即用 |
| 💾 **106 GiB 存储** | 128 GB eMMC 首次开机自动扩容，不再是玩具系统 |
| 🖥️ **三种形态** | `minimal` 命令行、`xfce` 轻量桌面、`desktop` 完整 Ubuntu 桌面（GNOME + Snap） |
| 📶 **Wi-Fi / USB 网络** | MediaTek WMT/gen4m Wi-Fi，CDC-NCM USB 网卡（`169.254.42.1`） |
| 🎮 **2.4 GHz + 1068 MHz** | 大核短时 2.4 GHz、GPU 最高 1068 MHz，性能全开（详见[性能数据](#-性能数据)） |
| 🔋 **电源完整** | MT6360 充电 + MT6359 电量计 + Type-C/PD 节点，`upower`/`power-profiles-daemon` 可用 |
| 🌡️ **温控 / 降频** | 13 路 LVTS + passive cooling，满载自动降频不烫手 |
| 📦 **Snap 可用** | 内核启用 squashfs XZ/ZSTD/LZ4/LZO，`snap install` 正常 |
| 🔐 **安全** | 内核 7.3 主线 + AVB 签名启动链，SSH 密码登录 |

---

## 🚀 快速开始

### 1️⃣ 下载

前往 [**Releases**](https://github.com/Evergo-Mainline/evergo-distro-builder/releases)，按手机内存挑选：

- `boot-<发行版>-<内存>gib.img` —— 内核 + initramfs
- `userdata-<发行版>-<内存>gib.img.gz` —— 根文件系统

> 🧠 **一份镜像适配全部内存**：内核会从 bootloader 读回真实 DRAM 大小并裁剪，
> 4 / 6 / 8 GiB 机型共用同一份 `boot.img`，下载和刷入时**不再区分内存版本**。

### 2️⃣ 刷入

```bash
gunzip userdata-debian.simg.gz

fastboot -S 200M flash boot_a    boot-debian.img
fastboot -S 200M flash boot_b    boot-debian.img
fastboot -S 200M flash userdata  userdata-debian.simg
fastboot reboot
```

> 💡 Windows 版 fastboot 处理不了大文件，见 **[刷机教程 · Windows 用户注意](docs/TUTORIAL.md#windows-用户注意)**。

### 3️⃣ 连上

首次开机自动扩容（约 1–10 分钟），随后：

```bash
ssh root@169.254.42.1        # 密码 evergo
```

手机通过 **USB RNDIS/NCM 网卡**接入：直连电脑零配置；插路由器还能上网。
没有串口 —— 一切操作走 SSH 或 `screen` 外接（可选）。

---

## 🖼️ 镜像矩阵

### 发行版

| 发行版 | 版本 | userdata 体积 | 包管理器 |
|:---|:---|:---:|:---|
| 🟦 **Arch Linux ARM** | rolling | 840 MB | `pacman` |
| 🟥 **Debian** | 13 (trixie) | 162 MB | `apt` |
| 🟧 **Ubuntu** | 26.04 LTS | 206 MB | `apt` |
| 🟪 **Fedora** | 44 | 142 MB | `dnf` |

### 桌面形态（`edition`）

| edition | 内容 | 适合 |
|:---|:---|:---|
| `minimal` | SSH + NetworkManager + `nmtui` + `evergo-*` 工具 | 服务器 / 最稳最省内存 |
| `xfce` | XFCE + LightDM + 中文 | 想要图形界面但不吃内存 |
| `desktop` | **完整 `ubuntu-desktop`**：GNOME 全套应用、中文语言包、IBus 拼音、Snap | 当主力桌面用 |

> 🧑‍🚀 `desktop` 镜像**不预建账号**：首次开机由 GDM 拉起 **GNOME Initial Setup**，
> 语言、账号、密码全部由用户自己设置，真正做到开箱即用。
>
> 📦 桌面 rootfs 约 12 GiB，userdata 稀疏镜像超过 2 GiB，因此**只进 Actions artifacts，不进 Release**；
> Release 里提供对应的 `boot-*.img` 和轻量镜像。

### 面板与触摸

| 面板 | `PANEL` | 触摸固件 |
|:---|:---|:---|
| 天马 Tianma | `k16a_36_02_0a_vdo` | `novatek_ts_fw.bin` |
| 华星 CSOT | `k16a_42_02_0b_vdo` | `novatek_ts_ct_fw.bin` |

触摸固件按 DTS 面板节点**自动选择**；选错面板会表现为显示/触摸异常。
不确定时两个都刷一次试。

---

## 🎛️ 硬件支持

| 子系统 | 状态 | 说明 |
|:---|:---:|:---|
| 主控 MT6833 / 天玑 810（8 核） | ✅ | 单 MC 域，`lscpu` 显示 8 物理核 |
| 存储 128 GB eMMC → 106 GiB | ✅ | 首次开机自动 resize |
| 显示 `mediatekdrmfb` 帧缓冲 | ✅ | 含 90 Hz 面板时序 |
| 触摸 Novatek NT36672C | ✅ | 固件内嵌，suspend/resume 已修复 |
| Wi-Fi（MT6631 / gen4m） | ✅ | WMT 拉起 CONNSYS，NetworkManager 管理；`regdom=CN` |
| 蓝牙 | 🟡 | BT 核可上电、HCI 5.1，但无出厂地址（BlueZ 不可用） |
| USB CDC-NCM | ✅ | 高速模式实测 ~5.2 MB/s |
| 音频 / 麦克风 | ✅ | MT6833 AFE + MT6359 sound card |
| 振动马达 | ✅ | AW8697 haptic |
| 充电 / 电量计 | ✅ | MT6360 charger + MT6359 fuel gauge + Type-C/PD |
| 传感器 | ✅ | ALS / 距离 / 加速度 / 陀螺仪 / 磁力计 |
| CPU DVFS | ✅ | 小核 950 MHz–2.0 GHz；大核 725 MHz–**2.4 GHz**（短时） |
| GPU DVFS | ✅ | Panfrost Mali-G57 MC2，390–**1068 MHz** |
| 温控 | ✅ | 13 路 LVTS + passive cooling，critical 115 °C |
| Snap / squashfs | ✅ | XZ / ZSTD / LZ4 / LZO 全开 |
| 虚拟化 / TUN / VHOST | ✅ | `kvm`、`vhost-net`、`tun` |
| 基带 / 通话 | ❌ | 未移植 |
| 相机 | ❌ | 未移植 |

---

## ⚡ 性能数据

真机实测（6 GiB 版本，室温，Thermal 默认策略）：

| 项目 | 数据 |
|:---|:---|
| 🧠 CPU 大核 | 短时 **2400 MHz**；8 核满载受 85 °C 温控回落 ~1950–2000 MHz |
| 🧮 SHA256 单线程 | 2.0 GHz **1229 MB/s** → 2.4 GHz **1476 MB/s**（+20%） |
| 🎮 GPU（OpenGL ES） | 880 MHz **21.5 fps** → 950 **23.2** → 1000 **24.4** → 1068 **26.0 fps**（+21%） |
| 💾 内存写 | ~1.93 GB/s（4.8 GB 写入） |
| 💽 UFS 顺序写 | ~257 MB/s |
| 🌡️ 8 核 5 分钟压力 | 稳定，温度平台 ~85 °C |
| 🔌 USB 网络 | ~5.2 MB/s |

---

## 🛠️ 自行构建

### 依赖

Ubuntu 24.04 主机（或 CI runner），需要：

```bash
sudo apt-get install -y build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  ccache bc bison flex libssl-dev libelf-dev libncurses-dev cpio gzip xz-utils zstd \
  p7zip-full python3 device-tree-compiler patch debootstrap e2fsprogs \
  qemu-user-static android-sdk-libsparse-utils
```

### 一句话构建

```bash
# 完整镜像（以 Ubuntu 26.04 最小版、6 GiB 为例）
DISTRO=ubuntu EDITION=minimal RAM_GIB=6 ./build.sh

# 只构建内核 + boot.img（快速迭代内核/DTB）
KERNEL_ONLY=1 INITRAMFS_ROOTFS=$PWD/rootfs-alpine RAM_GIB=6 ./build.sh
```

产物在 `out/`：

```text
out/boot.img           # boot_a / boot_b
out/userdata.simg      # rootfs 镜像
```

常用环境变量：

| 变量 | 默认 | 作用 |
|:---|:---|:---|
| `DISTRO` | `debian` | `arch` / `debian` / `ubuntu` / `fedora` |
| `EDITION` | `minimal` | `minimal` / `xfce` / `desktop` |
| `RAM_GIB` | `6` | `6` 或 `8`，决定 DTB 内存节点 |
| `PANEL` | `k16a_36_02_0a_vdo,lcm` | 面板型号 |
| `KERNEL_ONLY` | `0` | `1` = 只构建内核 |
| `BOOT_IMG` | — | 复用已有 boot.img，跳过内核编译 |

> 🧬 内核固定到 [`Evergo-Mainline/linux_evergo`](https://github.com/Evergo-Mainline/linux_evergo)
> 的验证过的提交（见 [build.sh](build.sh) 里的 `KERNEL_COMMIT`），保证可复现。

### CI

GitHub Actions 全自动：

1. `plan` —— 计算镜像矩阵；
2. `boot / kernel + boot image` —— **只编译一次内核**，产出一份适配 4/6/8 GiB 的 boot 镜像；
3. `rootfs` —— 按发行版/edition 并行 debootstrap；
4. `release` —— 发布 `build-<N>` Release 与 artifacts。

---

## 📖 文档

| 文档 | 内容 |
|:---|:---|
| 📖 [刷机与使用教程](docs/TUTORIAL.md) | 下载、刷机、联网、首次登录、包管理、模式切换 |
| 🛠️ [构建指南](docs/BUILDING.md) | 本地构建、CI、多发行版脚本、内核配置 |
| 🧩 [功能与内核模块](docs/MODULES.md) | 各功能对应内核配置/补丁 |
| 🔌 [硬件支持清单](docs/HARDWARE.md) | 硬件/虚拟化/PD/Snap 的验证状态 |
| 🧠 [内核提交记录](docs/KERNEL.md) | 每个内核功能的提交与实测结果 |
| 🩺 [抓 panic 日志](docs/PANIC.md) | 无串口时从 expdb 取回崩溃 trace |
| 🕳️ [踩坑记录](docs/TROUBLESHOOTING.md) | 按症状查：启动失败、不扩容、连不上、构建报错 |
| 📜 [文档索引](docs/README.md) | 全部文档导航 |
| 🤝 [参与贡献](CONTRIBUTING.md) | 提交规范、本地检查、测试清单 |

---

## 🧰 常用命令

```bash
evergo-mode status     # 当前 USB 网络模式（pc / router）
evergo-mode router     # DHCP 客户端，插路由器用（默认）
evergo-mode pc         # DHCP 服务端，直连电脑用
evergo-wifi scan       # 扫描 Wi-Fi
evergo-wifi connect "SSID" "密码"
evergo-wifi status     # 链路 / 地址 / 关联状态
evergo-wifi down       # 断开并关闭 Wi-Fi
evergo-info            # 一屏系统概览
```

---

## ⚠️ 警告

> 🗑️ **会清空 Android 数据**：刷入会破坏 userdata（FBE 加密，主线 Linux 无法读取）。
> 回到 Android：`fastboot --set-active=a`，但 Android 会要求重新格式化数据分区。

> 🚫 **非官方支持**：本项目与小米、联发科无关，刷机风险自负。

> 🔋 **关机充电**：本机 fastboot 下无充电逻辑，长时间测试请保持供电。

---

## 🙏 致谢

| 贡献 | 来源 |
|:---|:---|
| 🧠 内核移植 | [Evergo-Mainline/linux_evergo](https://github.com/Evergo-Mainline/linux_evergo) · [KevinLmK0702/linux-evergo](https://github.com/KevinLmK0702/linux-evergo) |
| 🐧 Arch 根文件系统 | [Arch Linux ARM](https://archlinuxarm.org/) |
| 💡 分区扩容思路 | [GengWei1997/linux-xiaomi-raphael-uboot](https://github.com/GengWei1997/linux-xiaomi-raphael-uboot) · [postmarketOS](https://postmarketos.org/) |
| 🔧 MT6833 主线化起点 | Poco M3 Pro 5G（camellia）项目 |
| 🧪 测试与反馈 | 所有在真机上刷过这个系统的朋友 |

---

## 📜 License

本仓库以 **GPL-3.0-or-later** 发布，完整条款见 [LICENSE](LICENSE)。

<div align="center">

⭐ 如果这个项目帮到你，点个 Star 支持一下

</div>
