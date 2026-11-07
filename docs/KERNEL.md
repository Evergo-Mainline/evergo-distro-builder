# 🐧 内核移植状态

内核仓库：[`Evergo-Mainline/linux_evergo`](https://github.com/Evergo-Mainline/linux_evergo)（mainline 7.3-rc4 基线）
构建仓库通过 `KERNEL_COMMIT` pin 内核提交，见 [BUILDING](BUILDING.md)。

> 📌 当前 pin：**`865d67f3`**（`master` tip，已上机验证）。

## 分支与提交

### `master` — 已上机验证（构建镜像 pin 这里）

| 提交 | 内容 | 状态 |
|:---|:---|:---|
| `7d015704` | **cpufreq 电压上限**：MT6833 VPROC 1.15 V / VSRAM 1.25 V，解锁大核 2.4 GHz | ✅ |
| `bf899a2f` | **GPU 950 / 1000 / 1068 MHz OPP**（sign-off 上限，最高 +21%） | ✅ |
| `23112045` | **CPU 大核 2.4 GHz OPP**（2118/2203/2306/2400 MHz） | ✅ |
| `6f7c6054` | **MT6360 Type-C/PD 控制器**（GPIO9 PD_IRQB，sink 5 V/9 V PDO） | ✅ |
| `b72b125b` | **dramc-rk1 保留区 64 位地址单元修正** | ✅ |
| `8a430c3a` | **音量键修正**（PMIC home = 音量加，keypad = 音量减） | ✅ |
| `a10fac3a` | **温控 trip 抬高到 85/95/105 °C + GPU cooling map 修正** | ✅ |
| `29d2ef97` | **单 MC 域 cpu-map**：8 核同 socket，`lscpu` 不再显示 6+2 | ✅ |
| `a1f86e68` | **evergo config**：audio / PMIC / sensors / zram 全套打开 | ✅ |
| `7c0388f5` | **LK mblock 保留**：基带/SCP/SPM/RKP 固件内存，修复内存压力 panic | ✅ |
| `5d10282f` | CPU/GPU passive cooling 框架 | ✅ |
| `51e945c4` | 电池 OCV 表单位修正 | ✅ |
| `15322121` | fuel-gauge VBAT 改用 MT6360 ADC | ✅ |
| `6570467a` / `5aa7963e` | fuel-gauge DTS / IIO 通道绑定 | ✅ |
| `842fb312` | LVTS critical trip + 休眠恢复 | ✅ |
| `d0472c6e` | GPU DVFS（390–880 MHz 基础版） | ✅ |
| `0dc361b9` | CPU DVFS（mediatek-cpufreq） | ✅ |

### `cpuidle-exp` — 实验分支，不要合并

PSCI CPU idle states（cpuoff/clusteroff + 广播定时器）。状态确实生效，但与 DVFS 组合后压力测试/休眠会硬挂，并造成过 userdata 损坏。仅保留在分支。

## 已验证的关键结论

### CPU DVFS

- 驱动 `mediatek-cpufreq`，governor `schedutil`；
- 小核簇：950 MHz – 2.0 GHz（下限受 CCI 共享 Vproc2 限制）；
- 大核簇：725 MHz – **2.4 GHz**；
  - 2.4 GHz 需要 VPROC/VSRAM 提到 1.15 V / 1.25 V（`7d015704`），否则 cpufreq 切 PLL 后调压失败会挂死；
  - **短时爆发** 2.4 GHz，持续满载受 85 °C 温控回落到 ~1.95–2.0 GHz；
  - 2.4 GHz 下 SHA256 单线程 **1476 MB/s**（2.0 GHz 1229 MB/s，+20%）。

### GPU DVFS

- `/sys/class/devfreq/13000000.gpu`，`simple_ondemand`，390–**1068 MHz**；
- OPP：950 MHz @ 781250 µV、1000 MHz @ 800000 µV、1068 MHz @ 850000 µV；
- 主线 PLL postdiv/VCO 计算能覆盖 950/1000/1068，无需 frequency-hopping；
- GPU-bound OpenGL ES 基准（EGL pbuffer + 全屏 fragment shader，Mali-G57 MC2/Panfrost）：

| GPU 频率 | fps |
|:---:|:---:|
| 390 MHz | 9.5 |
| 545 MHz | 13.3 |
| 622 MHz | 15.2 |
| 700 MHz | 17.0 |
| 880 MHz | 21.5 |
| 950 MHz | 23.2 |
| 1000 MHz | 24.4 |
| **1068 MHz** | **26.0** |

> ⚠️ 早期 955 MHz 会重启（正好卡在 vendor posdiv 分界点）；950/1000/1068 均已实测稳定。

### 休眠

CPU+GPU DVFS 同时启用时 **s2idle 进出正常**（suspend_stats success=1；resume 后显示/触摸/cpufreq/GPU devfreq 都恢复）。GPU 固定在 1068 MHz 时休眠/唤醒同样通过。

### 电池 / 充电

修复前 `VOLTAGE_NOW` 为负、`capacity=0`。根因与修复：

1. fuel-gauge `io-channels` 指向 MT6359 AUXADC 阻抗通道（scale 不是 uV/LSB）→ 改读 MT6360 充电 ADC 的 VBAT（1250 uV/LSB）；
2. 驱动对已是 uV 的结果又乘 1000 导致 int 溢出 → 删除；
3. DTS 电池 OCV 表电压放大 10 倍 → 整表除以 10。

真机：`voltage_now=4287500 uV`、`capacity=85 %`、`charge_now≈4.25 Ah`、充电电流正常。

### 手电筒

MT6360 白闪 LED 暴露为 `/sys/class/leds/white:flash-1`：

- `brightness` 1–7：亮度档位；
- `flash_strobe`：闪光脉冲；
- `flash_fault`：过压/欠压状态。

镜像内 `evergo-torch on|off|toggle|strobe|status` 已验证。

## 温控与压力测试

CPU/GPU DVFS 打开后若无 cooling，满载温度会从 80 °C 升到 102 °C 并挂死（已复现）。13 个热区加 passive cooling 后：

- CPU 小核/大核：**85/95/105 °C** 三档降频；
- GPU：85/95 °C 两档（cooling map 修正为有效 state 0–6）；
- SOC 区：90/100 °C；
- critical trip：115 °C。

5 分钟 8 核满载：温度平台 ~85 °C，大核回落至 1.95–2.0 GHz，系统持续存活。
详见 [STRESS](STRESS.md)。

## 内存：DRAM 大小从 bootloader 读取（`865d67f3`）

`setup_arch()` 在换成内嵌 DTB 之前，从 LK 交给内核的树里读回真实 DRAM 大小和全部 `/reserved-memory`，然后裁剪/移除 memblock 中多声明的部分。这样 4/6/8 GiB 机型**共用同一份 boot.img**，不再需要按 RAM 变体编译两套内核。

真机（6 GiB 机型 + 8 GiB 上限镜像）：`evergo: trimming 2048 MiB`、`kept 32 of 43 bootloader reservations`、`MemTotal 5,261,276 kB`，4.3 GiB 分页探测 **0 坏页**。

## 内存压力 panic：根因与修复（`7c0388f5`）

- 内核用编译进 Image 的 DTB，丢掉了 **LK 注入的 `/memory` + `/reserved-memory` mblock**；
- 基带/SCP/SPM/RKP/framebuffer/MCUPM/TEE 占用的 DRAM 未被 Linux 保留，固件与 Linux 同时写 → 数据被踩；
- 4.2 GiB 内存测试复现：修复前 **47,308 个坏页**（0x6c000000..0xb52ff000，正好是基带 mblock），补全后 **0 坏页**。

`mem=4G` 的临时规避不再需要。

## 已知限制与风险

| 项 | 说明 |
|:---|:---|
| 大核持续 2.4 GHz | 受 85 °C 温控限制，仅短时爆发 |
| GPU 1068 MHz | 已达 vendor sign-off 上限 |
| 蓝牙地址 | BT 核无出厂地址，BlueZ 暂不可用 |
| 相机 / 基带 | 未移植，见 [CAMERA-MODEM](CAMERA-MODEM.md) |
| PSCI idle | `cpuidle-exp` 实验分支不稳定 |

## 抓内核崩溃日志（无串口）

SSH 失联超过 60 秒按 panic 处理，重新开机后：

```bash
ls /sys/fs/pstore/
cat /sys/fs/pstore/console-ramoops-0
```

取不到时从 expdb 分区提取，步骤见 [PANIC](PANIC.md)。

## 提交规范

- 一个功能一个提交，提交信息写清「为什么」和「上机验证了什么」；
- 未验证的改动明确标注并放分支；
- 不提交调试打印/死代码；发布镜像只 pin 已上机验证的 `master`；
- DTS 变更按 board/SoC 分开，方便其它 MT6833 机型复用；
- 提交到 `Evergo-Mainline/linux_evergo` 前先看最新 `master`，避免覆盖他人的新提交。
