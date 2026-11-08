# evergo 外设支持状态：振动马达 / 感光器 / 手电筒 / 屏幕亮度

> 本文汇总 stock Android 实测数据与 mainline 仓库（/root/memtree）现有支持状态。
> 采集日期：2026-11-08，设备：Redmi Note 11T 5G (evergo)，Android 13 + root。

## 1. 振动马达（AWINIC AW8697，线性马达 LRA）

### 硬件连接（stock DT）

```
i2c9@11d01000 / aw8697_haptic@5a
compatible = "awinic,aw8697_haptic"
reset-gpio = GPIO132     irq-gpio = GPIO6
```

### Android 实测范围/参数

```
f0(谐振频率)        = 1767 Hz（校准后 f0_cali=1709，DT f0_pre=1700，f0_coeff=260）
vmax(升压)          = 0x11（DT: bst_vol_default=0x10, ram=0x11, rtp=0x10）
马达电压             = 3600 mV（dmesg: effect->vmax_mv=3600）
LRA 内阻             = lra_resistance = 8344
电池电压监测         = vbat=4479 mV（vbat_comp 开启）
默认增益             = gain = 0x80（dmesg level=0x80 / comp=79）
有效果 ID 边界       = vib_effect_id_boundary = 10
最大效果 ID          = vib_effect_max = 197 (0xC5)
RTP 时长表           = {20ms × 8, 20000, 15000, 15000, 20000, 25000, 15000, 30000, ...}
驱动强度参数         = vib_cont_drv_lev=53, drv_lvl_ov=53, cont_td=0x9a, zc_thr=0x0ff1, num_brk=3
Android HAL          = 支持 ON_CALLBACK / PERFORM_CALLBACK / AMPLITUDE_CONTROL
                       （VibratorInfo 无频率控制；frequency profile = null）
MIUI 效果属性        = sys.haptic.* 每个效果 [时长, 强度]（如 down=[5,2], runin=[13]）
```

### mainline 现状

- 驱动：`drivers/input/misc/aw8697.c`（已实现，CONFIG_INPUT_AW8697=y）
- DTS 节点：`i2c9` 下 `aw8697_haptics@5a`（reset GPIO132、IRQ GPIO6、f0_pre=1700、f0_coeff=260）
- 支持能力：evdev FF_RUMBLE + FF_PERIODIC
  - rumble 强度 = max(strong_magnitude, weak_magnitude)，范围 0..0xFFFF
  - 驱动级 `drv = 0x38 + (cont_drv_lvl-0x38) * level / 0xFFFF`
  - 注意：mainline DTS 中 `vib_cont_drv_lev = 96`，vendor DT 为 53（需确认是否为有意调高）
- 缺失：RTP 波形文件/effects 表（vendor 的 197 种效果）、HAL 级别 amplitude sysfs、f0 自动校准。

## 2. 感光器（AMS TSL2522 环境光)

### 硬件连接

```
i2c1 @0x39（与 AK09918 磁力计同总线）
vendor 侧由 SCP sensor-hub 管理；Android sensorservice 显示 "TSL2522 LIGHT"
```

### Android 实测

```
Sensor type    = android.sensor.light(5)，on-change，minRate=1Hz
实测值         = ~102 lux（室内）
HAL            = vendor.sensors-hal-2-0-multihal + SCP nanohub
```

### mainline 现状

- 驱动：`drivers/iio/light/tsl2522.c`（已实现，CONFIG_TSL2522=y）
  - 寄存器初始化序列从 vendor scp.img 逆向（"init_tsl2522"）
  - IIO 通道：`IIO_LIGHT` raw + `IIO_INTENSITY` raw（仅 RAW，无 scale）
- DTS：`i2c1` 下 `light-sensor@39`，compatible = "amstaos,tsl2522"
- 缺失：lux 换算系数（scale）、积分时间/增益配置、阈值中断、SCP 替代方案说明。

## 3. 手电筒（MT6360 PMU FlashLED，双通道）

### Android 实测

```
驱动            = mt6360_pmu_fled + flashlights_mt6360
通道数          = 2（channel 0/1，decouple）
最大 duty       = 31（5-bit 亮度级）
设备节点        = /dev/flashlight（字符设备 503:0，camera 组）
闪光灯热控      = FLAH_THERM（auxadc ch4，7 段降额表）
```

### mainline 现状

- 驱动：`drivers/leds/flash/leds-mt6360.c`（已实现，CONFIG_LEDS_MT6360=y）
- DTS：`mt6360` 下 `led-controller` → `led@4`
  - torch: `led-max-microamp = 100000`（100 mA）
  - flash: `flash-max-microamp = 750000`（750 mA）
  - `flash-max-timeout-us = 1248000`（1.248 s）
- 缺失：热降额表（vendor 有 flash_therm）、双通道独立配置、V4L2 flash 集成。

## 4. 屏幕亮度（LM36273 LCD bias + backlight）

### Android 实测

```
背光驱动        = leds_mtk_disp（/sys/class/leds/lcd-backlight）
max_brightness  = 2047（11-bit，DT led-bits=11 / trans-bits=11）
当前 brightness = 103
额外属性        = hbm（高亮模式）
LCD bias        = i2c_lcd_bias @ i2c6 0x11（mediatek,i2c_lcd_bias）
亮度校准相关属性 = persist.vendor.max.brightness=430
                   persist.sys.brightmillet.enable=true（MIUI 亮度映射）
                   persist.vendor.sys.pq.disp.ccorr.silky.brightness=0
                   persist.vendor.sys.pq.disp.gamma.silky.brightness=0
面板            = k16a_36_02_0a_vdo（DT 中 cust evergo)
```

### mainline 现状

- 驱动：`drivers/video/backlight/lm36273_bl.c`（已实现）
  - sysfs `max_brightness = 255`，芯片亮度 0..180（`level = brightness*180/255`）
  - 面板 bias：VPOS/VNEG = ±5.5 V，DVDD = 1.3 V（MT6359 VCN13）
  - 背光使能通过 pinctrl（vendor lcm-bl-enable）
- DTS：`i2c6` 下 `backlight@11`（compatible 仍是 vendor 名 "mediatek,i2c_lcd_bias"，实际为 LM36273）
- 缺失：
  - 亮度校准曲线（MIUI persistent 数据 / "brightmillet" 映射）
  - HBM 高亮模式
  - 11-bit(2047) ↔ 255 级映射的用户态策略
  - PQ（ccorr/gamma）配置

## 5. 其它已确认的连接（本次顺带）

- LCD bias: LM36273 @ i2c6 0x11（vdd = MT6359 VCN13，en = pinctrl）
- 触摸: Novatek NT36672C on SPI1
- 指纹: FPC on SPI5
- 充电: LN8000 / SC8551 / MT6360
- 摄像头: s5kjn1(i2c2 0x5a) / ov16a1q(i2c4 0x12) / imx355(i2c4 0x2b) 等
- 扬声器功放: SIA8109 / SIA8152
- NFC: i2c3 0x08

## 6. 建议的下一步

1. 振动：加 RTP 效果表（从 vendor aw8697 参数/波形文件提取）并校正 `vib_cont_drv_lev`；
2. 感光：为 tsl2522 加 scale（lux/raw）与积分时间 DT 配置；
3. 手电筒：补 flash thermal 降额与双通道；
4. 亮度：实现 MIUI 亮度映射/校准（persist.vendor.max.brightness、brightmillet），以及 HBM；
5. 这些都可以在 mainline 上逐项 bring-up，与相机链路互不冲突。

## 7. 校准实现（已提交，2026-11-08）

内核 mainline 分支已加入并通过编译验证（HEAD `e10ba2027`）：

1. `iio: light: tsl2522`：新增 DT `lux-scale-num/den`，IIO light 通道输出校准后的 lux，
   并暴露 `in_illuminance_scale`；
2. `backlight: lm36273`：新增 DT `brightness-levels`（256 级，0..180 芯片级）与
   `default-brightness-level`，以及 panel bias 校准 `vpos`/`vneg`（默认 0x1e=±5.5V）；
3. `Input: aw8697`：新增 sysfs `amplitude`（0..0xffff）/ `run`（持续时间 ms）/
   `f0`（校准频率），方便不经过 FF 层直接测振；
4. DTS：加入 256 级 gamma2.2 亮度校准表、VPOS/VNEG、TSL2522 scale 属性。

验证镜像：`D:\evergo\agent-builds\cal1-boot.img`
（sha256 `79acd2ab9b7295c5c8f6dffcc2ab4c01e3e5d045545349fac587a38d650fdad3`），
initramfs 内已带 `/root/camring`、`/root/cqring.bin`、`/root/cal-test.sh`。

### 刷机后测试

```sh
# 校准自检
/root/cal-test.sh

# 相机 CQ 回放（带 cmd 位的 439 项环）
/root/camring /root/cqring.bin
```
