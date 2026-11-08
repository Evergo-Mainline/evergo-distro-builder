# MT6833 SENINF / MIPI CSI-2 Bring-up Plan — Xiaomi Redmi Note 11T 5G (evergo)

Target: first RAW10 frame from the Samsung S5KJN1 (I2C 0x20, 4-lane, 700 MHz/lane) on
mainline Linux 7.3-rc4 (`/root/memtree`), using the Android 4.14 MT6833 vendor kernel
(`/root/android-kernel`) as the register reference.

Status of this document: analysis + plan only. Nothing was built, flashed or run, and no
file other than this one was created or modified.

Citation key:
- `[V]` verified in the vendor 4.14 tree (path:line).
- `[M]` verified in the mainline 7.3-rc4 tree (path:line).
- `[I]` inference / must be confirmed on hardware. Anything marked `[I]` is **not**
  a fact extracted from source.

---

## 0. Executive summary (the five findings that shape the whole plan)

1. **There is no "capture" code in the vendor seninf driver at all.** The MT6833 seninf
   driver is a *shim*: a char device that hands userspace two `mmap` windows (seninf
   register file at `0x1A004000`, MIPI-RX analog at `0x11C80000`), plus clock and IRQ
   management. `[V]` `imgsensor/src/common/v1_1/seninf.c:185-241` (mmap),
   `:279-290` (ioctl returning map addresses), `mt6853/seninf_mt6833/seninf_cfg.h:21-27`.
   The ioctl set (`kd_seninf.h:29-48`) contains **no** start/stop/stream ioctl — only
   `GET_REG_ADDR`, `SET_MCLK_PLL`, `GET_ISP_CLK`, `GET_CSI_CLK`, DFS and cam-mux switch.
   ⇒ CSI-2 lane/VC/DT programming was done by the **proprietary Android camera HAL**
   writing through those mappings (and/or via CMDQ, see finding 2). It is *not* in the
   vendor kernel source we have, so it cannot be copied — it has to be re-derived.

2. **Register writes for capture are CMDQ-driven from userspace.** The vendor CMDQ driver
   exports SENINF FIFO-full events (7 of them: `SENINF_0..6`) and `ISP_SENINF_CAM0_FULL` /
   `CAM1_2_3_FULL`. `[V]` `drivers/misc/mediatek/cmdq/v3/cmdq_event_common.c:362-401`.
   The kernel ISP driver only does clocks, IRQ and register *dumps*; it does not build the
   capture register sequence. `[V]` `cameraisp/src/mt6853/camera_isp.c:10005` reads
   `CAMSV_REG_IMGO_BASE_ADDR` but never writes it.

3. **The real start/stop primitive is one bit.** On both CAM (ISP) and CAMSV the frame
   pipe is enabled by `TG_SEN_MODE[0]` (CMOS_EN) followed by `TG_VF_CON[0]` (VFDATA_EN),
   and stopped by clearing `TG_VF_CON[0]` then polling `TG_INTER_ST[13:8]` for TG idle.
   `[V]` `camera_isp.c:7942-7945` (start), `:7756` + `:7770` (stop + idle poll), `:5631-5632`.
   This is the "function that starts/stops a capture" — and it exists for CAM/CAMSV,
   **not** for seninf.

4. **Mainline already has most of the clock infrastructure, but no camera driver and no
   CAM power domain.** Present: camsys-main gate driver incl. `cam_m_seninf` `[M]`
   `clk-mt6833-cam_m.c:30-47`, seninf muxes `seninf_sel/seninf1_sel/seninf2_sel` `[M]`
   `clk-mt6833-topckgen.c:1146-1161`, CAMTG muxes `[M]` `:1040-1062`, camsys clock nodes
   `[M]` `mt6833.dtsi:1344-1360`, and a generic MIPI CSI-0.5 D-PHY driver `[M]`
   `drivers/phy/mediatek/phy-mtk-mipi-csi-0-5.c`. Missing: the CAM MTCMOS domains `[M]`
   `drivers/pmdomain/mediatek/mt6833-pm-domains.h` has only CONN/MFG0-3/AUDIO, seninf
   DT nodes, CAMSV/CAM nodes, LARB/IOMMU (the IOMMU node is commented out `[M]`
   `mt6833.dtsi:1458-1471`), and any V4L2 camera driver.

5. **The board DTS is already at "M2".** Sensor node at `i2c2 0x20`, 24 MHz MCLK from
   `CLK_TOP_CAMTG_SEL` parented to `CLK_TOP_UNIVPLL_192M_D8`, reset GPIO54, vddio from
   MT6360 LDO2 — all present `[M]` `mt6833-xiaomi-evergo.dts:1838-1858`, with comments
   literally labelled "Camera M1"/"Camera M2" `[M]` `:1812-1836`. What is missing is
   everything downstream of the sensor: `port`/`endpoint`, the CSI receiver, and the DMA.

Net: this is not a "port the seninf driver" job (there is nothing to port), it is
"write a new CSI-2 receiver + CAMSV capture driver for MT6833, using vendor register
offsets as the map and a golden register dump as the missing bitfield dictionary".

---

## 1. Source inventory and the "weird path"

### 1.1 Where the MT6833 seninf code actually lives

The task's hint is correct, and there are **two** same-named sibling directories:

```
drivers/misc/mediatek/imgsensor/src/mt6853/seninf/           <- fallback, 4 files
drivers/misc/mediatek/imgsensor/src/mt6853/seninf_mt6833/    <- MT6833 variant, 4 files
drivers/misc/mediatek/imgsensor/src/mt6785/seninf/           <- mt6785, different base
```

`seninf_mt6833/` is a *sibling*, not a child, of `seninf/`. Contents:

| File | Lines | Role |
|---|---|---|
| `seninf_cfg.h` | 29 | bases, window sizes, `SENINF_IRQ`, `_CAM_MUX_SWITCH` `[V]`:14-27 |
| `seninf_impl.c` | 166 | cam-mux crossbar switch + CRC/ECC IRQ handler `[V]`:33-163 |
| `seninf_clk.h` | 175 | clock name table + MCLK freq table `[V]`:51-136 |
| `Makefile` | 14 | builds `seninf_impl.o` + `../common/v1_1/seninf_clk.o` `[V]`:13-14 |

Build selection is dynamic, and this matters:

```make
# imgsensor/src/mt6853/Makefile
22  ifeq ($(CONFIG_MACH_MT6833),y)
23  MTK_DERIVED_PLATFORM := mt6833
...
99  ifneq (,$(wildcard $(IMGSENSOR_DRIVER_PATH)/$(CONFIG_MTK_PLATFORM_SENSOR)/seninf_$(MTK_DERIVED_PLATFORM)))
100     subdir-ccflags-y += -I.../seninf_$(MTK_DERIVED_PLATFORM)/
101     obj-y += seninf_$(MTK_DERIVED_PLATFORM)/
102 else
103     obj-y += seninf/
104 endif
105 subdir-ccflags-y += -I.../seninf/
```

So on MT6833 the build **does** use `seninf_mt6833/`, and because `-I.../seninf_mt6833/`
is added before `-I.../seninf/` (lines 100 vs 105), `#include "seninf_cfg.h"` resolves to
the MT6833 variant with `_CAM_MUX_SWITCH` and `SENINF_IRQ` enabled. The two `seninf_cfg.h`
files differ exactly by line 18 `#define _CAM_MUX_SWITCH`
(`[V]` `seninf_mt6833/seninf_cfg.h:18` absent in `seninf/seninf_cfg.h`).
`seninf_irq.c` (in `seninf/`) is therefore *not* built on MT6833; `_seninf_irq()` from
`seninf_impl.c:153` is used instead.

`common/v1_1/seninf.c` itself is always built (`mt6853/Makefile:45`) and is the only
platform-independent part.

### 1.2 The three address windows (the single most important fact)

```c
/* mt6853/seninf_mt6833/seninf_cfg.h */
16  #define SENINF_IRQ
18  #define _CAM_MUX_SWITCH
19  #define SENINF_MAX_NUM 6
21  #define SENINF_MAP_BASE_REG  0x1A004000
22  #define SENINF_MAP_BASE_ANA  0x11C80000
23  #define SENINF_MAP_BASE_GPIO 0xFFFFFFFF
25  #define SENINF_MAP_LENGTH_REG  0xA000
26  #define SENINF_MAP_LENGTH_ANA  0x10000
27  #define SENINF_MAP_LENGTH_GPIO 0x00000
```

- **REG window**: `0x1A004000`, size `0xA000` ⇒ `0x1A004000..0x1A00DFFF`, i.e. six 4 KiB
  CSI-2 brick pages `0x1A004000 + 0x1000*n`, `n = 0..5`.
- **ANA window**: `0x11C80000`, size `0x10000` — the MIPI D-PHY / MIPI-RX analog block.
- **GPIO window**: disabled (`0xFFFFFFFF`, length 0). The vendor camera mux is done with
  Linux GPIO/pinctrl, not through a seninf GPIO window.

Cross-check with the vendor DTB `[V]` `/root/vendor_dtb.dts:3407-3445`:

```
seninf1@1a004000  reg = <0 0x1a004000 0 0x1000>
seninf2@1a005000  reg = <0 0x1a005000 0 0x1000>
seninf3@1a006000, seninf4@1a007000, seninf5@1a008000, seninf6@1a009000 (same stride)
seninf_top@1a004000  reg = <0 0x1a004000 0 0x1000>   <-- aliases seninf1 page
```

`seninf_top` and `seninf1` share `0x1A004000` because the global TOP/crossbar registers sit
at offsets `0x0000..0x00FF` of the *first* brick page, while brick n's CSI-2 registers start
at `+0x0200` in each page. That is exactly how the vendor dump code reads them
(`base[0] + 0x0010` for TOP, `base[0] + 0x0200` for SENINF1) — see §2.1.

For the record, `seninf_top` carries the clock list but **no** `interrupts` property
`[V]` `:3437-3445`, and `seninf_probe()` tolerates that (`if (irq > 0)` `[V]`
`seninf.c:470`). ⇒ there is no wired seninf IRQ in the vendor DTB; CRC/ECC errors were
polled/handled opportunistically. Mainline should not assume a seninf IRQ exists.

### 1.3 License headers of the files involved

All seninf files, `cam_regs.h` and `camera_isp.c` carry
"GNU General Public License version 2" headers `[V]` e.g. `cam_regs.h:1-13`,
`camera_isp.c:1-13`, `seninf.c:1-12`, and `seninf.c:606` is `MODULE_LICENSE("GPL v2")`.
See §9 for what that does and does not allow.

---

## 2. Register map of the MT6833 seninf (only what is visible in the vendor tree)

All offsets below are **actually read or written** by vendor code; the citation is the line
that performs the access. Bitfields are given only where the vendor code manipulates them
explicitly (mask/shift); elsewhere the access is a whole-word read or write and the bit
layout is *not* in this tree — that gap is called out in §2.6 and is the main reason for the
"golden dump" step in §10.

### 2.1 Global TOP block (in the `0x1A004000` page, offsets `0x0000..0x00FF`)

| Offset | Name (vendor) | Width | Evidence |
|---|---|---|---|
| `0x0010` | `SENINF_TOP_MUX_CTROL_0` | 32 | read in dump `[V]` `seninf.c:67` |
| `0x0014` | `SENINF_TOP_MUX_CTROL_1` | 32 | read in dump `[V]` `seninf.c:68` |

Semantics `[I]`: the top-level mux selects which CSI-2 brick feeds which CAM/CAMSV "TG"
input; `_0`/`_1` are two registers of packed 4-bit selects. The vendor code never writes
them, so field layout is `[I]`. This is the first thing to confirm with a golden dump.

### 2.2 CAM_MUX crossbar (same page, `0x0400..0x05CF`)

| Offset | Name | Fields visible |
|---|---|---|
| `0x0400` | `SENINF_CAM_MUX_CTRL_0` | 8 bits per mux, low nibble = source select (4 bits) |
| `0x0404` | `SENINF_CAM_MUX_CTRL_1` | same |
| `0x0408` | `SENINF_CAM_MUX_CTRL_2` | same |
| `0x040C` | `SENINF_CAM_MUX_CTRL_3` | same |
| `0x0420 + n*4` | `SENINF_CAM_MUXn_OPT` | whole word (swapped) |
| `0x04A0` | `SENINF_CAM_MUX_IRQ_EN` | whole word `[V]` `seninf.c:74` |
| `0x04A8` | `SENINF_CAM_MUX_IRQ_STATUS` | whole word `[V]` `seninf.c:75` |
| `0x0500 + n*0x10` | `SENINF_CAM_MUXn_CHK_CTL_0` | whole word |
| `0x0504 + n*0x10` | `SENINF_CAM_MUXn_CHK_CTL_1` | whole word |
| `0x0508 + n*0x10` | `SENINF_CAM_MUXn_CHK_RES` | whole word `[V]` `seninf.c:78-92` |

The `CAM_MUX_CTRL_*` encoding **is** visible, in the stagger switch code
`[V]` `seninf_impl.c:85-128`:

```c
85  #define SENINF_CAM_MUX_CTRL_0 0x0400      /* .. _3 0x040c => 4 muxes per reg */
90  cam_mux_ctrl_addr_offset = SENINF_CAM_MUX_CTRL_0 + 4 * (cam_tg / 4);
91  cam_mux_ctrl_digit_offset = cam_tg % 4;
109 cam_src = (cam_mux_ctrl_value >> (cam_mux_ctrl_digit_offset * 8)) & 0xF;
113 cam_mux_ctrl_value &= ~(0xF << (cam_mux_ctrl_digit_offset * 8));
115 cam_mux_ctrl_value |= (sv_src << (cam_mux_ctrl_digit_offset * 8));
```

⇒ 13 mux positions (`CAM_MUX_NUM 13`, `[V]` `seninf_impl.c:31`), 4 per register, 8-bit
field stride, 4-bit source id in the low nibble of each field. `cam_mux_ctrl_digit_offset`
is really a *byte* offset (`*8`), so field n occupies bits `[8n+3 : 8n]`.
`CHK_CTL/RES` stride is `0x10` per mux, and `CHK_RES` is documented by the vendor's own
dump as 4 separate results for mux 0..3 then 4..10 `[V]` `seninf.c:76-92`.

`CAM_TG_NUM 3` (`[V]` `seninf_impl.c:30`) — only CAM TG0/1/2 can be crossbar targets on
this SoC, consistent with `cam1/cam2/cam3` in the DTB.

### 2.3 Per-brick CSI-2 block (`base = 0x1A004000 + 0x1000*n`)

| Offset | Name (vendor) | Evidence |
|---|---|---|
| `0x0200` | `SENINFn_CTRL` | `[V]` `seninf.c:100` |
| `0x0210` | `SENINFn_CSI2_CTRL` | `[V]` `seninf.c:102` |
| `0x0A00` | `SENINFn_CSI2_EN` | `[V]` `seninf.c:108` |
| `0x0AC8` | `SENINFn_CSI2_IRQ_STATUS` | `[V]` `seninf.c:110`, cleared by writing all-ones `:113`; also `seninf_impl.c:156-161` |
| `0x0ADC` | `SENINFn_CSI2_PACKET_CNT_STATUS` | `[V]` `seninf.c:112` |
| `0x0D00` | `SENINFn_MUX_CTRL_0` | `[V]` `seninf.c:119` |
| `0x0D18` | `SENINFn_MUX_IRQ_STATUS` | `[V]` `seninf.c:121`, cleared all-ones `:124` |
| `0x0D30` | `SENINFn_MUX_SIZE` | `[V]` `seninf.c:123` |

Interpretation `[I]`, consistent with the naming and with MTK's seninf generation:
`0x0200` = brick control (enable, TG select), `0x0210` = CSI-2 protocol config (lane count,
continuous/non-continuous clock, VC/DT filtering enables), `0x0A00` = CSI-2 packet decoder
enable, `0x0AC8` = per-VC error/status (CRC, ECC, packet size), `0x0ADC` = received packet
counter per VC (the practical "am I receiving anything" probe), `0x0D00/0x0D18/0x0D30` = the
output formatter/DMA-facing mux (frame size, output format).

**These 8 registers are the entire kernel-visible CSI-2 surface of the vendor driver.**
No lane-count, no settle-time, no VC/DT, no data-type table, no bitfield macros exist
anywhere in `/root/android-kernel` — verified by searching for `CSI2_LANE`, `SETTLE`,
`LANE_NUM`, `csi2_ctrl` across `drivers/misc/mediatek/**` (no hits outside the names above).

### 2.4 MIPI D-PHY / MIPI-RX analog at `0x11C80000`

The vendor kernel only exposes the window; the *field* definitions come from mainline,
which is a much better source. The mainline `phy-mtk-mipi-csi-0-5` driver defines:

| Offset | Register | Key fields `[M]` (`phy-mtk-mipi-csi-0-5-rx-reg.h`) |
|---|---|---|
| `0x0000` | `MIPI_RX_ANA00_CSIXA` | `CPHY_EN` b0, `BG_LPF_EN` b2, `BG_CORE_EN` b3, `DPHY_L0_CKMODE_EN` b5 / `L0_CKSEL` b6, `L1_...` b8/b9, `L2_...` b11/b12 (`:17-27`) |
| `0x0018` | `MIPI_RX_ANA18_CSIXA` | EQ `IS`/`BW` fields, `GENMASK(7,4)` / `(23,22)` etc (`:29-41`) |
| `0x001C` | `MIPI_RX_ANA1C_CSIXA` | EQ (`:43`) |
| `0x0020` | `MIPI_RX_ANA20_CSI0A` | EQ L2 (`:44`) |
| `0x0024` | `MIPI_RX_ANA24_CSIXA` | `RESERVE` `GENMASK(31,24)` (`:46-47`) |
| `0x0040` | `MIPI_RX_ANA40_CSIXA` | `CPHY_FMCK_SEL`, `ASYNC_OPTION`, `CPHY_SPARE` (`:49-52`) |
| `0x0080` | `MIPI_RX_WRAPPER80_CSIXA` | `CSR_CSI_RST_MODE` `GENMASK(17,16)` (`:54-55`) |
| `0x00A8` | `MIPI_RX_ANAA8_CSIXA` | byte-clock invert bits b0/b1/b2 (`:57-60`) |
| `+0x1000` | `CSIXB_*` | second half of the PHY, same layout `[M]` `phy-mtk-mipi-csi-0-5.c:22` |

Lane mapping assumed by that driver `[M]` `phy-mtk-mipi-csi-0-5.c:89-99`:
`CSIXA_LNR0 -> D2`, `CSIXA_LNR1 -> D0`, `CSIXA_LNR2 -> C`, `CSIXB_LNR0 -> D1`,
`CSIXB_LNR1 -> D3`. **This is the single most likely source of a "no packets" failure on
MT6833**, because the board may route lanes differently (and there is a MIPI switch, §5.3).
The driver also hardcodes `4D1C` only `[M]` `:184-187`.

Note the window is 64 KiB (`0x11C80000..0x11C8FFFF`); `CSIXB` at `+0x1000` fits comfortably.
Neither vendor nor mainline DT describes `0x11C80000` as a node (checked both trees), so the
address comes only from the vendor driver constant — treat `[V]`-verified but
DT-undocumented.

### 2.5 CAMSV and CAM (ISP) blocks — the DMA/output side

CAMSV (needed for a self-contained RAW capture; no full ISP required):

| Offset | Name | Evidence |
|---|---|---|
| `0x0010` | `CAMSV_REG_MODULE_EN` | `[V]` `cam_regs.h:751` |
| `0x0018` | `CAMSV_REG_INT_EN` | `[V]` `:752` |
| `0x001C` | `CAMSV_REG_INT_STATUS` | `[V]` `:753` |
| `0x0020` | `CAMSV_REG_SW_CTL` | `[V]` `:754` |
| `0x0058` | `CAMSV_REG_DCIF_SET` | `[V]` `:755` |
| `0x0130` | `CAMSV_REG_TG_SEN_MODE` | `[V]` `:759` |
| `0x0134` | `CAMSV_REG_TG_VF_CON` | `[V]` `:760` |
| `0x0140` | `CAMSV_REG_TG_PATH_CFG` | `[V]` `:761` |
| `0x016C` | `CAMSV_REG_TG_INTER_ST` | `[V]` `:762` |
| `0x01A0` | `CAMSV_REG_TG_TIME_STAMP` | `[V]` `:763` |
| `0x0400` | `CAMSV_REG_DMA_SOF_RSTSTAT` | `[V]` `:764` |
| `0x0420` | `CAMSV_REG_IMGO_BASE_ADDR` | `[V]` `:758` |
| `0x0A00` | `CAMSV_REG_FBC_IMGO_CTL1` | `[V]` `:756` |
| `0x0A04` | `CAMSV_REG_FBC_IMGO_CTL2` | `[V]` `:757` |

CAM (ISP) equivalents, `base = 0x1A030000 + 0x20000*(n-1)`:

| Offset | Name | Evidence |
|---|---|---|
| `0x1F00` | `CAM_REG_TG_SEN_MODE` | `[V]` `cam_regs.h:451` |
| `0x1F04` | `CAM_REG_TG_VF_CON` | `[V]` `:454` |
| `0x1F10` | `CAM_REG_TG_PATH_CFG` | `[V]` `:452` |
| `0x1F3C` | `CAM_REG_TG_INTER_ST` | `[V]` `:455` |
| `0x1F68` | `CAM_REG_TG_DCIF_CTL` | `[V]` `:453` |
| `0x1F74` | `CAM_REG_TG_SUB_PERIOD` | `[V]` `:456` |
| `0x40AC` | `CAM_REG_DMA_CQ_COUNTER` | `[V]` `:461` |
| `0x40B4` | `CAM_REG_DMA_DEBUG_SEL` | `[V]` `:691` |
| `0x40BC` | `CAM_REG_DMA_FRAME_HEADER_EN1` | `[V]` `:462` |
| `0x4420`-ish family | IMGO/AAO/AAHO/FLKO base addrs (see `:700-712`) | `[V]` |

Bitfields that are explicitly visible (useful, but small):

- `TG_VF_CON[0]` = `VFDATA_EN` — set to start, clear to stop
  `[V]` `camera_isp.c:7944-7945` (start), `:7756` (stop), `:5631-5632`, `:10583-10584`.
- `TG_SEN_MODE[0]` = `CMOS_EN` — set immediately before `VFDATA_EN`, cleared on suspend
  `[V]` `camera_isp.c:7942-7943`, `:7820-7821`.
- `TG_INTER_ST[13:8]` = TG idle state (polled until 0 after stop)
  `[V]` `camera_isp.c:7770` (`& 0x00003F00`).
- `FBC_CTRL_1` (`CAM 0x1A030E00` / CAMSV `0x0A00`): `FBC_NUM[7:0]`, `FBC_RESET[8]`,
  `FBC_DB_EN[9]`, `FBC_MODE[11]`, `LOCK_EN[12]`, `DROP_TIMING[13]`, `FBC_SUB_EN[14]`,
  `FBC_EN[15]`, `VALID_NUM[23:16]`, `SUB_RATIO[31:24]` — full bitfield union
  `[V]` `cam_regs.h:770-787`. For a first RAW10 frame, keep `FBC_EN=0`.
- `FBC_CTRL_2`: `RCNT[7:0]`, `WCNT[15:8]`, `FBC_CNT[24:16]`, `DROP_CNT[31:25]`
  `[V]` `cam_regs.h:789-797`.

### 2.6 What is *not* in the vendor tree (must be recovered)

| Needed | Status |
|---|---|
| CSI-2 lane count field | **absent** |
| D-PHY settle / T-hs-settle timing | **absent** |
| Virtual channel + data type config | **absent** |
| `SENINFn_CTRL` / `CSI2_CTRL` / `CSI2_EN` bit layout | **absent** (whole-word only) |
| `TOP_MUX_CTROL_0/1` select encoding | **absent** |
| MIPI switch (GPIO43) polarity vs port | partially: sequence only, see §5.3 |

⇒ The plan must include an *empirical* step to obtain these (golden register dump from a
running stock device, or vendor HAL register tables if they can be extracted from the ROM).
This is not optional: without it, lane count and settle time are guesswork.

---

## 3. Clock and power requirements (vendor ↔ mainline mapping)

### 3.1 Vendor model

`seninf_clk_init()` grabs 17 clocks by name `[V]` `seninf_clk.c:115-140` from the
`seninf_top` DT node, names at `[V]` `seninf_clk.h:117-136`:

```
SYS group  : SCP_SYS_CAM, CAMSYS_SENINF_CGPDN, TOP_MUX_SENINF, TOP_MUX_SENINF1, TOP_MUX_SENINF2
TG group   : TOP_MUX_CAMTG, TOP_MUX_CAMTG2..5
FREQ group : TOP_UNIVP_192M_D32(6M), _D16(12M), TOP_F26M_CK_D2(13M),
             TOP_UNIVP_192M_D8(24M), TOP_CLK26M(26M), TOP_UNIVP_192M_D4(48M),
             TOP_UNIVPLL_D6_D8(52M)
```

- `seninf_clk_open()` enables the 5 SYS clocks on first `open()` and takes a wakeup source
  `[V]` `seninf_clk.c:218-238`; `seninf_clk_release()` unwinds all counts `[V]` `:240-259`.
- `seninf_clk_set()` implements MCLK as **`clk_set_parent(TOP_MUX_CAMTG[n], <fixed-rate
  parent>)`**, after enabling both `[V]` `:168-191`. For 24 MHz the parent is
  `TOP_UNIVP_192M_D8` `[V]` `seninf_clk.h:129-133`.
- MCLK range 6..52 MHz `[V]` `seninf_clk.h:96-104`; TG index 0..4 `[V]` `:86-94`.

### 3.2 Mainline equivalents (most already exist)

| Vendor clock | Mainline provider | Status |
|---|---|---|
| `CAMSYS_SENINF_CGPDN` | `cam_m_seninf`, gate bit 8 of camsys_main `[M]` `clk-mt6833-cam_m.c:36` | **present** |
| `CAMSYS_CAM_CGPDN` | `cam_m_cam` bit 6 `[M]` `:34` | present |
| `CAMSYS_CAMTG_CGPDN` | `cam_m_camtg` bit 7 `[M]` `:35` | present |
| `CAMSYS_CAMSV0/1/2_CGPDN` | `cam_m_camsv1/2/3` bits 10/11/12 `[M]` `:37-39` | present |
| `CAMSYS_LARB13/14` | `cam_m_larb13/14` bits 0/2 `[M]` `:31-32` | present (gate only; no LARB driver) |
| `TOP_MUX_SENINF{,1,2}` | `seninf_sel/seninf1_sel/seninf2_sel` + `fseninf{,1,2}_ck` `[M]` `clk-mt6833-topckgen.c:288-290,1146-1161` | **present** |
| `TOP_MUX_CAMTG{,2..5}` | `camtg_sel..camtg5_sel` `[M]` `:1040-1062` | present |
| `TOP_UNIVP_192M_D8` | `CLK_TOP_UNIVPLL_192M_D8` id 23 `[M]` `mediatek,mt6833-clk.h:56` | present |
| `SCP_SYS_CAM` | MTCMOS CAM power domain | **missing** `[M]` `mt6833-pm-domains.h` has no CAM entry |

The mainline `camsys_main` clock-controller node exists and the gate driver binds to it
`[M]` `mt6833.dtsi:1344-1348` + `clk-mt6833-cam_m.c:54-57`, so
`<&camsys_main CLK_CAM_M_SENINF>` is usable from a new seninf node today.

### 3.3 Board power topology (from the vendor camera DTSI)

Cam0 = S5KJN1 `[V]` `cust_evergo_camera.dtsi:318`:

| Rail | Supply (vendor) | Line |
|---|---|---|
| AVDD | `wl2866d_avdd1` | `:299` |
| DVDD | `wl2866d_dvdd1` | `:298` |
| DOVDD/VCAMIO | `mt_pmic_vtp_ldo_reg` | `:300` |
| MCLK | GPIO49 / `CMMCLK0`, 2/4/6/8 mA steps | `:88-117` |
| RESET | GPIO54 | `:15-28` |
| EEPROM | `i2c2 0x50` | `:328-332` |
| Camera PMIC | WL2866D on `i2c6`, addr `0x28` | `:355-357`, `wl2866d.dtsi:1-4` |

Mainline reality: the board DTS keeps WL2866D enabled with a **GPIO hog** and has no
WL2866D driver at all, and wires `vddio-supply = <&mt6360_ldo2>` `[M]`
`mt6833-xiaomi-evergo.dts:1812-1816, 1852`. `vdda`/`vddd` are **not** described, and the
mainline `s5kjn1` driver treats all four rails as optional
`[V]`-equivalent `[M]` `s5kjn1.c:97-100,1208-1231`. So today the sensor probes and streams
with AVDD/DVDD unmanaged — fine for I2C/CCI, **not** fine for a real MIPI link.

---

## 4. Minimal sequence to capture ONE RAW10 frame from S5KJN1

Numbers: 4080x3072 @30 fps, HTS 4352, VTS 4288 `[M]` `s5kjn1.c:719-728`, 4 data lanes,
700 MHz link, 24 MHz MCLK `[M]` `s5kjn1.c:17-19`. Raw bit rate = 4352*4288*30*10 ≈ 5.6 Gbps,
i.e. 1400 Mbps/lane — matches the 700 MHz DDR link exactly, so 4 lanes are mandatory
(no 2-lane fallback at full resolution).

Steps 1-3 are board/sensor, 4-6 are PHY, 7-9 are seninf, 10-12 are CAMSV, 13-14 are
start/stop. Items marked `[I]` need the golden dump from §10.

### Step 1 — CAM subsystem power (currently blocking)

```
MT6833_POWER_DOMAIN_CAM  (id 13)  -> must exist in pm-domains + DT
MT6833_POWER_DOMAIN_CAM_RAWA (14), CAM_RAWB (15)  -> only if RAW path used
```
Mainline: absent `[M]` `mt6833-pm-domains.h:61-141`. Without CAM MTCMOS on, the
`camsys_main` gate registers at `0x1A000000` are in a powered-off domain and every
seninf/camsv access will hang the bus (not fail cleanly). **This is milestone M0 and it is
a hard prerequisite.**

### Step 2 — Clocks

```
fseninf_ck      = seninf_sel      (parent: e.g. univpll_d3 / 288 MHz class)  [I]
cam_m_seninf    gate on           (camsys_main + 0x4 bit 8)
cam_m_camsvN    gate on           (bit 10/11/12)     if CAMSV path
cam_m_larb13/14 gate on           (bits 0/2)         if DMA via LARB
```
Vendor reference: `seninf_clk.c:218-238` (sys clocks), `seninf_clk.h:54-57`.

### Step 3 — Sensor power, MCLK 24 MHz, probe, stream-on

```
WL2866D: AVDD 2.8 V, DVDD 1.05-1.2 V   (needs a driver or fixed regulators)
VCAMIO 1.8 V (mt6360_ldo2, already modelled)
MCLK: clk_set_parent(camtg_sel, univpll_192m_d8) -> 24 MHz   [M] board dts:1853-1855
RESET: GPIO54 low 1 ms, high 1 ms, then I2C probe at 0x20
CCI: chip id 0x38e1 @0x0000   [M] s5kjn1.c:22-23
mode: 4080x3072, HTS 4352, VTS 4288  [M] s5kjn1.c:719-728
stream on: 0x0100 = 0x01      [M] s5kjn1.c:25-26
```
Vendor power-up order for the pins is in `imgsensor_cfg_table.c:29-42`
(MCLK → AVDD → DOVDD → DVDD → PDN → RST) — note **AVDD before DOVDD before DVDD**, and
that the vendor drives AVDD/DVDD from *GPIO-controlled* WL2866D pins, not regulators.

### Step 4 — MIPI switch (board specific, easy to forget)

The evergo has a CSI lane switch on **GPIO43** `[V]` `cust_evergo_camera.dtsi:209-222`.
Vendor policy `[V]` `imgsensor_cfg_table.c:190-235`:

```
SUB   (front/ultrawide) : MIPI_SWITCH_EN=0, MIPI_SWITCH_SEL=1
MAIN2                   : MIPI_SWITCH_EN=0, MIPI_SWITCH_SEL=0
MAIN  (S5KJN1)          : no sequence entry -> default state (SEL=0) [I]
```
⇒ For the rear main camera, drive GPIO43 **low** (or leave at reset default) and document
it as a `gpio-hog`/`mux` in the board DTS. If a first capture yields nothing, toggling
GPIO43 is the cheapest experiment after the lane mapping.

### Step 5 — D-PHY power on

Reuse mainline `phy-mtk-mipi-csi-0-5` after adding `mediatek,mt6833-csi-rx`
`[M]` `phy-mtk-mipi-csi-0-5.c:277-280`:

```
ANA00  : CPHY_EN=0, L0/L1 CKMODE=0 CKSEL=1, L2 CKMODE=1 (clock lane), BG_CORE_EN=1
         [M] :100-105, :143-144
ANAA8  : byte-clock invert on L0/L1/L2      [M] :118-120
ANA18/1C/20 : EQ IS/BW tune                 [M] :56-71
ANA40  : set bits 0x90                      [M] :136
ANA24  : RESERVE=0x40                       [M] :138
WRAPPER80: CSR_CSI_RST_MODE=0               [M] :140
delay ~20-40 us between BG_CORE_EN and BG_LPF_EN   [M] :145-147
```
**Must verify for MT6833**: number of PHY banks (CSIXA `+0`, CSIXB `+0x1000`), the
`4D1C` restriction `[M]` `:184-187`, and the fixed lane permutation
`CSIXA_LNR0->D2, LNR1->D0, LNR2->C, CSIXB_LNR0->D1, LNR1->D3` `[M]` `:89-99`.

### Step 6 — Route the brick to a CAM/CAMSV TG

```
TOP_MUX_CTROL_0/1 (0x0010/0x0014) : select CSI brick -> TG input     [I]
CAM_MUX_CTRL_{0..3} (0x0400..0x040c): nibble for target mux = brick id
                                     (encoding visible: seninf_impl.c:85-128)
CAM_MUXn_OPT (0x0420+n*4)          : data path option               [I]
```

### Step 7 — CSI-2 brick: lanes, settle, VC/DT  ← the real unknown

```
SENINFn_CTRL      (0x0200) : enable brick, TG select
SENINFn_CSI2_CTRL (0x0210) : lane count = 4, continuous clock, VC filter, DT filter
SENINFn_CSI2_EN   (0x0A00) : packet decoder enable
```
Values: `[I]`, unknown from this tree (§2.6). Must come from a golden dump.
Recommended conservative starting point `[I]`: 4 lanes, continuous clock, DT = RAW10
(`0x2B`), VC = 0.

### Step 8 — Verify the link is alive (do this before touching DMA)

```
read 0x0ADC (PACKET_CNT_STATUS) : must increase
read 0x0AC8 (IRQ_STATUS)        : expect 0; if bit set, CRC/ECC/size error
read 0x0D30 (MUX_SIZE)          : must equal frame width/height programming
```
This is exactly what the vendor's `seninf_dump_reg()` prints
`[V]` `seninf.c:104-124` — reuse the layout as the first debugfs file in the new driver.

### Step 9 — CAMSV output format

```
CAMSV TG_PATH_CFG (0x0140) : path enable, pixel/tg mode    [I]
CAMSV TG_SEN_MODE (0x0130) : CMOS_EN=bit0
CAMSV FBC_IMGO_CTL1 (0x0A00): FBC_EN=0 for RAW10 passthrough  [V] cam_regs.h:770-787
CAMSV MODULE_EN (0x0010)   : TG + IMGO DMA enable             [I]
```

### Step 10 — DMA destination

```
CAMSV IMGO_BASE_ADDR (0x0420) = physical address of the vb2 buffer
CAMSV DMA_SOF_RSTSTAT (0x0400): clear
frame size: likely programmed through the MUX_SIZE/format registers  [I]
```
Mainline constraint: **no IOMMU and no LARB driver** `[M]` `mt6833.dtsi:1458-1471`
(commented out). For the first frame, allocate the buffer from a `reserved-memory`
carveout and program its physical address (`vb2-dma-contig` with a no-IOMMU device, or a
plain `dma_alloc_coherent`). IOMMU/LARB is a separate, much larger milestone.

### Step 11 — IRQ

```
CAMSV INT_EN (0x0018) : IMGO done + TG frame done
CAMSV INT_STATUS (0x001C): ack by write-1
```
No vendor DT IRQ for seninf `[V]` `vendor_dtb.dts:3437-3445`; the CAMSV nodes *do* have
interrupts: `camsv3..8` at GIC SPI 0x147..0x14f `[V]` `vendor_dtb.dts:4719-4752`.

### Step 12 — Map the frame

RAW10 arrives packed 4 pixels / 5 bytes; stride = `ALIGN(width*10/8, 16)` `[I]`. For
4080 px ⇒ 5100 bytes/line, 3072 lines ≈ 15.7 MB/frame. Plan for a ≥ 32 MB CMA carveout or
a 2-buffer ping-pong.

### Step 13 — Start

```c
/* order matters; vendor does SEN_MODE first, then VF_CON */
val = readl(base + 0x0130); writel(val | BIT(0), base + 0x0130);   /* CMOS_EN */
val = readl(base + 0x0134); writel(val | BIT(0), base + 0x0134);   /* VFDATA_EN */
```
Reference `[V]` `camera_isp.c:7942-7945` (CAM) and the same two registers for CAMSV
(`0x0130`/`0x0134`, `cam_regs.h:759-760`).

### Step 14 — Stop

```c
val = readl(base + 0x0134); writel(val & ~BIT(0), base + 0x0134);  /* VF off */
/* poll TG idle */
do { st = readl(base + 0x016C) & 0x00003F00; } while (st && --timeout);
```
Reference `[V]` `camera_isp.c:7756` (VF off) + `:7770` (idle mask `0x3F00`, `VS_INT_ST` wait).

---

## 5. What mainline has vs. what must be written

### 5.1 Present and reusable

| Component | Path | Reuse |
|---|---|---|
| camsys-main gate clocks | `drivers/clk/mediatek/clk-mt6833-cam_m.c` | as-is |
| seninf + camtg muxes | `drivers/clk/mediatek/clk-mt6833-topckgen.c:1146-1161,1040-1062` | as-is |
| camsys clock DT nodes | `arch/.../mt6833.dtsi:1344-1360` | as-is |
| MIPI CSI-0.5 D-PHY | `drivers/phy/mediatek/phy-mtk-mipi-csi-0-5.c` | **extend** with `mediatek,mt6833-csi-rx` |
| S5KJN1 subdev | `drivers/media/i2c/s5kjn1.c` | as-is (V4L2 CCI, MC-ready) |
| MT6360 regulator / pinctrl / CMMCLK0 | `pinctrl-mtk-mt6833.h:597` | as-is |

### 5.2 Missing entirely

| Missing | Consequence |
|---|---|
| CAM/CAM_RAWA/CAM_RAWB MTCMOS domains | seninf/camsv register access hangs |
| `seninf` DT nodes + bindings | no driver can bind |
| `camsv` DT nodes + bindings | no capture device |
| `larb13/14/15` + IOMMU nodes (`mt6833.dtsi:1458-1471` commented out) | DMA needs physical carveout |
| `drivers/media/platform/mediatek/seninf/` | no CSI-2 receiver driver |
| `drivers/media/platform/mediatek/camsv/` | no DMA/capture driver |
| WL2866D regulator driver | AVDD/DVDD for the sensor |
| any camera node in the board DTS | no graph, no link |

### 5.3 Proposed mainline architecture

Two viable shapes; recommend **A for bring-up, B for upstream**.

**A. Single self-contained capture driver (bring-up, fastest to first frame)**

```
drivers/media/platform/mediatek/seninf/
    mtk_seninf.h
    mtk_seninf_regs.h      /* offsets from vendor; bitfields from golden dump */
    mtk_seninf.c           /* seninf bricks + crossbar + CAMSV, one video node */
    mtk_seninf_dphy.c      /* thin wrapper around the generic PHY */
```
- one `video_device`, `vb2-dma-contig` queue, `VIDIOC_*` only;
- `s_stream` on the sensor subdev via `v4l2_subdev_call`;
- no media-controller graph: the sensor is bound by a hard-coded
  `devm_v4l2_async_register_subdev` or by a direct fwnode lookup.
- Cost: low. Not upstreamable as-is (no MC graph, no subdev split).

**B. Proper media-controller split (upstream shape)**

```
sensor (s5kjn1, existing subdev)
   -> CSI-2 receiver subdev   (mtk_seninf.c, sink pad = CSI-2, source pad = "csi")
   -> CAMSV video node        (mtk_camsv.c, V4L2 video_device + vb2)
```
- `mtk_seninf.c` becomes a `v4l2_subdev` with `MEDIA_ENT_F_VID_IF_BRIDGE`,
  pads `MTK_SENINF_PAD_SINK`/`MTK_SENINF_PAD_SRC`, implementing
  `get_fmt/set_fmt/enum_mbus_code/s_stream`, and parsing the endpoint
  `data-lanes`, `clock-noncontinuous`, `link-frequencies` with `v4l2_fwnode`;
- `mtk_camsv.c` is a `video_device` with `vb2_v4l2_memops` + `vb2-dma-contig`,
  `media_entity` + `media_pad`, `v4l2_async_notifier` for the graph;
- DT: two nodes, two `port`s each, `remote-endpoint` links — exactly the shape already
  used by `gc05a2.yaml` in this very tree (`Documentation/.../galaxycore,gc05a2.yaml:106`
  references `seninf_csi_port_1_in`).

**DMA engine: not needed.** CAMSV has its own bus-master DMA (IMGO); there is no
`dmaengine` consumer. Use `vb2-dma-contig` and program `IMGO_BASE_ADDR` directly. This is
the same choice as `ti/cal`, `renesas/rcar-vin`, etc.

**Files to add (B):**

```
drivers/media/platform/mediatek/seninf/Kconfig
drivers/media/platform/mediatek/seninf/Makefile
drivers/media/platform/mediatek/seninf/mtk_seninf.c
drivers/media/platform/mediatek/seninf/mtk_seninf.h
drivers/media/platform/mediatek/seninf/mtk_seninf_regs.h
drivers/media/platform/mediatek/camsv/Kconfig
drivers/media/platform/mediatek/camsv/Makefile
drivers/media/platform/mediatek/camsv/mtk_camsv.c
drivers/media/platform/mediatek/camsv/mtk_camsv_regs.h
Documentation/devicetree/bindings/media/mediatek,mt6833-seninf.yaml
Documentation/devicetree/bindings/media/mediatek,mt6833-camsv.yaml
```
plus edits to `drivers/media/platform/mediatek/{Kconfig,Makefile}`.

**DT nodes to add to `mt6833.dtsi`:**

```dts
seninf: csi@1a004000 {
        compatible = "mediatek,mt6833-seninf";
        reg = <0 0x1a004000 0 0xa000>;
        interrupts = <GIC_SPI ??? IRQ_TYPE_LEVEL_HIGH>;   /* not in vendor DTB; optional */
        clocks = <&topckgen CLK_TOP_SENINF_SEL>,
                 <&camsys_main CLK_CAM_M_SENINF>;
        clock-names = "seninf", "cam_m_seninf";
        power-domains = <&scpsys MT6833_POWER_DOMAIN_CAM>;
        #address-cells = <1>; #size-cells = <0>;
        port@0 { ... };   /* to sensor */
};

csi_phy: phy@11c80000 {
        compatible = "mediatek,mt6833-csi-rx";
        reg = <0 0x11c80000 0 0x10000>;
        num-lanes = <4>;
        phy-type = <PHY_TYPE_DPHY>;
        #phy-cells = <0>;
};

camsv: camsv@1a092000 {
        compatible = "mediatek,mt6833-camsv";
        reg = <0 0x1a092000 0 0x1000>;
        interrupts = <GIC_SPI 0x147 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&camsys_main CLK_CAM_M_CAMSV1>, ...;
        power-domains = <&scpsys MT6833_POWER_DOMAIN_CAM>;
};
```

**Board DTS additions (evergo):** sensor `port`/`endpoint` with `data-lanes = <1 2 3 4>`,
`link-frequencies = /bits/ 64 <700000000>`, `remote-endpoint = <&seninf_in>`; WL2866D
regulator nodes (or fixed always-on regulators labelled avdd/dvdd); GPIO43 mipi-switch hog.

---

## 6. Milestones, with references and effort

Effort assumes one engineer familiar with V4L2, no vendor support, hardware on the bench
with a serial console and a way to dump registers. "Hard" = debugging-dominated.

| # | Milestone | Key work | Refs | Effort |
|---|---|---|---|---|
| **M0** | CAM power domain | add `MT6833_POWER_DOMAIN_CAM/RAWA/RAWB` to `mt6833-pm-domains.h` + subsys clks; verify `camsys_main` gate writes land | `[M]` `mt6833-pm-domains.h:61-141`, `mt6833-power.h:26-28`, `[V]` vendor clock-names `vendor_dtb.dts:4663` | 3-5 d |
| **M1** | Sensor power + probe | WL2866D driver or fixed regulators; verify 0x38e1, 24 MHz MCLK on GPIO49 | `[M]` board dts `:1838-1858`, `s5kjn1.c:22-23`; `[V]` `cust_evergo_camera.dtsi:88-117,298-306` | 2-4 d (partly done) |
| **M2** | D-PHY | add `mediatek,mt6833-csi-rx` to `phy-mtk-mipi-csi-0-5.c`; DT node at `0x11c80000`; validate lane permutation | `[M]` `phy-mtk-mipi-csi-0-5.c:277-280,89-99,73-150` | 3-5 d |
| **M2b** | Golden register dump | boot stock Android (or vendor kernel) with camera streaming; snapshot `0x1a004000..0x1a00dfff`, `0x11c80000..+0x2000`, and CAMSV page; diff idle vs streaming | `[V]` `seninf.c:59-131` gives the register list to dump | 2-3 d (mostly logistics) |
| **M3** | seninf CSI-2 driver | brick init, lane count, settle, VC/DT, crossbar; debugfs dump; verify `0x0ADC` increments | `[V]` §2.3 offsets, `seninf_impl.c:85-128`; values from M2b | 8-12 d, hard |
| **M4** | CAMSV capture | MODULE_EN, TG_PATH_CFG, IMGO base, FBC off, IRQ, vb2-dma-contig, start/stop per §4 steps 9-14 | `[V]` `cam_regs.h:751-764`, `camera_isp.c:7942-7945,7756,7770` | 8-12 d, hard |
| **M5** | First frame | end-to-end single RAW10 frame, dump to file, validate against expected 5100 B/line | — | 5-10 d, hard |
| **M6** | MC graph + subdev split | convert to shape B, async notifier, bindings, yaml | §5.3 | 4-6 d |
| **M7** | LARB/IOMMU | un-comment/implement `mt6833.dtsi:1458-1471`, larb nodes, `mtk_iommu` data | `[M]` `mt6833.dtsi:1458-1471` | 10-15 d, hard |
| **M8** | Stabilize + upstream | multi-frame, error paths, pm, checkpatch, submit | — | 15-25 d |

**Realistic totals**
- "One RAW10 frame on a private branch": M0-M5 ⇒ **~5-7 weeks**.
- "Usable camera stack, upstreamable": M0-M8 ⇒ **~3-4 months**, plus libcamera work.

Highest-risk items, in order: M3 (no bitfields), M2 (lane permutation), M0 (power domain
bring-up can hang the bus), M4 (DMA addressing without IOMMU).

---

## 7. Debug strategy without the missing bitfields

1. **Golden dump (M2b)** — the decisive step. The vendor `seninf_dump_reg()` already
   enumerates exactly the registers worth capturing `[V]` `seninf.c:59-131`
   (TOP mux 0x0010/0x0014, CAM_MUX 0x0404/0x0408/0x04A0/0x04A8,
   CHK_RES 0x0508+0x10*n, per-brick 0x0200/0x0210/0x0A00/0x0AC8/0x0ADC/0x0D00/0x0D18/0x0D30).
   Diff "camera idle" vs "camera streaming" and the bit deltas *are* the field definitions.
2. **Vendor ISP register dump path** — `camera_isp.c:5036-5210` has a register-dump ioctl
   that also prints CAMSV/CAM TG state; useful to see the DMA/format programming.
3. **Bisect by register**: first make `0x0ADC` (packet counter) move; that proves PHY +
   lane + VC/DT before any DMA exists. Do not attempt CAMSV until this works.
4. **CRC/ECC**: `0x0AC8` per brick, plus `seninf_impl.c:153-162` shows the vendor reads one
   word per brick. Non-zero ⇒ settle time or lane order wrong.
5. **GPIO43 experiment** (§4 step 4) — cheapest fix if packets never arrive.

---

## 8. Things that will bite (concrete)

- **`0x1A004000` aliasing**: `seninf_top` and `seninf1` are the same page
  `[V]` `vendor_dtb.dts:3407,3437`. A new driver must map the window **once** and treat
  brick 0 as `page0 + 0x0200`, or it will double-map and clobber.
- **Window size**: `0xA000` for all six bricks `[V]` `seninf_cfg.h:25`, but the DT gives
  each node only `0x1000`. Mainline should describe one node with `0xa000` (or six
  sub-nodes) — do not copy the vendor's overlapping layout verbatim.
- **`camsys@1a000000` 64 KiB syscon** in the vendor DTB `[V]` `:4659-4661` overlaps every
  seninf page. Mainline correctly uses a 4 KiB `camsys_main` node `[M]`
  `mt6833.dtsi:1344-1348`; keep it that way.
- **No seninf IRQ in the vendor DTB** `[V]` `:3437-3445` — do not make the driver depend on
  one; poll `0x0AC8`/`0x0ADC` in a workqueue for bring-up.
- **CMDQ events exist for SENINF_0..6 (7 ports)** `[V]` `cmdq_event_common.c:395-401`,
  while the DT only has seninf1..6. There may be a 7th (seninf0/TOP) block; do not assume
  brick index == port index without checking.
- **`cam_m_seninf` is gate bit 8**, but the binding id is `CLK_CAM_M_SENINF = 5`
  `[M]` `mediatek,mt6833-clk.h:593` vs `clk-mt6833-cam_m.c:36`. The id/shift split is
  intentional — do not "fix" the shift to match the id.
- **Sensor rails are currently unmanaged** on mainline (§3.3). A link that trains but drops
  frames is a classic under-powered AVDD symptom.

---

## 9. GPL licensing constraints for porting vendor code

**What the vendor files say.** Every file involved is GPL-2.0: `seninf.c:1-12` and
`:606` (`MODULE_LICENSE("GPL v2")`), `seninf_impl.c:1-12`, `seninf_clk.c:1-12`,
`cam_regs.h:1-13`, `camera_isp.c:1-13`. The wording is "GNU General Public License
**version 2** as published by the FSF" — i.e. GPL-2.0-**only**, not "or later".

**Consequences.**

1. **Copying code is permitted**, because GPL-2.0-only is compatible with the kernel's
   GPL-2.0-only licensing. Mainline files must carry `// SPDX-License-Identifier: GPL-2.0-only`
   (**not** `GPL-2.0-or-later` — you cannot upgrade the vendor's "version 2" wording).
2. **Keep provenance.** Preserve the original `Copyright (C) 2017 MediaTek Inc.` notice,
   add your own, and state in the commit message / file header that the register
   definitions were derived from the MediaTek MT6833 Android 4.14 vendor kernel
   (name the file and the vendor drop). This is both a licence obligation and the honest
   thing to do for reviewers.
3. **The userspace HAL is proprietary and is *not* covered.** The CSI-2 lane/settle/VC-DT
   values live there. Copying HAL *code* is not allowed; observing *register values* is
   fine — addresses and bit patterns are facts, not creative expression. Deriving them from
   a golden dump (§7) is the clean path. Do not paste decompiled HAL source into the kernel.
4. **`EXPORT_SYMBOL` vs `EXPORT_SYMBOL_GPL`.** Vendor code uses plain `EXPORT_SYMBOL`
   (`seninf.c:56`). For a new driver, export nothing unless required; if you must, use
   `EXPORT_SYMBOL_GPL`, consistent with mainline practice for camera internals.
5. **No stable-API relief.** The 4.14 code uses `mt_reg_sync_writel`, `class_create(THIS_MODULE, ...)`,
   `wakeup_source_init`, `sched.h`, `__clk_is_enabled` etc. — all gone or changed by 7.3-rc4.
   In practice the "port" is a rewrite, which conveniently reduces the copying to register
   tables and short sequences.
6. **Third-party mainline code is GPL-2.0 too**, so extending
   `phy-mtk-mipi-csi-0-5.c` (MediaTek + BayLibre, `[M]` `:1-7`) with an MT6833 compatible is
   clean; add your copyright line and keep the SPDX tag.
7. **No patent/trademark grant is implied by GPL-2.0**; MTK register programming sequences
   are not patented as far as is publicly known, but this is not legal advice — if the port
   is for distribution, have counsel review the provenance statement.
8. **Do not copy files with no licence header.** Some vendor `camera_hw` files are
   GPL-headed, but if a file lacks a header, treat it as proprietary and do not lift code
   from it (register constants read out of it are still facts).

---

## 10. Open questions that block certainty

1. Which CSI brick/port is cam0 physically wired to, and what is the lane permutation?
   (partially answerable from the schematic; otherwise M2b.)
2. Does MT6833's PHY need the same `4D1C`-only, fixed-permutation treatment as
   `mt8365-csi-rx`? `[M]` `phy-mtk-mipi-csi-0-5.c:89-99,184-187` is written for a
   different SoC.
3. Is there a seninf interrupt at all on MT6833, and at which SPI?
4. Are `CAM_MUX` targets for cam0 TG0 (CAM) or CAMSV? For a pure RAW dump, CAMSV avoids
   the ISP entirely — preferred.
5. What exactly does `SENINFn_MUX_CTRL_0` (0x0D00) / `MUX_SIZE` (0x0D30) expect for a
   4080-wide RAW10 frame?
6. Is the WL2866D I2C address 0x28 (`wl2866d.dtsi:1`) reachable on mainline `i2c6`
   alongside the LM36273 backlight `[M]` board dts `:478-486`?

---

## 11. Verification checklist (for when hardware work starts)

- [ ] `0x1A000000` readable after CAM MTCMOS on (M0 gate: read `cam_m_seninf` STA bit 8)
- [ ] sensor chip id `0x38e1`, 24.000 MHz measured on GPIO49
- [ ] `0x11C80000` ANA00 reads back the values written (PHY alive)
- [ ] `0x0ADC` packet counter increments with sensor streaming, `0x0AC8 == 0`
- [ ] CAMSV `INT_STATUS` IMGO-done fires once per frame
- [ ] first buffer starts with a plausible RAW10 black frame / embedded header
- [ ] line stride matches `ALIGN(4080*10/8, 16)`
- [ ] stop sequence leaves `TG_INTER_ST[13:8] == 0` and no DMA in flight

---

### Appendix A — one-page address book (MT6833 camera)

| Block | Base | Size | Source |
|---|---|---|---|
| camsys_main (gate regs) | `0x1A000000` | `0x1000` | `[V]` `vendor_dtb.dts:4635`; `[M]` `mt6833.dtsi:1344` |
| LARB13 / 14 / 15 | `0x1A001000` / `0x1A002000` / `0x1A003000` | 4K | `[V]` `vendor_dtb.dts:3582,3592,3602` |
| SENINF1..6 (+TOP in page 0) | `0x1A004000` + `0x1000*n` | `0xA000` total | `[V]` `seninf_cfg.h:21,25`, `vendor_dtb.dts:3407-3445` |
| CAM1 / CAM2 / CAM3 | `0x1A030000` / `0x1A050000` / `0x1A070000` | `0x8000` | `[V]` `vendor_dtb.dts:4700,4706,4712` |
| camsys_rawa / rawb / c | `0x1A04F000` / `0x1A06F000` / `0x1A08F000` | 4K | `[V]` `vendor_dtb.dts:4643,4650,4682` |
| CAMSV3..8 | `0x1A092000` .. `0x1A097000` | 4K | `[V]` `vendor_dtb.dts:4719-4752` |
| MIPI-RX analog (D-PHY) | `0x11C80000` (+`0x1000` CSIXB) | `0x10000` | `[V]` `seninf_cfg.h:22,26`; `[M]` `phy-mtk-mipi-csi-0-5.c:22` |
| CAMSV3 IRQ | GIC SPI `0x147` | — | `[V]` `vendor_dtb.dts:4722` |

### Appendix B — files inspected (for reproducibility)

Vendor: `imgsensor/src/mt6853/{seninf,seninf_mt6833}/*`,
`imgsensor/src/common/v1_1/{seninf.c,seninf_clk.c,seninf.h,seninf_common.h,seninf_clk.h}`,
`imgsensor/inc/{kd_seninf.h,kd_seninf_define.h}`,
`imgsensor/src/common/v1_1/camera_hw/imgsensor_cfg_table.c`,
`imgsensor/src/mt6853/Makefile`,
`cameraisp/src/mt6853/{camera_isp.c,inc/cam_regs.h}`,
`cmdq/v3/cmdq_event_common.c`, `video/mt6833/cmdq/v3/cmdq_event_common.c`,
`arch/arm64/boot/dts/mediatek/{cust_evergo_camera.dtsi,wl2866d.dtsi}`,
`/root/vendor_dtb.dts`.

Mainline: `arch/arm64/boot/dts/mediatek/{mt6833.dtsi,mt6833-xiaomi-evergo.dts}`,
`drivers/clk/mediatek/clk-mt6833-{cam_m,topckgen}.c`,
`include/dt-bindings/clock/mediatek,mt6833-clk.h`,
`include/dt-bindings/power/mediatek,mt6833-power.h`,
`drivers/pmdomain/mediatek/{mt6833-pm-domains.h,mtk-pm-domains.c}`,
`drivers/phy/mediatek/phy-mtk-mipi-csi-0-5{,-rx-reg.h}.c`,
`drivers/media/i2c/s5kjn1.c`, `drivers/media/platform/mediatek/*`,
`drivers/pinctrl/mediatek/pinctrl-mtk-mt6833.h`,
`Documentation/devicetree/bindings/clock/mediatek,syscon.yaml`,
`Documentation/devicetree/bindings/media/i2c/galaxycore,gc05a2.yaml`.
