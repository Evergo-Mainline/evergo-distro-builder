# 📷 相机 Bring-up 记录（MT6833 / evergo）

> 目标：在 mainline Linux 上打通 内置主摄（S5KJN1）→ CSI-2/SENINF → CAMSYS → V4L2/libcamera，取得第一帧。
> 基线：内核 `865d67f3911c0a52b7c02aa567699b4e2b6ffc13`（builder `dce79833`，屏幕/系统正常）；相机工作在内核/构建仓库的 `camera-test` 分支进行。

## 1. 硬件链路

```
S5KJN1 (4-lane MIPI CSI-2, 24MHz MCLK)
  → MIPI D-PHY (0x11c80000)
  → SENINF / CSI2 receiver（本机 port0 → SENINF 实例 2：0x1a006000）
  → SENINF mux → CAM_MUX（cammux5）
  → CAM_A (0x1a030000, P1/ISP RAW) 或 CAMSV3 (0x1a092000)
  → IOMMU/SMI → DMA 写入内存（IMGO）
```

### 地址速查

| 模块 | 基址 | 说明 |
|:---|:---|:---|
| CAMSYS_MAIN | `0x1a000000` | 时钟门控/复位/CAMSV unhalt |
| SENINF | `0x1a004000`（窗口 0xa000） | 全局：0x0000/0x0010 顶层路由；0x0400 CAM_MUX；0x0d00 mux0；0x1d00 mux1 |
| SENINF 实例 2（port0 CSI2） | `0x1a006000` | CSI2 寄存器块：`0x1a006a00` 起 |
| CAM_A | `0x1a030000` | CTL/CQ/TG/IMGO；CQ 0x200/0x210/0x214/0x218；TG 0x1f00+ |
| CAM_A_INNER | `0x1a038000` | 独立 CQ 线程组 |
| CAMSV3 | `0x1a092000` | MODULE_EN 0x10、INT_EN 0x18、TG 0x130、IMG0 0x420 |
| MIPI D-PHY | `0x11c80000` | ANA 表 + `0x1000` bank |
| GCE | `0x10228000` | 用于 MM（该 SoC 的 DTB 中 CAMSYS 映射值陈旧，见 §4） |

## 2. 软件现状（camera-test 分支）

### 内核提交

| 提交 | 内容 |
|:---|:---|
| `f3396b5f2` | CAM_A/CAMSV bring-up 调试基础设施（camraw 驱动、SENINF debugfs、sensor 流日志、DT iommu/mbox、PHY golden） |
| `05265ef3e` | vendor S5KJN1 完整寄存器表 + SENINF cammux5 路由修正（已验证 streaming） |

### 驱动与 DT

- **seninf（ISP3.0）**：`CONFIG_MTK_SENINF=y`；MT6833 走 vendor golden（PHY/CSI2/mux/TOP mux），debugfs 提供流控制与寄存器 dump
- **CAMSV30**：`CONFIG_MTK_CAMSV30=y`；MT6833 专属 TG/DMA 序列（vendor 镜像语义）
- **camraw**：`CONFIG_MTK_CAMRAW=y`；CAM_A 寄存器映射、IRQ219、25MB DMA 缓冲、physbuf、CQ/GCE 调试接口
- **s5kjn1**：4-lane、24MHz MCLK 校验；`init_array_setting` 与 `4080x3072@30fps` 表替换为 vendor `s5kjn1sunny` 全量（1321 + 277 条写，原上游表只有 206 条）
- **DT（mt6833.dtsi）**：`camraw` 增加 `iommus = <&iommu 0x200>, <&iommu 0x202>` 与 `mboxes = <&gce 12 8 0>`；`seninf` port0→s5kjn1、port4→camsv3

### media 拓扑（全部 link 已使能）

```
s5kjn1:0 -> seninf:0        seninf:4 -> camsv3:0      camsv3:1 -> video stream:0
```

## 3. 已验证的事实（真机日志/寄存器证据）

1. **sensor 探测与流**：`s5kjn1 6-0010: streaming on: ctrl_mode=0001 chip=38e1`
2. **sensor 真正输出像素**（换用 vendor 完整寄存器表之后）：
   - CSI2 实例 2 的活动计数器 `0x1a006adc` 在 streaming 时持续递增、停止流后冻结
   - `0x1a006ad4 = 0x13ec002b` → 行长 5100 字节、MIPI DT `0x2b`（RAW10）
3. **CAM_A TG 正在运行**：`irq_count` 持续增长（720+ 到 3700+），中断状态 `raw=0x1/0x2`（VS/SOF），约 60 fps；TG 计数前进
4. **CAMSV3 能收到同步**：`cammux5 ← mux0` 时 CAMSV 中断约 22-30/s（VS/SOF）
5. **SENINF 静态配置与 Android 基本一致**：全窗口 288 项逐项差分仅 36 处差异（cammux5 路由、mux1 块、MIPI RX lane 值等），补齐后行为不变
6. **D-PHY ANA 与 Android 完全一致**：271 项仅 2 处差异 + 2 项未覆盖

## 4. 调试过程中的关键发现

- **vendor sensor 寄存器表是分水岭**：上游精简表（206 条）下 sensor 只产生同步；换成 vendor 表后 CSI2 才收到真实 RAW10 像素包
- **CAMSV3 输入是 cammux5**：逐个 cammux 扫描定位；驱动已在 `mtk_seninf_mt6833_start()` 中把 cammux5 路由到实际启动的 mux
- **GCE 路线不通**：从原厂 boot 镜像提取的 DTB 中 CAMSYS GCE 子系统的 base 是陈旧值（`0x18080000` 等，实际 CAM 在 `0x1a0x`）；GCE subsys 与 PA 两条写路径都无法写 CAM 寄存器（客户端本身工作正常：线程执行、无报错）
- **CAMSYS 时钟/复位正常**：`CG_CON=0`（全部时钟开）、SW_RST=0、CAM_A SW_CTL 空闲
- **CQ 引擎不可用**：CPU 直写、VS 同步写、6 种寄存器顺序、IOVA/物理环地址、GCE 均无效；`INT6=0`、描述符不执行、IMGO 不写
- **在线写 CAM/CAMSV 控制寄存器有挂起风险**：多次出现设备失联，均通过 ESP 电源循环 + 重刷已知镜像恢复

## 5. 阻塞点（当前）

1. **CSI2 → SENINF mux 像素数据未转发**：CSI2 收包正常，但 CAM_A/CAMSV 的 TG 只有 VS/SOF，无 PASS1_DONE、无 DMA 写入
2. **CAM_A CQ 引擎不执行**：与数据无关（有真实数据时同样不执行）

### 已排除项

- CQ：寄存器值/顺序、环内容（含 427 描述符重建环与安全化环）、IOVA/物理地址、VS 同步启动、时钟门控、SW/HW 复位、GCE（subsys 与 PA）
- SENINF/CAMSV/D-PHY：全量寄存器与 Android live 差分、mux 路由、CAM_MUX OPT/CHK、MIPI RX lane 配置、CAMSV 全寄存器镜像、格式/打包矩阵（8 组）
- CSI2：DT/VC 参数扫描（仅 0xa04=0x1f0037 会打断路径）
- sensor：4-lane 模式、24MHz MCLK、vendor 表已对齐（流/包均正常）

## 6. 下一步选项

| 选项 | 内容 | 需要 |
|:---|:---|:---|
| A（最快） | 短暂刷回 Android，运行时抓取 SENINF/CSI2 像素通路与 CQ 启动的寄存器写时序 | 用户允许刷回 Android（抓完立即恢复 mainline） |
| B | 使用 MT6833 SENINF/CQ 寄存器手册或 vendor 预编译模块/源码 | 资料 |
| C | 降低目标：先做 CAMSV 小数据通路验证（若只要证明通路） | 需求放宽 |

## 7. 提交记录

```
kernel  camera-test:
  f3396b5f2  camera-test: CAM_A/CAMSV bring-up instrumentation (verified on device)
  05265ef3e  camera-test: vendor S5KJN1 register tables + CAMSV mux routing (verified streaming)
builder camera-test:
  a008d15  pin f3396b5f2
  45f89e6  pin 05265ef3e
```

## 8. 已知限制/注意事项

- 设备有 **启动偶发变慢**（约 90s）与 **启动挂起** 的情况，必要时 ESP 重刷恢复
- 显示偶发 `flip_done timed out`（flip=1~3），重启后恢复 flip=0，不影响相机调试
- 不在 streaming 中通过 debugfs 并发读 sensor 寄存器（曾触发内核 oops）
