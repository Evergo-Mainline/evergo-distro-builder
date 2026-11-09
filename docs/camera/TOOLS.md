# 🧰 相机调试工具与测试流程

> 所有工具都在设备上以 `root` 运行（SSH `root@169.254.42.1`，密码 `evergo`）。
> 工具源码/二进制来源见 [MATERIALS.md](MATERIALS.md)。

## 1. 固件构建与刷写

### 构建（WSL Ubuntu 24.04）

```bash
cd /root/reposnap
KERNEL_SRC=/root/memtree RAM_GIB=8 PANEL=k16a_36_02_0a_vdo,lcm \
  KERNEL_ONLY=1 INITRAMFS_ROOTFS=/root/alpine-e2fs ./build.sh
# 产物：/root/reposnap/out/boot.img
```

### 刷写（Windows，ESP 快启接口）

```powershell
# ESP API：POST http://192.168.111.188/api  action=fastboot / power / wait ...
# fastboot: tools\platform-tools\fastboot.exe
fastboot flash boot_a boot.img
fastboot flash boot_b boot.img
fastboot reboot
```

> 设备失联时：ESP 电源循环 + 重刷已知可用镜像（见 `docs/TROUBLESHOOTING.md`）。

## 2. 基础测试流程（相机）

```bash
# 1) 启动 sensor 与 SENINF（debugfs 直控，绕过 media 管线）
echo 1 > /sys/kernel/debug/s5kjn1/stream
echo 1 > /sys/kernel/debug/mtk-seninf/stream

# 2) 用 V4L2 采集 CAMSV3（/dev/video0）
/root/camcap /dev/video0

# 3) 查看 CAM_A 中断/状态
cat /sys/kernel/debug/camraw/irqstats
```

## 3. camraw debugfs（CAM_A 调试核心）

| 文件 | 权限 | 说明 |
|:---|:---|:---|
| `irqstats` | ro | IRQ 计数、最近 RAW_INT/INT6/INT7、TG、IMGO_ERR、CQ 计数 |
| `size` | ro | camraw 25MB DMA 缓冲（第一行大小，第二行 IOVA） |
| `frame` | rw | 直接读写该 DMA 缓冲（用于写入 CQ 环/读取帧数据） |
| `physbuf` | rw | 64KB 物理连续调试缓冲（`__get_free_pages(GFP_DMA32)`） |
| `physaddr` | ro | 上述 physbuf 的物理地址 |
| `cqsched` | wo | 写 1：编程 CQ 寄存器并在 VS 中断里写 START（VS 同步启动测试） |
| `gce` | wo | 写 "<subsys> <cq>"：经 GCE 提交写 CAM 寄存器（subsys 映射不可用，仅调试） |
| `gcepa` | wo | 写 "<addr> <val>"：经 GCE PA 路径写物理地址（同上，不可达 CAM） |

## 4. 命令行寄存器工具

| 工具 | 用法 | 说明 |
|:---|:---|:---|
| `campeek` | `campeek 0x1a030004 [count] [stride]` | 通过 /dev/mem mmap 读物理寄存器 |
| `campoke` | `campoke 0x1a030150 0x5a5a5a5a` | 写物理寄存器 |
| `camdump` | `camdump 0x1a004000 0x2800 > out.bin` | 整段 dump 到二进制文件（用于与 Android live 差分） |
| `camgraph` | `camgraph /dev/media0` | 打印 media controller 拓扑/链接 |
| `camcap` | `camcap /dev/video0` | 最小 V4L2 MMAP 采集（申请 4 个 25MB 缓冲） |
| `camdirect7` | `camdirect7` | 直接写 CAM_A P1 预览寄存器镜像 + TG 启动（调试用） |

## 5. Android 抓取（离线资料，见 MATERIALS.md）

在原厂 Android 上曾用 `/dev/mem`、`/dev/camera-isp` mmap、`/proc/kcore`、ION/m4u debugfs 等采到：
CAMSYS_MAIN / CAM_A / CAM_A_INNER / SENINF / CAMSV3 / D-PHY ANA 运行态寄存器、CQ 环内容、m4u 映射、dmesg/logcat。

## 6. 安全注意事项（重要）

- **不要**在 streaming 运行中并发通过 debugfs 读 sensor 寄存器（曾触发内核 oops）
- **不要**在运行中零散写 CAM_A/CAMSV 的控制寄存器（多次导致设备挂起，需重刷恢复）
- 优先把配置做成**驱动内启动序列**，再一次性启动；避免"运行中 poke 状态机"
