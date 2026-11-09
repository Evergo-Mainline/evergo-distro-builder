# 📦 相机原始资料清单

> 大文件不放进 git；本页记录资料位置、内容与来源，方便后续复现。

## 1. Android 运行态抓取（工作区）

路径：`D:\evergo\android-camera-dump\`（约 24MB）

| 文件 | 内容 |
|:---|:---|
| `final/live_regs.txt` | 相机预览时全窗口寄存器 dump：`SENINF_REG_ALL`（288 项）、`SENINF_ANA_ALL`（271 项）、CAMSYS/CAM/CAMSV 等 |
| `final/161112330-*-main-P1CB-Preview_p1.reg` | P1 预览寄存器镜像（0x8000 字节，0xfefefefe = 未编程，3883 项已编程） |
| `camA.txt` | CAM_A 全寄存器 dump（5092 行，2557 个唯一偏移） |
| `camsv_live2.txt` | CAMSV3 运行态寄存器（41 项，本仓库 REGISTERS §4 的来源） |
| `cq_ring_full_rebuilt.bin` / `cq_ring_cam_a_rebuilt.bin` | 重建的 CQ 环（439×8 字节，含 vendor IOVA 描述符） |
| `cq_module_info.txt` / `cq_table_cam_a.txt` | CQ 模块表/描述符解析 |
| `ANDROID_CAMERA_FULL_DUMP.md` | 抓取过程与结论汇总 |
| `PERIPHERALS-STATUS.md` | 外设状态/调试属性记录 |
| `dmesg.txt` / `logcat_p5.txt` / `trace_ioctl.txt` | 内核日志、HAL 日志、ISP ioctl 追踪 |
| `m4u_buffer.txt` / `mtk_iommu.txt` | IOMMU/m4u 映射（含 CQ 环的 vendor IOVA） |

## 2. 原厂 ROM 与提取物

| 位置 | 内容 |
|:---|:---|
| `D:\evergo\rom\super.raw` | 原厂 super 镜像（9.1GB） |
| `D:\evergo\rom\parts\vendor_a.img` | 已拆出的 vendor 分区（ext4） |
| `/root/hal/`（WSL） | 从 vendor 提取的相机库：`libcamdrv_isp.so`、`libimageio*.so`、`libcameracustom*.so` 等 |
| `/root/dtb_0.dtb`（WSL） | 从原厂 boot 镜像提取的 DTB（GCE/CAMSYS 陈旧映射的来源） |
| `/root/android-kernel/`（WSL） | vendor 内核源码（含 `s5kjn1sunny_mipi_raw.c`、camera_isp.c、seninf 驱动等） |

## 3. 工作区分析文档（早期调研）

| 文件 | 内容 |
|:---|:---|
| `D:\evergo\CAMERA-SENINF-PLAN.md` | SENINF/CSI-2 侧调研与计划（44KB） |
| `D:\evergo\CAMERA-ISP-PLAN.md` | CAMSYS/ISP/CQ 侧调研与计划（39KB） |
| `D:\evergo\ANDROID_CAMERA_INVESTIGATION.md` | Android 抓取清单与方法 |
| `D:\evergo\camera-doc.md` | 早期"相机/基带支持分析"（已过时，以本目录为准） |

## 4. 调试工具源码/二进制

| 位置 | 内容 |
|:---|:---|
| `D:\evergo\tools\` | 设备端工具：`camcap`、`campeek`、`campoke`、`camdump`、`camgraph`、`camdirect7`、`caminfo` 等 |
| 仓库 [`tools/camera/`](../../tools/camera/) | 上述工具源码 + Makefile（`make` 交叉编译 aarch64 静态二进制） |
| `D:\evergo\agent-builds\` | 历史 boot 镜像：`camtest19`…`camtest28`、已知可用基线 `known-good-boot.img` |

## 5. 复现抓取的方法概要

- 寄存器：root + `/dev/mem`（或 vendor `/dev/camera-isp` mmap）
- P1 镜像：相机预览时 dump CAM_A，按 0xfefefefe 标记识别未编程寄存器
- CQ 环：读 `CAM_A 0x214/0x218` 得到 base/size，再从 m4u 映射或 `/proc/kcore` 读出 0xdb8 字节
- 内核/HAL 日志：`dmesg`、`logcat -b all`、ISP ioctl 的 kprobe trace
