# 📷 相机寄存器与数据格式速查（MT6833 / evergo）

> 数值来自 Android 原厂运行态抓取（见 [MATERIALS.md](MATERIALS.md)）与 mainline 真机实测。
> 写作目的：后续继续 bring-up 时直接查表，不用重新 dump。

## 1. 地址表

| 模块 | 基址 | 关键偏移 |
|:---|:---|:---|
| CAMSYS_MAIN | `0x1a000000` | `0x0` CG_CON / `0x4` CG_SET / `0x8` CG_CLR / `0xc` SW_RST / `0x10` GCTL |
| SENINF 全局 | `0x1a004000` | `0x0` TOP_CTRL / `0x8` TOP_MUX / `0x10` CAM_MUX_CTRL / `0x14` / `0x400` CAM_MUX / `0x410`/`0x418` CAM_MUX check |
| SENINF mux0 | `0x1a004000 + 0xd00` | `0xd00` ctrl / `0xd04` ctrl1 / `0xd10` / `0xd20`/`0xd30` / `0xd28` |
| SENINF mux1 | `0x1a004000 + 0x1d00` | `0x1d04=0x1f0209`、`0x1d20=0xbf0027c` 等（vendor 全配置） |
| SENINF 实例 2 | `0x1a006000` | port0 的 CSI2 接收器：`0x1a006a00` 起 |
| CAM_A | `0x1a030000` | 见 §2 |
| CAM_A_INNER | `0x1a038000` | 独立 CQ 线程组（CQ_THR1/9..24） |
| CAMSV3 | `0x1a092000` | 见 §4 |
| MIPI D-PHY | `0x11c80000` | ANA 表（`0x0`/`0x1000` 两个 bank） |
| GCE | `0x10228000` | 该 SoC DTB 的 CAMSYS 子系统映射是陈旧值，见 §5 |

## 2. CAM_A（P1/RAW）

### 控制/中断

| 偏移 | 名称 | 说明 |
|:---|:---|:---|
| `0x004` | CTL_EN | vendor 预览值 `0x071effef` |
| `0x008` | CTL_EN2 | `0x3a3ffd88`（vendor live） |
| `0x00c` | CTL_EN3 | `0x01ebb01e`（vendor live） |
| `0x014` | DMA_EN | IMGO_R1_EN = bit0；vendor live `0x000bf785` |
| `0x040` | CTL_SEL | `0x06072d90` |
| `0x048` | FMT_SEL | `0x01081091` |
| `0x054` | CTL_MISC | P1 镜像 `0x00120074`；vendor live `0x00360075` |
| `0x058` | CD_DONE_SEL | |
| `0x074` | CTL_START | 写 1 启动对应 CQ 线程 |
| `0x078` | CTL_START_ST | |
| `0x07c` | CTL_SW_CTL | `0x0` idle / `0x4` reset 触发 |
| `0x100` | RAW_INT_EN | vendor `0x00877c73` |
| `0x104`/`0x108` | RAW_INT_STATUS/X | bit0=VS、bit1=SOF、bit14=HW_PASS1_DON |
| `0x150`/`0x154`/`0x158` | INT6_EN/STATUS/STATUSX | CQ DONE 相关 |

### CQ

| 偏移 | 名称 | vendor 运行值 |
|:---|:---|:---|
| `0x200` | CQ_EN | `0x112`（bit0 EN、bit4 MODE、bit8 DONE_SEL） |
| `0x210` | CQ_THR0_CTL | `0x21` |
| `0x214` | CQ_THR0_BASEADDR | `0xf8662000`（低 32 位；完整 IOVA 带 0x2 前缀域） |
| `0x218` | CQ_THR0_DESC_SIZE | `0xdb8`（3512 = 439×8） |

### TG / IMGO

| 偏移 | 名称 | 说明 |
|:---|:---|:---|
| `0x1f00` | TG_SEN_MODE | `0x00016001`（P1 镜像） |
| `0x1f04` | TG_VF_CON | 写 1 启动 |
| `0x1f08`/`0x1f0c` | TG_GRAB_PXL/LIN | |
| `0x1f3c` | TG_INTER_ST | 不是简单帧计数 |
| `0x4820` | IMGO_BASE_ADDR | P1 镜像中为 0（由 CQ 每帧 patch） |
| `0x482c`/`0x4830`/`0x4834` | IMGO_XSIZE/YSIZE/STRIDE | P1 预览 `0x13eb`/…/`0x13f0` |
| `0x4068` | IMGO_ERR | |
| `0x40ac` | DMA_CQ_COUNTER | |

### CQ 描述符编码（8 字节/条）

```
word0 = (cmd << 16) | (module << 27) | offset
word1 = value（寄存器写）或 IOVA（DMA 类描述符）
module: 3 = CAM_A
END   = 0x06000000
```

- vendor 环共 439 条（0xdb8 字节），前几条是 CQ 上下文/CTL 寄存器写
- 环里有 15 条携带 vendor IOVA 的描述符；在 mainline 中直接重放会导致 IOMMU 访问异常（曾挂起设备）
- 环内容最终写入的寄存器值与 P1 预览镜像完全一致（即 P1 镜像就是"环执行后的寄存器状态"）

## 3. SENINF / CSI2

### 顶层

```
0x008 = 0x32100000   TOP mux 路由（vendor）
0x010 = 0x03020202   CAM_MUX_CTRL
0x014 = 0x00000004
0x400..0x40c = CAM_MUX（每 cammux 4bit，vendor: 0x03020100/0x07060104/0x0b0a0908/0x0f0e0d0c）
  cammux5 默认 ← mux1；mainline 驱动改为 ← 实际启动的 mux（mux0）
```

### mux0 块（0xd00，vendor golden）

```
0xd00 = 0x00000001
0xd04 = 0x001f0208
0xd10 = 0x80000000
0xd20 = 0x0c000ff0
0xd28 = 0x01fd0000
0xd30 = 0x0c000ff0
```

### CSI2 实例 2（port0，`0x1a006a00`）

| 偏移 | vendor 值 | 说明 |
|:---|:---|:---|
| `0xa00` | `0x0000000f` | |
| `0xa04` | `0x001f0036` | 对数据通路敏感（`0x1f0037` 会断流） |
| `0xa10` | `0x00000106` | INT_EN |
| `0xa14` | — | INT_STATUS（运行中为 0） |
| `0xa20` | `0x002b0011` | DT/VC 相关 |
| `0xa24` | `0x00300111` | |
| `0xa60`-`0xa6c` | `0x100/0x200/0x400/0x800` | 4 lane 使能位图 |
| `0xad4` | `0x13ec002b` | 收到的包头：行长 5100、DT 0x2b(RAW10) |
| `0xadc` | 递增 | 接收活动计数（可用作"是否收包"判断） |

### MIPI RX lane 块（`0x8000/0x8800/0x9000/0x9800`，每块 4 个）

```
+0x100 = 0x0000000a
+0x244 = 0x00002105（仅低 16 位可写）
+0x300 = 0x000004f1
+0x444 = 0x00000001
```

## 4. CAMSV3

| 偏移 | 名称 | vendor 值/说明 |
|:---|:---|:---|
| `0x010` | MODULE_EN | `0x40000015`（mainline 驱动用 `0x20000015`） |
| `0x014` | FMT_SEL | `0x1` |
| `0x018` | INT_EN | `0x31871`；bit16/17 为使能会引发错误风暴 |
| `0x01c` | INT_STATUS | 位定义见下 |
| `0x040` | SW_CTL | reset 相关 |
| `0x04c` | PAK | vendor `6` |
| `0x130` | TG_SEN_MODE | `0x00012001` |
| `0x134` | TG_VF_CON | `0x1` |
| `0x138`/`0x13c` | GRAB_PXL/LIN | `0x0ff00000`/`0x0c000000`（4080×3072） |
| `0x140` | TG_PATH_CFG | `0x1000` |
| `0x16c` | TG_INTER_ST | 帧/行状态计数 |
| `0x420` | IMG0 base | vendor `0xf2400000`（IOVA） |
| `0x430`/`0x434`/`0x438` | XSIZE/YSIZE/STRIDE | vendor `0x1fdf`/`0xbff`/`0xb1fe0` |
| `0x43c`/`0x440`/`0x444` | CON/CON2/CON3 | `0x80000080`/`0x00400030`/`0x00200010` |

### CAMSV 中断位（vendor `cam_regs.h`）

```
bit0  SV_VS1          bit6  SV_SOF_INT
bit4  SV_TG_ERR       bit5  SV_TG_GBERR
bit11 SV_HW_PASS1_DON bit12 SV_SW_PASS1_DON
bit16 SV_IMGO_ERR     bit17 SV_IMGO_OVERRUN
```

运行中实际只观察到 `0x5`（VS1+TG_ST2）与 `0x41`（VS1+SOF），**从未出现 PASS1_DON**。

## 5. 厂商数据来源与 GCE 注意点

- **S5KJN1 vendor 寄存器表**：来自原厂内核 `s5kjn1sunny_mipi_raw.c`
  - init 表 1321 条、preview（4080x3072@30fps）277 条
  - 关键参数：pclk 560MHz、mipi_pixel_rate 504MHz、linelength 5894、framelength 3168、4 lane、settle_dc 40
- **CAMSYS GCE 子系统**：原厂 DTB 中 `camsys_base = <0x18080000 0x1b ...>` 等值**与实际 CAM 地址（0x1a0x）不符**，属于陈旧映射；因此 GCE 的 subsys/PA 两条路径都无法写 CAM 寄存器（客户端本身可正常提交包）
