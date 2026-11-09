# 🧰 相机调试工具（aarch64 静态）

源码配套 [docs/camera/TOOLS.md](../../docs/camera/TOOLS.md)，在设备（root）上运行。

## 构建

```bash
cd tools/camera
make            # 需要 aarch64-linux-gnu-gcc；产物为静态二进制
# 产物：camcap campeek campoke camdump camgraph caminfo camprobe
```

## 工具一览

| 工具 | 用法 | 作用 |
|:---|:---|:---|
| `camcap` | `camcap /dev/video0` | 最小 V4L2 MMAP 采集（申请 4×25MB 缓冲，打印 G/S_FMT 与帧） |
| `campeek` | `campeek 0x1a030004 [count] [stride]` | 通过 `/dev/mem` mmap 读物理寄存器 |
| `campoke` | `campoke 0x1a030150 0x5a5a5a5a` | 写物理寄存器 |
| `camdump` | `camdump 0x1a004000 0x2800 > out.bin` | 整段 dump 到二进制（用于与 Android live 差分） |
| `camgraph` | `camgraph /dev/media0` | 打印 media controller 拓扑与链接状态 |
| `caminfo` | `caminfo [--wait ms]` | 只读打印 CAM_A 状态 + camraw irqstats |
| `camprobe` | `camprobe [xsize] [stride] [ctl_en]` | 直接配置 CAM_A（P1 风格）+ 启动 TG，轮询 INT 状态 |

> 注意：这些工具直接操作 `/dev/mem` 与 CAM 寄存器，**仅用于 bring-up 调试**。
> 更复杂的历史工具（`camdirect7`、`camring*` 等）依赖自动生成的寄存器表头（`camboth.h` 等），未收入本仓库。
