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

### M2 进展（2026-11-08）

- 在 `i2c2`（adapter 6）挂上 `samsung,s5kjn1` 节点：reset-gpios=GPIO54、
  `clocks=<&topckgen CLK_TOP_CAMTG_SEL>`，assigned parent=`univpll_192m_d8`、rate 24 MHz；
- 内核把 `CONFIG_MEDIA_SUPPORT/VIDEO_DEV/VIDEO_S5KJN1` 改为 `=y` 后，驱动注册成功：

```
s5kjn1 6-0020: error -EINVAL: failed to check HW configuration
```

- `camtg_sel` 已经选到 24 MHz parent（`clk_parent=univpll_192m_d8`、`clk_rate=24000000`），
  但 probe 失败后 `clk_prepare_count=0`；
- pinmux 调试显示 **pin 49/54 仍是 UNCLAIMED**，说明 sensor 节点的 pinctrl-0
  没有真正落到 CMMCLK0/GPIO54 功能上；
- 总线扫描仍看不到 sensor 0x20（EEPROM 0x50/0x51 已可见），所以 sensor 还没 ACK。

下一步：修正 CMMCLK0 的 pinmux（可能需要给 mt6833 pinctrl 驱动补 CMMCLK 功能表项，
或直接用 devmem 验证），并用万用表/示波器确认 WL2866D 的 DVDD/AVDD 实际电压。

### M2 结果：sensor 已能 probe（2026-11-08）

- **MT6360 LDO2 (VTP) 1.8 V**：vendor cam0 的 vcamio 实际来自 MT6360 LDO2，不是 MT6359；
- S5KJN1 的 7-bit 地址是 **0x10**（vendor 表里的 0x20 是 8-bit 写地址）；
- 补齐 endpoint（data-lanes=1..4 + link-frequencies=700 MHz）后驱动通过 `check_hwcfg`；
- 真机结果：`/sys/bus/i2c/drivers/s5kjn1/6-0010` 绑定成功，chip ID 读出 **0x38e1**。

也就是说 **供电 / I2C / reset / MCLK 全链路已经通了**。下一步是 M3：
CSI-2 seninf + MRAW 驱动，见 [CAMERA-SENINF-PLAN](CAMERA-SENINF-PLAN.md) 和
[CAMERA-ISP-PLAN](CAMERA-ISP-PLAN.md)。

### M3 进展：CAM MTCMOS 电源域已补（2026-11-08）

- 主线原来只有 CONN + MFG0..3 电源域，CAM/CAM_RAWA/CAM_RAWB 未建模，
  访问 0x1a000000/0x1a04f000/0x1a06f000 会直接挂死；
- 内核提交 `1bf31515`：补 3 个 camera 电源域（vendor `clk-mt6833-pg.c` 寄存器
  0x035c/0x0360/0x0364、status bit 23/24/25、SRAM PDN bit8/ack bit12），
  并在 DTS 里让 camsys 三个 clock controller 挂到对应 genpd；
- 真机验证：`pm_genpd_summary` 出现 `cam` / `cam_rawa` / `cam_rawb` 且为 **on**，
  三个 `1a0*.clock-controller` 不再 deferred probe 超时（之前 -110），
  GPU/传感器/WiFi/显示等其余系统正常；
- 还没做：CAM 的 INFRA_TOPAXI_PROTECTEN_MM/_VDNR bus protection（vendor 有，
  主线没有），所以域目前只上不下。

下一步：seninf + CAMSV DT/驱动（见 [CAMERA-SENINF-PLAN](CAMERA-SENINF-PLAN.md)）。

### M3 进展 2：CSI D-PHY 已接入（2026-11-08）

- 内核提交 `e16da3dd`：`phy-mtk-mipi-csi-0-5` 增加 `mediatek,mt6833-csi-rx`
  compatible；SoC DTS 描述 `0x11c80000` 的 MIPI CSI ANA（4 lane、D-PHY、
  CAM 电源域），board DTS 打开；
- 配置 `CONFIG_PHY_MTK_MIPI_CSI_0_5=y`；
- 真机验证：`/sys/class/phy/` 出现 `phy-11c80000.phy`，S5KJN1 仍正常绑定并读
  chip ID，CAM 电源域保持 on。

下一步：SENINF + CAMSV 节点与最小 V4L2 采集驱动。

### M3 进展 3：SENINF 节点 + bring-up 驱动（2026-11-08）

- 内核提交 `9b8da3d2`：
  - 新增 `drivers/media/platform/mediatek/seninf/`，最小平台驱动映射
    `0x1a004000`（SENINF_TOP + 各 brick）、使能 SENINF clock、用 debugfs
    `mtk-seninf/reg` 暴露 crossbar/IRQ/checker 寄存器，便于在不重编内核的情况下
    做寄存器级调试；
  - `mt6833.dtsi` 增加 seninf 节点（`CLK_TOP_SENINF_SEL` + CAM 电源域），board 打开；
- 配置 `CONFIG_VIDEO_MTK_SENINF=y`；
- 真机验证：

```
mtk-seninf 1a004000.seninf: seninf ready: mux0=03020100 mux1=07060504
debugfs: mux_ctrl0=03020100 mux_ctrl1=07060504 mux_ctrl2=0b0a0908 mux_ctrl3=0f0e0d0c
```

下一步：CSI-2 lane/VC/DT 配置 + CAMSV TG/DMA，把 RAW10 帧抓出来。
