# 📷 内置摄像头移植调研（bring-up 路线）

> 状态：**未开始驱动移植**。本文记录从 stock ROM / vendor 内核里提取的硬件拓扑和分阶段计划，
> 避免重复调研。整体是数月级工程，先按「能读到 sensor ID → 能出 RAW → 能出图」拆里程碑。

## 1. 硬件拓扑（来自 vendor 内核源码 + vendor DTB）

### Sensor 与 PMIC（vendor `cust_evergo_camera.dtsi`）

| 位置 | vendor sensor 字符串 | 供电 |
|:---|:---|:---|
| cam0（主摄） | `s5kjn1sunny_mipi_raw` / `ov50c40ofilm_mipi_raw` | vcamd=`wl2866d_dvdd1`、vcama=`wl2866d_avdd1`、vcamio=`mt_pmic_vtp_ldo_reg` |
| cam1 | `ov16a1qofilm_mipi_raw` / `ov16a1qqtech_mipi_raw` | vcama=`wl2866d_avdd2`、vcamd=`wl2866d_dvdd2`、vcamio=`mt_pmic_vcamio_ldo_reg` |
| cam2 | `imx355ofilm_mipi_raw` / `imx355sunny_mipi_raw` | 同 cam1（`wl2866d_avdd2/dvdd2`） |

> ⚠️ 早期文档写的「OV8856 超广角」与 vendor 表不一致，以 vendor 字符串为准。

### 外部 Camera PMIC：WL2866D

- `i2c6` 上，地址 **0x28**，`en-gpio = GPIO59`；
- 4 路 LDO：`dvdd1/dvdd2`（0.6–2.13 V）、`avdd1/avdd2`（1.2–4.3875 V），
  init：dvdd1 1.2 V、dvdd2 1.05 V、avdd1/2 2.8 V；
- 主线**没有** wl2866d 驱动，vendor 驱动在
  `drivers/regulator/wl2866d.c`（949 行，regmap + regulator_ops），需要移植。

### 控制 GPIO（vendor pinctrl）

| 信号 | GPIO |
|:---|:---|
| cam0 reset | GPIO54 |
| cam1 reset | GPIO60 |
| cam2 reset | GPIO58 |
| cam4 reset | GPIO56 |
| cam0 MCLK | GPIO49（CMMCLK0） |
| cam1 MCLK | GPIO52（CMMCLK3） |
| cam2 MCLK | GPIO51（CMMCLK2） |
| cam4 MCLK | GPIO50（CMMCLK1） |
| MIPI switch sel | GPIO43 |
| cam1/cam2 DVDD EN | GPIO93 |

### SoC 侧

| 模块 | 地址 | 主线现状 |
|:---|:---|:---|
| seninf1..6 / seninf_top | `0x1a004000`–`0x1a009000` | ❌ 无驱动、DTS 无节点 |
| camsys / camsys_rawa/rawb | `0x1a000000` / `0x1a04f000` / `0x1a06f000` | 只有 clock syscon 节点 |
| cam1/cam1_inner | `0x1a030000` / `0x1a038000` | 无驱动 |
| imgsys1/imgsys2 | `0x15020000` / `0x15820000` | 只有 clock syscon 节点 |
| MIPI CSI-2 PHY | — | 主线 `phy-mtk-mipi-csi-0-5` 只有 `mediatek,mt8365-csi-rx` compatible |

### 主线已有、可直接复用的部分

- `drivers/media/i2c/s5kjn1.c`：支持 `vddd/vdda/vddio/afvdd` 四路 regulator + `mclk` + reset GPIO；
- `drivers/media/i2c/imx355.c`、`ov02a10.c`、`ov02c10.c`；
- `drivers/regulator/mt6359-regulator.c` 已有 `ldo_vcamio`；
- `clk-mt6833-cam_m/ra/rb.c` 时钟驱动已编进内核（但 DTS 只挂了 clock-controller syscon，没有给 sensor 用的 MCLK 消费者）。

## 2. 缺失清单

1. **i2c2 / i2c4 / i2c8**：`mt6833.dtsi` 只描述了 `i2c6`，sensor 和 EEPROM 所在的 i2c2/4/8 需要补节点（时钟、pinctrl、DMA）；
2. **WL2866D regulator 驱动**（或等价的最小 LDO 驱动）；
3. **CAM MCLK**：sensor 需要 24 MHz；vendor 由 CMMCLK0-3 输出，主线需要把 `cam_m` 时钟树接到 pinctrl 的 CMMCLK 功能上；
4. **seninf 驱动**：CSI-2 receiver（D-PHY + 协议、虚拟通道、错误处理）；
5. **camsys/imgsys ISP 驱动**：MT6833 RAW→YUV、统计、AF/AE/AWB；
6. **libcamera pipeline + 3A**、标定数据解析。

## 3. 分阶段计划

| 里程碑 | 内容 | 验收 | 预估 |
|:---|:---|:---|:---|
| **M1 供电 + I2C** | 补 i2c2/4 节点、移植 WL2866D、加 sensor 节点（reset + 供电） | `i2cdetect` 能看到 sensor ACK，sensor 驱动 probe 读到 chip ID | 1–2 周 |
| **M2 MCLK** | 打通 CMMCLK0 24 MHz 输出 | sensor 进入 SW standby，ID 读取稳定 | 1 周 |
| **M3 seninf** | 移植/新写 seninf CSI-2 RX，接 sensor 出 RAW | `v4l2-ctl --stream-mmap` 拿到 RAW10 帧 | 1–2 月 |
| **M4 ISP pass-through** | camsys RAW→YUV 最小路径 | 能录制 YUV 预览 | 2–4 月 |
| **M5 完整相机** | AF/AE/AWB、libcamera、标定 | 日常拍照可用 | 数月 |

> 💡 建议：M1/M2 可以先只做 cam0（S5KJN1），成功后其它 sensor 复用同一套供电/时钟框架。

## 4. 参考

- vendor 内核：`xbxbncb/kernel_xiaomi_mt6833`（Android 4.14）
  - `arch/arm64/boot/dts/mediatek/cust_evergo_camera.dtsi`（GPIO / 供电）
  - `arch/arm64/boot/dts/mediatek/wl2866d.dtsi`（PMIC 节点）
  - `drivers/regulator/wl2866d.c`
  - `drivers/misc/mediatek/imgsensor/src/mt6833/`（sensor 驱动 / seninf_impl）
- stock 镜像：`evergo_images_V14.0.4.0.TGBCNXM_13.0`

## 5. 结论

内置相机目前仍不可用；本文件是后续移植的起点。M1（供电 + I2C + sensor ID）是第一个可验证、
风险可控的里程碑，也是判断整条链路值不值得继续投入的关键一步。

## 6. M1 实测记录（2026-11-08）

测试内核在 board DTS 里给 GPIO59（WL2866D EN）和 GPIO54（cam0 reset）加了 gpio-hog，
启动后用 userspace I2C 直接操作：

- **WL2866D 确认存在**：`i2c-0` 上 `0x28` ACK，读 `reg 0x03 = 0x64`（= WL2866D_ID）；
- 按 vendor 初始化序列写入后：
  `0x02=0x8f`、`0x03=0x64`、`0x04=0x4b`、`0x05=0x80`、`0x06=0x80`、`0x0e=0x0f`；
- 全总线扫描：`i2c-0` = `0x28 0x58`，`i2c-2` = `0x66`，其余为空；
- **sensor 0x20 仍 NACK** —— 供电和 I2C 已经通了，剩下的就是 **MCLK（CMMCLK0 24 MHz）**；
  主线目前没有输出 CMMCLK，sensor 不上电时钟不会响应 I2C。

结论：M1 的「供电 + I2C + PMIC」已打通，下一步 M2 需要把 CMMCLK0 配出来。

### M1.5（2026-11-08 补充）

把板级 DTS 里的 `mt6359_vcamio_ldo_reg` 设为 `regulator-always-on`（1.8 V）后重新扫描：

```
/dev/i2c-0 ACK: 0x28 0x58        ← 0x28 = WL2866D（reg 0x03 = 0x64）
/dev/i2c-8 ACK: 0x50 0x51        ← camera EEPROM（vendor 表里 i2c8@0x51）
```

**进展**：sensor 的 I/O 供电（vcamio）和外部 PMIC 都已确认，EEPROM 已经能在总线上看到。

**仍缺 MCLK**：sensor（S5KJN1 @ 0x20）还是不 ACK。主线 topckgen 的 `camtg_sel` mux
parent 列表里有 `univpll_192m_d8`（= 24 MHz），就是 vendor 给 sensor 的 CMMCLK 源；
只差一个消费者去 enable `camtg_sel` 并把 CMMCLK0 的 pinmux 切过去。
