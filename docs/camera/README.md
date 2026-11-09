# 📷 相机 Bring-up 文档

> 设备：红米 Note 11T 5G（`evergo` / `everpal`，MT6833）
> 分支：内核 `camera-test`（`05265ef3e`）/ 构建 `camera-test`（`45f89e6`）

## 当前状态一览

| 环节 | 状态 | 证据 |
|:---|:---|:---|
| S5KJN1 sensor 供电/I2C/探测 | ✅ | `i2c 6-0010`，chip id `0x38e1` |
| sensor 输出（MIPI CSI-2） | ✅ | CSI2 收到 RAW10 包（5100B/行、DT `0x2b`） |
| CSI-2 PHY / SENINF 静态配置 | ✅ | 与 Android live 抓取逐项比对基本一致 |
| CAM_A TG 运行 | ✅ | VS/SOF 中断 ~60 fps，TG 计数前进 |
| CAMSV3 同步（VS/SOF） | ✅ | `~22-30/s`，输入定位为 `cammux5` |
| **像素数据转发到 TG** | ❌ | TG 只有同步，无 PASS1_DONE、无 DMA 数据 |
| **CAM_A CQ 启动** | ❌ | 任何方式下 CQ 均不执行 |
| **第一帧 RAW** | ❌ | 未取得 |

## 文档索引

| 文档 | 内容 |
|:---|:---|
| [BRINGUP.md](BRINGUP.md) | bring-up 全过程记录：已做、已排除、阻塞点、下一步 |
| [REGISTERS.md](REGISTERS.md) | 地址表、关键寄存器、CQ 描述符编码、vendor golden 数据来源 |
| [TOOLS.md](TOOLS.md) | 调试工具（camraw debugfs / campeek 等）与真机测试流程 |
| [MATERIALS.md](MATERIALS.md) | 原始资料清单（Android 抓取 dump、离线提取的厂商库/表、分析文档） |

## 一句话结论

**数据链路已通到“CSI-2 收到真实 RAW10 像素包、CAM/CAMSV 的 TG 都能收到帧同步”，但像素数据没有进入 TG 像素通路，且 CAM_A 的 CQ 引擎不执行——这两点是拿到第一帧的唯一剩余障碍。**
