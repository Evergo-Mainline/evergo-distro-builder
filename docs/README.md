# 📚 Evergo 文档索引

> 红米 Note 11T 5G（`evergo` / `everpal`，MT6833）mainline Linux 的全部文档。
> 建议顺序：**新用户 → 刷机教程；开发者 → 构建指南 + 内核记录；排障 → 踩坑记录。**

<div align="center">

| 🚀 上手 | 🛠️ 开发 | 🔍 排障 |
|:---|:---|:---|
| [刷机教程](TUTORIAL.md) | [构建指南](BUILDING.md) | [踩坑记录](TROUBLESHOOTING.md) |
| [镜像矩阵](../README.md#️-镜像矩阵) | [内核提交](KERNEL.md) | [抓 panic](PANIC.md) |
| [硬件支持](HARDWARE.md) | [功能/模块](MODULES.md) | [压力测试](STRESS.md) |
| [常见命令](../README.md#-常用命令) | [内核配置](../config/evergo.config) | [vendor 移植](VENDOR-PORT.md) |

</div>

---

## 📖 文档一览

| 文档 | 一句话说明 | 适合谁 |
|:---|:---|:---|
| [TUTORIAL.md](TUTORIAL.md) | 下载 → fastboot → 首次开机 → SSH/Wi-Fi 全流程 | 第一次刷机的人 |
| [BUILDING.md](BUILDING.md) | 本地/CI 构建、发行版矩阵、内核配置、产物说明 | 想自己出包的人 |
| [MODULES.md](MODULES.md) | 每个硬件功能对应的内核模块、固件、补丁 | 想改内核的人 |
| [HARDWARE.md](HARDWARE.md) | CPU/GPU/充电/传感器/Snap/PD 的实测支持状态 | 想确认功能是否可用 |
| [KERNEL.md](KERNEL.md) | 每个内核提交做了什么、实测数据如何 | 跟进内核进展 |
| [PANIC.md](PANIC.md) | 无串口时怎么从 expdb 取崩溃日志 | 遇到随机 panic |
| [STRESS.md](STRESS.md) | CPU/GPU/内存/IO 压力测试方法与最终数据 | 做稳定性验证 |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | 按症状查：启动失败/不扩容/连不上/构建报错 | 出问题的人 |
| [VENDOR-PORT.md](VENDOR-PORT.md) | 从 vendor 内核移植功能的思路与踩坑 | 深入开发者 |
| [EVERGO-CONTINUE.md](EVERGO-CONTINUE.md) | 后续开发计划与待办 | 想接着做的人 |
| [CAMERA-MODEM.md](CAMERA-MODEM.md) | 相机/基带移植调研记录（相机部分已过时，见 camera/） | 做外设移植的人 |
| [camera/README.md](camera/README.md) | 📷 相机 bring-up 总览：当前状态、文档索引 | 跟进相机进展 |
| [camera/BRINGUP.md](camera/BRINGUP.md) | 相机 bring-up 全过程：已完成、已排除、阻塞点、下一步 | 做相机驱动的人 |
| [camera/REGISTERS.md](camera/REGISTERS.md) | 相机寄存器/数据格式速查（CAM_A/CQ/SENINF/CAMSV） | 查寄存器/写驱动 |
| [camera/TOOLS.md](camera/TOOLS.md) | 相机调试工具（camraw debugfs、campeek 等）与真机流程 | 复现实测 |
| [camera/MATERIALS.md](camera/MATERIALS.md) | 相机原始资料清单（Android dump、厂商库、历史镜像） | 找原始数据 |

---

## 🧭 常见问题速查

| 症状 | 先看 |
|:---|:---|
| 刷完黑屏 / 不进系统 | [TROUBLESHOOTING · 启动类](TROUBLESHOOTING.md) |
| 存储没变成 106 GiB | [TROUBLESHOOTING · 扩容](TROUBLESHOOTING.md) |
| SSH 连不上 `169.254.42.1` | [教程 · 连接](TUTORIAL.md) |
| Wi-Fi 扫描不到 | [踩坑记录 · 网络](TROUBLESHOOTING.md) |
| 随机 panic / 死机 | [PANIC.md](PANIC.md) |
| 构建缺少依赖 / 失败 | [BUILDING.md](BUILDING.md) |
| 想开更高频率 | [KERNEL.md](KERNEL.md) + [STRESS.md](STRESS.md) |

---

> 💡 文档里的命令默认以 `root` 用户执行（密码 `evergo`）；本地构建以 Ubuntu 24.04 为参考环境。
>
> 📜 本仓库文档与代码均以 **GPL-3.0-or-later** 发布，见 [LICENSE](../LICENSE)。
