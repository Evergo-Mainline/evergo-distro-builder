# 🧭 后续开发计划与待办（EVERGO-CONTINUE）

> 本页汇总"主线已经可用"之外、仍值得继续做的方向。历史细节见 [KERNEL](KERNEL.md)、[HARDWARE](HARDWARE.md)、
> [TROUBLESHOOTING](TROUBLESHOOTING.md)；相机专项见 [camera/README](camera/README.md)。

## 🎯 近期优先

| 方向 | 状态 | 说明 | 相关 |
|:---|:---|:---|:---|
| 相机 bring-up（第一帧） | 🚧 进行中 | sensor 已输出 RAW10、CSI-2/TG 已通；仍缺"像素进入 TG 像素通路"和 CAM_A CQ 启动 | [camera/BRINGUP](camera/BRINGUP.md) |
| 电源键亮屏（DRM unblank 超时） | 🚧 未修 | 见 [MODULES](MODULES.md) 已知风险 #1 | [MODULES](MODULES.md) |
| PSCI idle（`cpuidle-exp`） | 🚧 实验分支 | 状态生效，但压力/休眠下会挂，未合并 | [KERNEL](KERNEL.md) |
| 蓝牙文件传输 | ❓ 待实测 | 内核配置已补，文件传输未验证 | [MODULES](MODULES.md) |

## 📷 相机下一步（选项）

1. **A（最快）**：短暂刷回 Android，运行时抓取 SENINF/CSI2 像素通路与 CQ 启动的寄存器写时序，抓完恢复 mainline
2. **B**：拿到 MT6833 SENINF/CQ 寄存器手册或 vendor 预编译模块/源码
3. **C**：降低目标，先验证 CAMSV 小数据通路

详见 [camera/BRINGUP.md §6](camera/BRINGUP.md)。

## 🔧 中期方向

| 方向 | 说明 |
|:---|:---|
| 超广角/微距/前摄 | 主摄打通后，按同链路接 OV8856 / OV02 系 / IMX355（sensor 驱动已在树内） |
| libcamera pipeline handler | MTK 平台 handler + sensor 3A；避免依赖 vendor 闭源库 |
| 标定数据 | AF/闪光/LSC 数据在 vendor `/vendor/etc/camera`，格式未公开，需要逆向解析 |
| 基带 / SIM | 无 CCCI/MDF 驱动，属长期工程，见 [CAMERA-MODEM](CAMERA-MODEM.md) |

## 🧪 稳定性与体验

- 传感器：TSL2522 ALS、LSM6DSO 未完全可用（见 [HARDWARE](HARDWARE.md)）
- 休眠：s2idle 已验证可进/出，默认不自动休眠（策略待定）
- 随机 panic 抓取流程见 [PANIC](PANIC.md)；压力测试方法见 [STRESS](STRESS.md)

## 🤝 参与方式

- 内核改动：`Evergo-Mainline/linux_evergo`（基线 `master` = `865d67f39`，开发分支按主题命名）
- 构建/打包：`Evergo-Mainline/evergo-distro-builder`（`master` 保持已验证镜像，实验在特性分支）
- 规则：**能真机构建、刷写、测试通过后再提交**；实验性调试代码放特性分支
