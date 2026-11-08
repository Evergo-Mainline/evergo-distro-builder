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
## 11. 追加调查（CQ 缓冲区可达性）

### 11.1 vendor ISP ioctl 清单（camera_isp.h, mt6853）

```
ISP_CMD_READ_REG / WRITE_REG       寄存器读写（按 module）
ISP_CMD_ION_GET_PA / MAP_PA / UNMAP_PA   ION fd -> 物理地址
ISP_CMD_NOTE_CQTHR0_BASE            HAL 通知内核 CQ base（内核存在 g_cqBaseAddr）
ISP_CMD_CQ_SW_PATCH                 模拟 CQ 更新（原子行为）
ISP_CMD_DUMP_BUFFER / GET_DUMP_INFO dump 接口（DUMP_BUFFER 在本内核无实现）
ISP_CMD_LARB_MMU_CTL                larb MMU 开关
ISP_CMD_DEBUG_FLAG                  日志等级
```

- `ISP_CMD_DUMP_BUFFER` 在 `camera_isp.c` 中**没有实现 case**，无法据此读 CQ 内容；
- `/proc/driver/isp_reg`、`camio_reg` 权限 0000；
- 内核无 `CONFIG_DEBUG_FS`、`CONFIG_PROC_KCORE`，无 `/dev/mem`。

### 11.2 M4U IOVA→PA 映射（`/proc/m4u_dbg/buffer`）

相机相关端口（camerahalserver）示例：

```
port 512(16-0) L16_CAM_IMGO_R1_A : IOVA 0x2e5966000 -> PA 0x4d1b0000
port 512(16-0) L16_CAM_IMGO_R1_A : IOVA 0x2e5bb2000 -> PA 0xf6940000
...
```
共 245 行 CAM 相关映射。CQ 缓冲（0xf8662000 一类）同样走 M4U。

### 11.3 进程内存扫描（尝试直接找 CQ 描述符）

- 工具：静态 `memscan`（/proc/<pid>/mem 扫描，root + SELinux permissive）
- 扫描 `camerahalserver` 全部可读映射（跳过 >128MB 的映射）
- 结果：
  - 按“编码地址”模式（0x18000054 / 0x08000054 / 0x18000004 …）命中大量库数据/堆数据，无真实 CQ 描述符序列；
  - 按“已知寄存器值”模式（0x06072d90 / 0x00360074 / 0x3a3ffd88 …）找到的是
    **HAL 的寄存器配置数组**（连续寄存器值），并非 (地址,数据) 描述符对；
  - 与 live CAM_A 寄存器映射交叉匹配，描述符匹配数为 0。
- 结论：CQ 描述符缓冲区很可能通过 **非缓存 ION 映射**写入（用户态 VA 不在常规可读映射中），
  或由第三方进程/内核侧持有，无法从 userspace 进程内存直接获取。

### 11.4 下一步可行方案

1. 通过 `ISP_CMD_ION_GET_PA`（或 `/proc/m4u_dbg/buffer`）定位 CQ buffer 物理地址后，
   寻找能读该 PA 的内核接口（当前无 /dev/mem，需要额外内核侧手段）；
2. 让 HAL 自身把 CQ 描述符写盘：`libcamdrv_isp.so` 的 `dumpCQTable` / `dumpToFile_camcq_info`
   需要在 HAL 进程内调用（可通过注入/property 触发，待定位触发条件）；
3. 继续静态反汇编 `CQ_SetContent` 的 vtable 分派实现（APS2 重定位解析），直接从二进制推导数据字格式。

## 12. 运行时 vtable / CQ 对象结构（新突破）

### 12.1 绕过 APS2 重定位

`libcamdrv_isp.so` 的 `.rela.dyn` 是 Android 打包重定位（SHT_ANDROID_RELA），静态解析困难。
改为**从运行中的 `camerahalserver` 进程内存直接读取已重定位的 vtable**：

- 映射：`libcamdrv_isp.so` base=0x704ae0c000；
- vtable 文件偏移 0x2c2d8 → 运行时 0x704ae382d8；
- 用 `/proc/<pid>/mem` 读出每个槽位，反算回文件偏移后反汇编。

已解析：
```
ISP_DRV_CAM vtable slot26 (vptr+192) = 0x27be4  -> ISP_DRV_CAM::writeRegs(ISP_DRV_REG_IO_STRUCT*, u32, int)
IspDrvImp vtable slot... = 0x1308c (setSCQMode) 等
```

### 12.2 ISP_DRV_CAM 对象结构（来自 HAL 进程内存）

找到 25 个 ISP_DRV_CAM 实例（CAM_A/B/C 的每个 CQ 线程一个），每个对象 0x200 字节：

```
+0x000  vptr = ISP_DRV_CAM vtable+0x10
+0x130  "m1:ispCQDesc[module][thread]" / "m2:ispCQDesc[module][thread]"  (名字)
+0x0f0/+0x100  指向 CQ 描述符缓冲区 VA / IOVA（随线程状态变化）
+0x50   影子寄存器数组指针（writeRegs 写入或读取的 u32 数组，按寄存器偏移索引）
```

- `ISP_DRV_CAM::writeRegs` 的实际行为：把值写入 **影子寄存器数组** `[obj+0x50]`（偏移索引、0xfefefefe 表示未写），
  这正好解释了 P1CB `.reg` 文件的格式（0x8000 寄存器镜像 + 未写填充）；
- CQ 描述符缓冲区以 **(VA, IOVA)** 对的形式记录在每个 CQ 线程对象中（IOVA 形如 0x02fbb…、0x2fbb…）。

### 12.3 下一步

- 在预览运行时读取对应 CAM_A CQ 线程对象的 `+0x100` 字段得到 CQ 描述符缓冲区 VA，
  然后用 `/proc/<pid>/mem` 直接 dump 0xDB8 字节，解析真实描述符格式（数据字编码）；
- 或直接调用 libcamdrv 的 `dumpCQTable`/`dumpToFile_camcq_info`（需在 HAL 进程内或找到触发条件）。

## 13. CQ 描述符缓冲区定位（对象字段 + PFNMAP 限制）

每个 `ISP_DRV_CAM` 对象（0x200 字节）由两个 0x100 子对象组成：

```
+0x000  vptr
+0x088  ≤子对象1> 缓冲区 VA    +0x090 IOVA    名字 @+0x0B0  "ispVirReg[...]" 
+0x108  ≤子对象2> 缓冲区 VA    +0x110 IOVA    名字 @+0x130  "mX:ispCQDesc[module][thread]"
```

实测（预览中）：

```
obj m1:ispCQDesc[0][5]  : VA=0x70ecd1f000  IOVA=0x02fec6e000
obj m1:ispCQDesc[0][4]  : VA=0x70ed020000  IOVA=0x02fea0... 
obj m1:ispVirReg[0][5]  : VA=0x70ece39000  IOVA=0x02fea04000
```

- 每个 CQ 线程一个对象，名字里的 `[module][thread]` 对应 CAM 模块与 CQ 线程号；
- `ispVirReg` 子对象就是 **影子寄存器数组**（0x8000 寄存器镜像，P1CB .reg 的来源）；
- `ispCQDesc` 子对象对应 **CQ 描述符缓冲区**。

### 限制：VA 不可通过 /proc/<pid>/mem 读取

对上述 VA 使用 `pread(/proc/<pid>/mem)` 返回 **EIO**：
这些缓冲区是 ION/dma-buf 映射（VM_PFNMAP），用户态进程映射不可被常规内存读取接口访问。

### 可行突破路径

1. **ptrace syscall 注入**：attach 到 `camerahalserver`，劫持线程寄存器直接调用 `openat/write` 系统调用，
   把 `ispCQDesc` 的 VA 内容写到 `/data/vendor/camera_dump/`，再 pull；
2. 通过 `camera-mfb`（MFB 位块引擎）做内存到内存复制（CQ IOVA -> 可读缓冲）；
3. 触发 HAL 自身 dump 路径（`dumpCQReg` / `full_dump.csv`），需要找到对应条件/属性。

> 备注：设备共 3 个摄像头（见 `/proc/driver/camera_info`）：
> 1. s5kjn1（主摄 4080x3072）
> 2. ov16a1q（前置 2304x1728）
> 3. imx355（超广角 3280x2464，HDR）
> 三个 sensor 的 HAL 配置对象均可在同一份 dump 中获取（m1/m2/m3 组）。

## 14. 成功导出 vendor CQ 描述符缓冲区（ptrace 注入 memcpy）

### 方法

PFNMAP(dmabuf) 映射不能被 `/proc/<pid>/mem`、`process_vm_readv`、`PTRACE_PEEKDATA` 读取（均为 EFAULT/EIO）。
最终方案：**在 camerahalserver 进程内执行一次 memcpy**，把目标缓冲区复制到可读的线程栈，再读栈。

步骤（工具 `injmemcpy`）：
1. 从 target maps 找到 libc 基址，`memcpy` 偏移 0x88ae0；
2. ptrace attach，保存寄存器；
3. 设置 `x0=sp-0x2000`(目标栈地址)，`x1=CQ缓冲区VA`，`x2=0x1000`，`x3=0`，`LR=0`（memcpy 返回后取指异常触发 SIGSEGV stop），`PC=memcpy`；
4. `PTRACE_CONT` 等 stop，恢复寄存器、detach；
5. 用 `/proc/<pid>/mem` 读取 `sp-0x2000` 的 0x1000 字节。

实测：HAL 进程存活，成功导出 4096 字节 CQ 描述符缓冲区。

### 抓到的 CQ 缓冲区内容（示例，CAM 模块）

```
idx w0        base off    cmd  w1
000 280044a0  05   44a0   000  fef19030
001 280044cc  05   44cc   000  fef19034
002 280244f0  07?  44f0   002  fef19038
003 2800028c  05   028c   000  fef19068
004 28004494  05   4494   000  fef19068
005 06000000  00   0000   600  00000000      <- CQ END
006 fb783000  1f   3000   378  fb65e040
007 000000ee  00   00ee   000  fb783000
```

- **END 标记 `0x06000000` 位置确认**；
- 描述符确实是 8 字节 (word0=编码地址+命令位, word1=数据)，与反汇编得出的编码一致；
- vendor 在此 CQ 线程里使用的 module id 字段为 **5**，data 为 IOVA（0xfef19xxx / 0xfb78xxxx），
  说明该线程向 CAM 侧写入 DMA 基地址/配置；不同 CQ 线程对应不同 module（m1/m2）。

### 待办

- 过滤出主摄（s5kjn1 / CAM_A P1）的 CQ 线程对象，导出其缓冲区并逐条解码（区分 CAM_A/CAM_B/CAMSV）；
- 用抓到的真实描述符序列对照 mainline 的 CQ ring 初始化，确认缺失的 CQ 上下文/时钟/ring 设置。

## 15. 全部 CQ 线程缓冲区导出结果（29 个）

用 `injmemcpy` + `scanobjs` 遍历所有 `ispCQDesc` 对象，成功导出 29 个 CQ 缓冲区（4KB each），HAL 全程存活。

分类：

```
9 个 "小" 缓冲区: 41 个非零 8 字节项，含 4 个 END(0x06000000)，module base 为 5/3/28/30/31
20 个 "大" 缓冲区: 439 个非零项，含 1 个 END  -> 正好等于 DESC_SIZE=0xDB8 / 8 = 439 描述符！
                    其中 8 个 base=3（CAM_A）、8 个 base=5（CAM_B）、4 个 base=5/其他
```

439 项与 `CQ_THR0_DESC_SIZE=0xDB8` 完全吻合，确认抓到的是真实 CQ 描述符环形缓冲。

### 内容特征（以 CAM_A 439 项表 cqall_19.bin 为例）

```
idx 000..008: w0=18004494  w1=fbccf000+...   (dummy 填充，与反汇编得到的 dummy 0x18004494 一致)
idx 012: w0=18000004  (base3 off4  CTL_EN)     w1=fbccf004
idx 013: w0=18000040  (CTL_SEL)                w1=fbccf040
idx 014: w0=18000048  (FMT_SEL)                w1=fbccf048
idx 015: w0=18000054  (CTL_MISC)               w1=fbccf054
idx 218: w0=18000214  (CQ_THR0_BASE)           w1=fbccf214
...
END (0x06000000)
```

**新发现**：描述符的 word1 不是直接寄存器值，而是 `0xfbccf000 + offset` 形式的**指针/地址**（每项与 word0 的 offset 同步），
说明该缓冲在抓取时刻是 "**寄存器地址表 / 前编码表**"，还不是最终发给硬件的数据（或 word1 是 staging 地址表项）。
需要继续确认 HAL 在 frame 提交时如何把该表转换为最终 word1 数据（可能经过 `ISP_CMD_CQ_SW_PATCH` / CQ patch 流程）。

### 原始数据文件

- 全部 29 个缓冲区：`D:\evergo\android-camera-dump\cqall\cqall_*.bin`
- CAM_A 439 项解码表：`D:\evergo\android-camera-dump\cq_table_cam_a.txt`
- 单缓冲示例：`D:\evergo\android-camera-dump\cqmem.bin`

### 下一步

1. 继续用 `injmemcpy` 在**帧提交瞬间**（preview 稳定、多次采样）抓取同一 CQ 线程，观察 word1 从 `0xfbccf...` 变为真实寄存器值的时机；
2. 或直接注入调用 libcamdrv `dumpCQTable` 类函数输出解码结果；
3. 结合 `ISP_CMD_CQ_SW_PATCH` 语义确认 patch 流程，得到 mainline 需要模拟的初始化步骤。

## 16. 采样对比：活跃 CQ 环 vs 静态大表

对全部 29 个 CQ 缓冲区各采样 3 次（间隔 ~0.2s）后对比：

- **439 项大表（8 个，全部静态 diff=0）**：`w1 = 0xfbccf000 + offset` 形式的暂存值表；
- **41 项小表（8 个，每次采样变化 3-4 项）**：**活跃 CQ 环**，其 w1 是真实 IOVA（0xfe.../0xff...）。

活跃环结构（示例 s2_75e6bc0000.bin，可读且稳定）：

```
每 16 项一个 frame block:
  0: w0=28004580 (base=5 off=4580)      w1=ff295030   IOVA
  1: w0=280045ac (base=5 off=45ac)      w1=ff295034
  2: w0=280345cc (base=5 off=45cc cmd=3) w1=ff295038
  3: w0=280002bc (base=5 off=02bc)      w1=ff295068
  4: w0=28004494 (base=5 off=4494)      w1=ff295068   <- dummy 写
  5: w0=06000000 (END)
  6: w0=fd7d1000 (base=31)              w1=fb65f140
  7: w0=00000009                        w1=fd7d1000
  ...
 13: w0=ff295080 (base=31 cmd=729)      w1=00000002
 15: w0=e6bc0080 (base=28 cmd=6bc)      w1=00000075
```

- 每个 block 5 条 CAM 写 + END + 10 条“meta”描述（base 0x31/0x28，含 IOVA 与计数）；
- 与 mainline 反汇编得到的编码 `(off)|(base<<27)|(cmd<<16)` 一致；
- **活跃环里的 w1 是真实 IOVA**（ff295030 等），说明这些环是**已 patch 完成、直接给硬件用**的最终描述符表；
- 大表（439 项）可能是用于其它 camera/CQ 线程的"预生成模板"（暂存地址表），在预览时未被硬件使用。

### 下一步

1. 确定 base id 与 CAM/CAMSV 模块的对应关系（用活跃环中的 IOVA/计数与 live 寄存器比对）；
2. 用活跃环的完整 16 项 block 结构对照 mainline 的 CQ 环布局，补齐 mainline 缺失的 10 条 meta 描述符（很可能就是 CQ 引擎启动所必需的上下文/中断/TG 配置）。

## 17. mIspCQModuleInfo 表解析（描述符命令位来源）

`libcamdrv_isp.so` 符号表：

```
mIspCQModuleInfo              VA 0x2dbb0  size 8780 = 439*20
mIspCQModuleInfo_SpecialCQ    VA 0x2fdfc  size  500 =  25*20
mIspCQModuleInfo_STT_FBCInfo  VA 0x2fff0  size  160 =   8*20
```

20 字节结构（经 getCQModuleInfo 反汇编确认返回 [+8] 和 [+0xc]）：

```
+0: u32 index
+4: u32 (module<<16)|offset   ← 目标寄存器地址（相对 0x1a000000 相机 aperture）
+8: u32 offset
+12: u32 cmd                  ← 描述符命令位
+16: u32 (1)
```

示例（对应 CAM_A）：

```
idx 12: CTL_EN  module_off=00030004 off=0004 cmd=6
idx 13: CTL_SEL module_off=00030040 off=0040 cmd=2
idx 14: FMT_SEL module_off=00030048 off=0048 cmd=3
idx 15: CTL_MISC module_off=00030054 off=0054 cmd=1
...
module 5 对应 CAM_B (0x1a050000) 的一组寄存器，如 000545cc 等
```

**描述符 word0 编码**：`(cmd << 16) | (module << 27) | offset`（module 为 5-bit）。
这与活跃 CQ 环里观察到的 `0x280345cc`（module=5, cmd=3, off=0x45cc）完全一致。

已导出整表：`D:\evergo\android-camera-dump\cq_module_info.txt`（439 条 + SpecialCQ 25 条）。

### 待解释

静态 439 项表中的 `w1 = 0xfbccf000 + offset` 仍待确认：可能是 "值表 IOVA/指针"（CQ patch 前的占位），
也可能是另一种描述符形式。下一步可尝试用同样的注入方法读取 0xfbccf000 附近内容（若它是可映射的 IOVA/VA）。

## 18. 影子寄存器数组抓取尝试（本轮）

尝试用同样方法导出 `ispVirReg` 影子寄存器数组（f88 字段，0x8000 字节）：

```
shadow_1.bin 14944B（部分读取）
shadow_3.bin 32768B
```

分析结果：0x4/0x40/0x48 等寄存器偏移处为 0，非零字 1296 个但不符合 CAM 寄存器镜像 →
f88 字段所指对象并非 CAM 影子寄存器数组（或对象布局/偏移含义不同）。
抓取过程中 camerahalserver 崩溃一次，已自动重启并恢复预览正常（新 pid，CAM 中断继续增长）。

同时抓到 `patch_3.bin`（4096B）：前 7 个 dummy `0x18004494` + END，`w1=0xff30d000`（IOVA），
说明该线程是小尺寸 CAMSV/patch 环。

## 19. Android 侧逆向总结（已达到"足够"程度）

已完整掌握：

1. **描述符编码**：`word0 = (cmd<<16) | (module<<27) | offset`
   - offset = 寄存器偏移；module = 模块号（3=CAM_A, 5=CAM_B, …）
   - cmd 来自 `mIspCQModuleInfo` 20B 表（439 项，已导出）
2. **CQ 寄存器配置**：0x200=0x112、0x210=0x21、0x214 base、0x218 DESC_SIZE=0xDB8(439)
3. **CQ 环结构**：活跃环为 16 项 frame block（5 条 CAM 写 + END + 10 条 meta 描述，含 IOVA）
4. **HAL 内部结构**：ISP_DRV_CAM 对象含 ispVirReg/ispCQDesc 子对象；patch 表格式为 (reg编码, 值指针)
5. **物理限制**：dmabuf PFNMAP 只能用进程内 memcpy 注入导出（已实现工具链）
6. **三摄像头**：s5kjn1(主) / ov16a1q(前) / imx355(超广)，HAL CQ 线程均已覆盖

### 回到 mainline 需要的步骤

1. 用正确 cmd 位重建 439 项 CQ 描述符（此前 mainline replay 用的是 cmd=0 的模板，这是最可能失败原因）；
2. 刷 mainline boot + Alpine userdata 验证 CQ 是否执行（出现 CQ_DONE / PASS1_DON）；
3. 观察帧引擎是否启动，再调整 RAW/IMGO 通路。

> 注意：刷 mainline boot 会破坏当前 Android 用户数据（此前 Android userdata 会被覆盖），需用户确认后再进行。

## 20. 重建带 cmd 位的 CQ 环（离线产物）

用已导出的 `mIspCQModuleInfo` 表 + P1CB 寄存器镜像重建了完整 CQ 描述符环：

```
D:\evergo\android-camera-dump\cq_ring_full_rebuilt.bin   3512B（= 0xDB8，439 项）
D:\evergo\android-camera-dump\cq_ring_full_rebuilt.txt   427 条写描述符 + END
D:\evergo\android-camera-dump\cq_ring_cam_a_rebuilt.bin  仅 module=3 (CAM_A) 版本
```

- 每条 `w0 = (cmd<<16) | (module<<27) | offset`，`w1` 来自 P1CB 寄存器镜像的对应偏移值；
- 模块分布：module 3 (CAM_A) 377 项、module 9 56 项、module 0 3 项、module 5 2 项、module 7 1 项；
- 12 项偏移超出 0x8000 或未编程，已跳过；其余 427 项写入 + END + dummy 填充到 439 项。

### 与旧测试的差异

之前 mainline 测试工具 `camcq*.c` 只写 **1 条**描述符（带 cmd 位）+ dummy 填充，且把 END 放在最后一槽；
重建环是 **427 条完整描述符序列**（含 END 在正确位置）。下一步应在 mainline 上重放完整环（不仅单条），
并同时加载与 CAM_A 一致的寄存器上下文（CQ_EN/CTL/clock）。

## 21. mainline CQ 回放工具（已构建，待刷机验证）

新增 `camring`（静态 aarch64，源码 `camring.c`）：

```
用法: camring [ringfile] [--zero-cmd] [--single]
```

流程：
1. 从 `/sys/kernel/debug/camraw/size` 读第二行获取 CQ 缓冲 IOVA；
2. 把 439 项 CQ 环（`cqring.bin`，3512 字节）写入 `/sys/kernel/debug/camraw/frame`；
3. 通过 `/dev/mem` 映射 CAM_A，软复位后设置：
   `0x200=0x112`、`0x204=0xffffffff`、`0x210=0x21`、`0x214=IOVA`、`0x218=0xDB8`；
4. 触发 `0x74`，等待 200ms，读取 `INT6/INT7/ST/CQ_EN/THR0/SIZE` 状态。

对比模式：
- `--zero-cmd`：清零 cmd 位（复现旧测试）；
- `--single`：只保留第一条描述符（复现旧测试）。

产物位置：`D:\evergo\tools\camring`、`camring.c`、`cqring.bin`。

### 下一步（需刷 mainline）

1. 刷 cam 系列 boot + Alpine userdata；
2. 把 `camring` 和 `cqring.bin` 推到设备 `/root/`；
3. 运行 `./camring /root/cqring.bin`，观察是否出现 `INT6`/`PASS1_DON`；
4. 若 CQ 执行，再继续 RAW/IMGO 配置。
