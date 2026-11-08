# Android 相机组件全量调查（evergo / MT6833 / Android 13）

> 采集时间：2026-11-08；设备：Redmi Note 11T 5G (evergo)，Android 13，adb root（su）+ SELinux permissive
> 采集方式：adb push 静态工具 + `/dev/camera-isp`、`/dev/seninf` mmap；vendor CQ/dump debug 属性
> 原始数据：同目录 `phase1/`、`phase2/`、`final/`（含 live 寄存器、dmesg、logcat、m4u、.reg dump）

## 1. 设备与环境

- `ro.product.device=evergo`，Android 13
- 内核：vendor MT6833（`CONFIG_DEBUG_FS is not set`、`CONFIG_PROC_KCORE is not set`）
- 相机 sensor：**S5KJN1**（`/proc/driver/camera_info`）
  - `Pre/Cap: TgGrab=4080x3072`，`Vid=4080x2296`，mclk=24MHz，4-lane MIPI
  - `Seninf_Type=1 (MIPI)`，`PDAF_Support=2 (VC Full)`
  - 第二/第三摄：ov16a1q（2304x1728）、imx355（3280x2464，HDR）

## 2. 设备节点与调试接口

```
/dev/camera-isp    510,0   system:camera   ← CAM_A/B/C 寄存器 mmap 可用
/dev/camera-dip    509,0   system:camera   ← CAMSYS_MAIN mmap 失败（No such device）
/dev/seninf        504,0   system:camera   ← SENINF 寄存器 mmap 可用
/dev/ion           10,126  graphics
/dev/irtx          480,0   system:system   ← 红外
无 /dev/mem、无 /proc/kcore、无 debugfs
```

- `/dev/camera-isp` 可以 mmap：`0x1a030000`(CAM_A)、`0x1a038000`(CAM_A_INNER)、`0x1a050000/0x1a070000`、CAMSV 区
- **不能** mmap：`0x1a000000`(CAMSYS_MAIN)、CQ 缓冲 IOVA（返回 EAGAIN）
- `/proc/driver/isp_reg`、`/proc/driver/camio_reg` 权限 0000，不可用
- `/proc/m4u_dbg/{debug,buffer,domain,help}` 可用（root）：提供 IOVA→PA 映射表

## 3. 相机 HAL / 服务 / 库

- 进程：`cameraserver`、`camerahalserver`、`com.android.camera`
- 服务：`media.camera`、`consumer_ir` 等
- 关键库（`/vendor/lib64/`）：
  - `libcamdrv_isp.so`（CQ 描述符生成/管理）
  - `libimageio_plat_drv.so`（P1 配置/图像 IO，含 debug dump）
  - `libcam.halisp.so`、`libcam.iopipe.so`、`libcam.halisp.buf/common`
  - `libcameracustom.camera.isp.so`（CAMSV custom decode）
  - `libcam.seninfn3d.so`、`libcam.halsensor.so`
- HAL 实现：`/vendor/bin/hw/android.hardware.camera.provider@2.4-service_64`（mtkcam）

## 4. 设备树相机节点

```
cam1@1a030000  cam1_inner@1a038000
cam2@1a050000  cam2_inner@1a058000
cam3@1a070000  cam3_inner@1a078000
camsv3@1a092000 ~ camsv8@1a097000
seninf@1a004000（节点名在 dt 中，见 dt_nodes.txt）
camsys@1a000000 / camsys_a@1a04f000 / camsys_b@1a06f000 / camsys_c@1a08f000
cam_smi_subcom@1a00c000 / @1a00d000
相机 EEPROM: camera_eepromdrv0..8
```

## 5. 实时寄存器（相机预览中，CAM_A）

| 寄存器 | 偏移 | 值 |
|:---|:---|:---|
| CTL_EN | 0x004 | `0x0516ffef` |
| CTL_EN2 | 0x008 | `0x3a3ffd88` |
| CTL_EN3 | 0x00c | `0x01ebb01e` |
| CTL_SEL | 0x040 | `0x06072d90` |
| CTL_FMT_SEL | 0x048 | `0x01081091` |
| CTL_MISC | 0x054 | `0x00360074` |
| CTL_SW_CTL | 0x07c | `0x00000002` |
| RAW_INT_EN | 0x100 | `0x00877c73` |
| RAW_INT7? | 0x158 | `0x00025e01` |
| **CQ_EN** | 0x200 | **`0x00000112`** |
| **CQ_THR0_CTL** | 0x210 | **`0x00000021`** |
| **CQ_THR0_BASEADDR** | 0x214 | **`0xf8662000`**（每帧变化） |
| **CQ_THR0_DESC_SIZE** | 0x218 | **`0x00000DB8`**（439 描述符） |
| TG_SEN_MODE | 0x1f00 | `0x00016001` |
| TG_VF_CON | 0x1f04 | `0x00000001` |
| TG_INTER_ST | 0x1f3c | `0x30300403` |

CAM_A_INNER 同样有一套 CQ 寄存器：`0x200=0x112`、`0x210=0x21`、`0x214=0xf8669000`、`0x218=0xdb8`。

> 结论：vendor CQ 配置与 mainline 我们设置的值完全一致（0x112 / 0x21 / 0xDB8），差异不在这些寄存器值。

## 6. P1CB dump（`.reg` 文件）解析

`/data/vendor/camera_dump/161112330-XXXX-main-P1CB-Preview_p1.reg`（32776 字节）

结构：
```
offset 0x0: u32 0x2001 (magic?)
offset 0x4: u32 0x0000000c
offset 0x8: 0x8000 字节 = CAM_A 寄存器镜像 (offset i*4 = 寄存器偏移)
```
- 未编程寄存器用 **`0xfefefefe`** 填充；
- 有效编程寄存器 **3883 个**；
- 已验证镜像值与寄存器对应关系：
  - `0x4 = 0x071effef`（预览模式 CTL_EN）
  - `0x8 = 0x3a3ffd88`（CTL_EN2，与 live 一致）
  - `0x40 = 0x06072d90`（CTL_SEL）
  - `0x48 = 0x01081091`（FMT_SEL）
  - `0x54 = 0x00120074`（CTL_MISC）
  - `0x214 = CQ_THR0_BASEADDR`
- 两帧差分（59 个 u32 不同），关键：
  - `0x214`: `0xfec6d000 → 0xfec6b000`（CQ 缓冲每帧切换，说明是环形 CQ buffer）
  - `0x82c/0x830/0x834...`: 统计/计数器
  - `0x3a00/0x3a04/...`: 帧计数/状态
- 说明该文件是 **HAL 的 P1 寄存器配置镜像**（不是 CQ 硬件描述符原始字节）。

## 7. M4U IOVA→PA 映射（/proc/m4u_dbg/buffer）

相机端口（camera halserver 进程）：
```
port 512(16-0) L16_CAM_IMGO_R1_A : IOVA 0x2e5966000~... -> PA 0x4d1b0000~...
port 512(16-0) L16_CAM_IMGO_R1_A : IOVA 0x2e5bb2000~... -> PA 0xf6940000~...
...
```
共 245 行 CAM 相关映射；说明 CQ/IMGO 走 M4U，CQ buffer 地址是 IOVA（每帧变化）。

## 8. CQ / dump 调试手段

- 属性：
  - `vendor.debug.isp.dumpcq.enable=1`
  - `vendor.debug.isp.dumpcq.framecnt=2`
  - `vendor.debuglog.imageio.*`（CMDQMgr、cam_cap 等日志开关）
- 注意：**selinux enforcing 下这些 vendor 属性无法设置**；需要 `setenforce 0`（已实测可设置成功）
- 启用后生成 `/data/vendor/camera_dump/**.reg`（P1 寄存器镜像）与 `shading_CT/*.log`
- libimageio 里还存在 `/data/vendor/camera_dump/full_dump.csv` 路径（本次未生成，可能需其他条件/HDR 场景）

## 9. 对 mainline 的意义

1. CQ 控制寄存器值与 mainline 一致，问题不在 CQ_EN/CTL/DESC_SIZE；
2. vendor HAL 的 P1 配置镜像（3883 个寄存器写入）可导出，可作为 mainline **CAM_A 配置白名单**；
3. CQ 缓冲每帧切换（0xfec6d000/0xfec6b000 …），描述符内容不可从 userspace 读取：
   - 无 `/dev/mem`、无 `/proc/kcore`、无 debugfs；
   - `/dev/camera-isp` 无法 mmap CQ IOVA；
   - `/proc/m4u_dbg` 只提供 IOVA→PA 映射，不能读 DRAM；
4. 下一步可行方案：
   - 通过 M4U 表找到 CQ buffer 的物理地址后，用某个可 DMA 的设备/驱动把内容搬到可读区域（复杂）；
   - 或从 `libcamdrv_isp.so` 的 `dumpCQTable`/`dumpToFile_camcq_info` 触发 HAL 自身把 CQ 描述符写到文件（需要找到对应 property/ioctl 条件）；
   - 或继续静态反汇编（`CQ_SetContent` 的 vtable 分派实现）确定数据字编码。

## 10. 原始数据索引

```
phase1/    环境、组件、DT、dmesg、logcat（预览前）
phase2/    首次寄存器 dump、kcore 检查
final/     live_regs.txt、*.reg、m4u_buffer.txt、camera_info.txt、dmesg/logcat
```
所有原始文件保存在 `D:\evergo\android-camera-dump\` 与工作区 `android-camera-dump\`。