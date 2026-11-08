# MT6833 / Xiaomi `evergo` camera ISP and libcamera engineering plan

Status: source-analysis only. No kernel was built, nothing was flashed, and the
device was not touched. All addresses and line numbers below are from the two
trees named in the request:

* vendor: `/root/android-kernel` (Android 4.14, MT6833)
* mainline: `/root/memtree` (Linux 7.3.0-rc4)

The decompiled vendor DT (`/root/vendor_dtb.dts`) is used only to recover node
names, addresses and interrupts that are missing from the vendor source DTS.

---

## 0. Bottom line

1. **There is no `drivers/misc/mediatek/imgsys` and no
   `drivers/media/platform/mediatek` in the vendor tree.** The MT6833 camera
   stack lives in `drivers/misc/mediatek/cameraisp/` (plus `ccu/`,
   `imgsensor/`, `cam_cal/`, `mdp/`, and the WL2866D regulator).
2. **The vendor drivers are mostly resource managers, not ISP programming
   code.** `/dev/camera-isp` even exposes `mmap()` of the CAM/CAMSV register
   windows (`camera_isp.c:6988-7044`), and the DIP driver copies a userspace
   "tpipe" register program into its own buffer (`camera_dip.c:485,3976`).
   The actual sensor/CSI/ISP register sequences live in the proprietary
   Android HAL. A kernel-only port of the vendor code therefore produces no
   image on a mainline/Linux userspace.
3. **Mainline has no camera capture/ISP driver for any MediaTek SoC** in this
   tree: `drivers/media/platform/mediatek/` contains only `jpeg`, `mdp`,
   `mdp3`, `vcodec`, `vpu` (`Kconfig:5-9`). MT6833 has camera *clock*
   controllers only (`mt6833.dtsi:1320-1360`), no SENINF/CAM/CAMSV/DIP nodes.
4. Mainline does have useful building blocks: MT6833 GCE/CMDQ is already
   supported (`mt6833.dtsi:15-19,1167`; `mtk-cmdq-mailbox.c:861`), the MT8365
   MIPI CSI D-PHY driver may be reusable (`phy-mtk-mipi-csi-0-5.c:277`), and
   mainline sensor drivers exist for the evergo modules (`s5kjn1`, `imx355`,
   `ov8856`, `ov02a10/ov02c10`), although the OV16A1Q/OV50C40 variants named
   in the vendor DTS would still need new sensor drivers.
5. **Recommended first deliverable: RAW capture through a new minimal
   mainline SENINF + MRAW driver, debayered in userspace** (libcamera simple
   pipeline + SoftISP, or a plain V4L2 capture plus an offline debayer). This
   avoids the DIP, CCU, statistics engines and the proprietary tuning data.
   First YUV frame is realistic in roughly **5-8 person-months** for one
   experienced developer; a hardware-DIP YUV path brings the total to
   **11-16 months** and is where most of the risk sits.

---

## 1. What the vendor camera stack actually is

### 1.1 Source inventory

| Area | Path | Size | Role |
|---|---|---:|---|
| CAMSYS / MRAW / CAMSV | `drivers/misc/mediatek/cameraisp/src/mt6853/camera_isp.c` | 12,877 lines / 366 KB | `/dev/camera-isp`: maps CAM A/B/C, CAMSV3..8 and CAMSYS, owns IRQs and DMA buffer rings, accepts private ioctls and register mmap |
| CAMSYS headers | `.../src/mt6853/inc/camera_isp.h`, `cam_regs.h` | 954 + 1,6xx lines | ABI, DMA enum, register offset macros |
| DIP / IMGSYS | `drivers/misc/mediatek/cameraisp/dip/isp_6s/camera_dip.c` | 8,902 lines / 270 KB | `/dev/camera-dip`: DIP_A/B, MFB/MSS/MSF, CMDQ + tpipe buffer management, IRQs |
| DIP header | `.../dip/isp_6s/camera_dip.h` | 386 lines | ABI and base addresses |
| 3A offload | `drivers/misc/mediatek/ccu/src/mt6853/` | 7,762 lines | `/dev/ccu`, Cortex-M4 firmware loader, mailbox/IPC, ION buffers |
| Sensors / SENINF | `drivers/misc/mediatek/imgsensor/` | 11 MB | vendor sensor framework, per-sensor drivers, SENINF clock/mux helpers |
| Calibration | `drivers/misc/mediatek/cam_cal/` | 240 KB / 985 lines | EEPROM read and sensor calibration list |
| Camera PMIC | `drivers/regulator/wl2866d.c` | 949 lines | WL2866D AVDD/DVDD regulator driver (Xiaomi compatible) |
| CMDQ helper | `drivers/misc/mediatek/mdp/` | ~30 files | GCE/CMDQ helpers used by DIP and display |

The kernel build gates for MT6833 are in
`cameraisp/src/Makefile` (MT6833 uses `src/mt6853/` and
`-DDISABLE_SV_TOP0`) and `cameraisp/dip/Makefile` (MT6833 uses `isp_6s/`).
`evergo_defconfig:111-126` enables RSC/DPE/FD/MFB and `CONFIG_MTK_CCU=y`.

### 1.2 Hardware map (vendor DT)

From `/root/vendor_dtb.dts` and `cust_evergo_camera.dtsi`:

| Block | DT node | Base | Size | IRQ (SPI) |
|---|---|---|---:|---:|
| CAMSYS main / clock+CG | `camsys_main@1a000000` | `0x1a000000` | `0x1000` | – |
| CAMSYS full syscon | `camsys@1a000000` | `0x1a000000` | `0x10000` | – |
| CAMSYS RAW A | `camsys_rawa@1a04f000` | `0x1a04f000` | `0x1000` | – |
| CAMSYS RAW B | `camsys_rawb@1a06f000` | `0x1a06f000` | `0x1000` | – |
| CAM/MRAW A | `cam1@1a030000` | `0x1a030000` | `0x8000` | 0x140 |
| CAM A inner | `cam1_inner@1a038000` | `0x1a038000` | `0x8000` | – |
| CAM/MRAW B | `cam2@1a050000` | `0x1a050000` | `0x8000` | 0x141 |
| CAM B inner | `cam2_inner@1a058000` | `0x1a058000` | `0x8000` | – |
| CAM/MRAW C | `cam3@1a070000` | `0x1a070000` | `0x8000` | – |
| CAM C inner | `cam3_inner@1a078000` | `0x1a078000` | `0x8000` | – |
| CAMSV3..8 | `camsv3@1a092000` … `camsv8@1a097000` | `0x1a092000`…`0x1a097000` | `0x1000` each | 0x147–0x14a, 0x14e–0x14f |
| SENINF top | `seninf_top@1a004000` | `0x1a004000` | `0x1000` | – |
| SENINF1..6 | `seninf1@1a004000` … `seninf6@1a009000` | `0x1a004000`…`0x1a009000` | `0x1000` each | – |
| MIPI CSI D-PHY analog | **no DT node**; hardcoded `0x11c80000` in `seninf_cfg.h:22` | `0x11c80000` | `0x10000` | – |
| CCU (Cortex-M4) | `ccu@1a101000` | `0x1a101000` | `0x1000` | 0x14b |
| IMGSYS1 config | `imgsys_config@15020000` | `0x15020000` | `0x1000` | – |
| DIP_A | `dip_a0@15021000` (`mediatek,dip1`) | `0x15021000` | `0xc000` | 0x15a |
| MFB/WPE A | `mfb@15010000`, `wpe_a@15011000` | `0x15010000`, `0x15011000` | `0x1000` | 0x15c, 0x15d |
| MSS A | `mfb@15012000` | `0x15012000` | `0x1000` | – |
| IMGSYS2 config | `imgsys2_config@15820000` | `0x15820000` | `0x1000` | – |
| DIP_B | `dip_b0@15821000` | `0x15821000` | `0xc000` | 0x161 |
| MFB/MSS B | `mfb_b@15810000`, `mss_b@15812000` | `0x15810000`, `0x15812000` | `0x1000` | 0x1f4, 0x1f5 |
| IOMMU / M4U | `m4u@14016000` | `0x14016000` | `0x1000` | 0x10f |
| GCE / CMDQ | `gce@10228000` (mainline) | `0x10228000` | `0x4000` | 203 |

Useful register offsets: CAM IMGO base `+0x4820`, IMGO XSIZE/YSIZE
`+0x482c/0x4830`, IMGO CON `+0x4838`; RRZO `+0x4900`; AAO `+0x44a0`; AFO
`+0x4510`; PDO `+0x4260` (`cam_regs.h:463-480`). CAMSV IMGO `+0x0420`, TG
mode `+0x0130`, VF `+0x0134` (`cam_regs.h:754-763`). DIP_A window starts at
`0x15021000`, with the actual DIP core alias at `0x15022000`
(`camera_dip.c:515-518`; `camera_dip.h:42-58`).

### 1.3 Pipeline block diagram

```
  S5KJN1 / OV50C40 / OV16A1Q / IMX355          (I2C: i2c2/i2c4/i2c8)
  MCLK0..3 via CAMTG mux, GPIO49/52/51/50
  reset GPIO54/60/58/56, rails from WL2866D + PMIC
            |
            |  MIPI CSI-2, 4 data lanes (+ clock)
            v
  +---------------------------------------+
  | MIPI CSI D-PHY (analog)               |
  | 0x11c80000 (vendor hardcoded; no DT)  |
  | candidate reuse: phy-mtk-mipi-csi-0-5 |
  +-------------------+-------------------+
                      |
                      v
  +---------------------------------------+
  | SENINF (CSI-2 receiver / lane mux)    |
  | top + 1..6: 0x1a004000..0x1a009000    |
  | CSI2 ctrl/en, VC/dt, TG mux           |
  +-------------------+-------------------+
                      |
        +-------------+----------------------------+
        |                                          |
        v                                          v
  +-------------------------------+       +-------------------------------+
  | CAM / MRAW A (0x1a030000)     |       | CAMSV3..8 (0x1a092000..)      |
  |  B (0x1a050000)               |       |  simple/DCIF capture           |
  |  C (0x1a070000)               |       |  no demosaic; YUV sensors only |
  | TG + MRAW + IMGO/RRZO DMA     |       |  IMGO DMA only                 |
  | AAO/AFO/PDO/LCSO stats (opt.) |       +-------------------------------+
  +---------------+---------------+               (optional;
                  |                                all evergo sensors are RAW)
                  | IMGO full RAW / RRZO resized RAW
                  v
             DRAM (RAW10)
                  |
                  | optional hardware path
                  v
  +---------------------------------------+
  | DIP_A 0x15021000  (ISP6s, IMGSYS1)    |
  | DIP_B 0x15821000  (IMGSYS2)           |
  | RAW->YUV, colour, gamma, NR ...       |
  | programmed by GCE CMDQ + tpipe from   |
  | the proprietary HAL                   |
  +-------------------+-------------------+
                      | YUVO / IMGO
                      v
                 DRAM (YUV)

  Optional / orthogonal:
    CCU 0x1a101000  Cortex-M4 3A, firmware lib3a.ccu
    MFB/WPE/RSC/DPE/TSF/OWE/FDVT/PDA  imgsys post-processing
    IOMMU 0x14016000 + SMI larbs       address translation
    GCE 0x10228000                     command queue for DIP
```

### 1.4 What each piece really does

* **MRAW (called "CAM" everywhere in the vendor code).** This is the RAW
  capture block: test pattern generator/TG, MIPI pixel packing, and the DMA
  engines `IMGO` (full RAW), `RRZO` (resized RAW), `UFEO/UFGO`, `YUVO/B/C`,
  and the statistics DMAs `AAO` (AE), `AFO` (AF), `PDO` (phase detect),
  `LCSO` (lens shading), `LTMSO`, `RSSO`, `CRZO`, etc. The list is in
  `camera_isp.h:315-390`; the register macros are in `cam_regs.h:463-480`.
  MRAW does **not** demosaic.
* **CAMSV.** "Camera simple video" is a TG + DMA path intended for
  YUV/DCIF sensors and low-power capture. It has no demosaic. On evergo all
  populated sensors are RAW (`cust_evergo_camera.dtsi:318-322`), so CAMSV is
  not a YUV path for this device. Whether it can pass RAW10 is not proven by
  the code in this tree; if it can, it would be a simpler capture engine than
  MRAW, but that needs a TRM experiment.
* **DIP / IMGSYS.** The RAW->YUV ISP. `camera_dip.c` is a buffer/IRQ/CMDQ
  manager. It does not contain a default image pipeline: the tpipe register
  program is copied from userspace (`camera_dip.c:485,3976`) and the actual
  frame is submitted through GCE CMDQ (`camera_dip.c:65`, 793 `cmdq`
  references). MFB/WPE/RSC/DPE/TSF/OWE/FDVT/PDA are optional post-processing.
* **CCU.** A Cortex-M4 that runs MediaTek's 3A firmware. The driver requests
  `lib3a.ccu` from firmware (`ccu_hw.c:940`), uses ION and mailbox/IPC. It is
  optional if exposure/gain are set manually.
* **Statistics.** AAO/AFO/PDO/LCSO are DMA outputs from MRAW. They are needed
  for AE/AF/AWB/PD calibration but **not** for a first frame with fixed
  exposure, gain and focus.
* **SENINF.** The CSI-2 receiver and lane/virtual-channel router. The vendor
  kernel only has clock/mux helpers (`seninf.c` 607 lines,
  `seninf_impl.c:153-163`); the register programming is done by the HAL.
* **MIPI D-PHY.** Vendor has no PHY driver at all: the analog base is only
  mmap'd to userspace (`seninf.c:200-215`, `seninf_cfg.h:21-27`).

### 1.5 First-frame block budget: strictly needed vs optional

"First YUV" is defined two ways below. Column A is the recommended
software-debayer path; column B is the hardware-DIP path.

| Function / block | A: first YUV via software | B: first YUV via DIP | Why |
|---|---|---|---|
| Sensor + I2C + reset + rails + MCLK | **required** | **required** | no sensor stream otherwise |
| MIPI CSI D-PHY (`0x11c80000`) | **required** | **required** | receives the differential lanes |
| SENINF (`0x1a004000..`) | **required** | **required** | CSI-2 packet decode, VC/lane routing |
| MRAW/CAM TG + IMGO DMA (`0x1a030000`) | **required** | **required** | turns the CSI stream into RAW10 in DRAM |
| RRZO resized RAW | optional | optional | lower-resolution preview for CPU debayer |
| DIP_A/B + IMGSYS (`0x1502/0x1582`) | **not used** | **required** | only block that does RAW->YUV |
| GCE/CMDQ (`0x10228000`) | not used | **required for DIP** | DIP frame submission |
| IOMMU/SMI (`0x14016000`) | optional initially (verify larb state) | recommended | CMA physical buffers work for first light |
| AAO/AFO/PDO/LCSO statistics | optional | optional initially | needed for AE/AF/AWB/PD calibration, not for fixed settings |
| CCU (`0x1a101000`, `lib3a.ccu`) | optional | optional | 3A offload; fixed exposure/gain works without it |
| CAMSV3..8 (`0x1a092000..`) | not used | not used | no demosaic; intended for YUV/DCIF sensors |
| MFB/WPE/RSC/DPE/TSF/OWE/FDVT/PDA | not used | not used for first frame | post-processing / depth / warp / face |
| EEPROM / OTP calibration | optional | recommended for quality | first frame can use fixed gains and generic CCM |

**Answer to "what is strictly needed":** sensor + D-PHY + SENINF + MRAW
TG/IMGO + DRAM buffers. Statistics, AF/AE/AWB, CCU, CAMSV, MFB/WPE/RSC and
the DIP are all optional for a first frame. The DIP becomes mandatory only
if you require hardware (not CPU) YUV.

### 1.6 The userspace coupling (the most important finding)

The vendor kernel exposes **private character devices**, not V4L2:

* `/dev/camera-isp` — major 251; ioctls `ISP_READ_REGISTER`,
  `ISP_WRITE_REGISTER`, `ISP_BUFFER_CTRL`, `ISP_WAIT_IRQ`, `ISP_ION_*`,
  `ISP_CQ_SW_PATCH`, etc. (`camera_isp.h:752-895`). `ISP_mmap()` accepts the
  physical CAM/CAMSV bases and maps them to userspace
  (`camera_isp.c:6988-7044`). In other words the HAL can write every MRAW
  register directly.
* `/dev/camera-dip` — major 251; `DIP_READ_REGISTER`, `DIP_WRITE_REGISTER`,
  `DIP_P2_BUFQUE_CTRL`, `DIP_SET_MEM_INFO`, `DIP_DUMP_BUFFER`
  (`camera_dip.h:315-348`). The tpipe and CMDQ buffers are supplied by
  userspace (`camera_dip.c:3966-3993`).
* `/dev/ccu` — CCU firmware load and mailbox (`ccu_drv.c:79,1429`).
* `/dev/seninf` — exposes SENINF register and PHY analog bases via mmap
  (`seninf.c:200-215`), plus MCLK and mux ioctls.
* `/dev/imgsensor` and the `kd_imgsensor` API — vendor sensor framework.

Consequence: porting `camera_isp.c` + `camera_dip.c` to 7.3 without the
proprietary HAL gives a driver that can map registers and queue buffers but
has no register sequences for a sensor mode, no CSI-2 setup, and no tpipe.
That is why option (a) below is not a shortcut for a mainline system.

---

## 2. Mainline 7.3-rc4 state and distance to MT6833

### 2.1 What exists

| Component | Mainline status | Evidence |
|---|---|---|
| MediaTek media platform | JPEG, MDP, MDP3, VCODEC, VPU only | `drivers/media/platform/mediatek/Kconfig:5-9` |
| Camera ISP / SENINF / CAMSV / DIP | **none for any SoC** | directory listing; no `camsys`, `seninf`, `camsv`, `mraw`, `dip` files |
| MT6833 camera clocks | CAMSYS main/RAWA/RAWB, IMGSYS1/2 gates | `mt6833.dtsi:1320-1360`; `clk-mt6833-cam_m.c:30-47`; `clk-mt6833-cam_ra.c:31-33`; `clk-mt6833-cam_rb.c:31-33`; `clk-mt6833-imgsys1.c:31-34`; `clk-mt6833-imgsys2.c:31-36` |
| MT6833 camera power | only GPU/conn/audio domains; ISP/CAM explicitly left to bootloader | `mt6833-pm-domains.h:14-16`; `clk-mt6833-pg.c:156-165,410-423,634-656` |
| MT6833 IOMMU | node is commented out | `mt6833.dtsi:1458-1471` |
| MT6833 SMI | no larbs in DT; driver has no MT6833 data | `mtk-smi.c:549-560,844-859` |
| MT6833 GCE/CMDQ | supported, described as MT8183 clone | `mt6833.dtsi:15-19,1167-1177`; `mtk-cmdq-mailbox.c:861` |
| MIPI CSI D-PHY | MT8365 driver exists, 4-lane D-PHY | `phy-mtk-mipi-csi-0-5.c:1-120,277-279` |
| MT8195 camera infrastructure | CAM/CSI/DIP power domains + clock controllers, but no capture nodes | `mt8195.dtsi:749-790,2654,2751` |
| Sensors | `s5kjn1`, `imx355`, `ov8856`, `ov02a10`, `ov02c10` | `s5kjn1.c:1467`; `imx355.c:1340`; `ov8856.c:2475`; `ov02a10.c:983`; `ov02c10.c:994` |
| libcamera | no MT6833 pipeline handler in upstream; ChromeOS has `mtkisp7` for MT8195 only | see §5 |

### 2.2 MT6833 gap table

| Piece | Mainline MT6833 | What is missing |
|---|---|---|
| CAMSYS clock gates | yes | nothing for clocks; driver still needs power/IRQ/device node |
| CAM/MRAW A/B/C | no | full driver: TG, DMA, CQ, IRQ, V4L2 |
| SENINF / CSI-2 | no | full receiver: D-PHY attach, lane map, CSI2 decode, VC routing, error IRQ |
| CAMSV3..8 | no | optional simple-capture driver |
| DIP_A/B / IMGSYS | clocks only | DIP driver, CMDQ programming, tpipe ABI, tuning |
| CAM/RAWA/RAWB power | no | genpd domains (SPM `0x35c/0x360/0x364`, status bits 23/24/25) |
| ISP/ISP2 power | no | genpd domains (SPM `0x334/0x338`, status bits 13/14) for DIP |
| IOMMU | commented out | MT6833 IOMMU data + larbs + `iommus` |
| SMI larbs 13/14/16/17 | no | larb nodes/data (mt8183/mt6893 are starting points) |
| MIPI D-PHY | MT8365 only | add MT6833 compatible/base and verify register layout |
| Sensor graph | sensor node only | `port`/`endpoint`, `data-lanes`, `link-frequencies`, supplies |
| libcamera | no handler | simple+SoftISP use, or a new pipeline handler |

**Distance:** the sensor side is ~80% done (drivers exist, one node is already
in the evergo DTS), the clock side is ~70% done, the power/IOMMU/SMI side is
~0-20%, and the capture/ISP side is 0%. The missing work is not a DT tweak;
it is a new media driver.

The existing evergo DTS already has a useful starting point:

* `&i2c2` S5KJN1 node at `camera@10`, reset GPIO54, CMMCLK0 on GPIO49,
  24 MHz parent (`mt6833-xiaomi-evergo.dts:1827-1858`);
* WL2866D enable GPIO hog (`:1807-1818`);
* mainline pinctrl has `CMMCLK0..3` functions
  (`pinctrl-mtk-mt6833.h:597-625`);
* but no `port`/`endpoint`, no `vdda`/`vddd` supplies, no CSI/SENINF/MRAW
  nodes. The sensor node currently supplies only `vddio`
  (`mt6833-xiaomi-evergo.dts:1845-1858`), while the vendor design uses
  AVDD/DVDD/DOVDD (`cust_evergo_camera.dtsi:298-310`).

---

## 3. Options

### 3.1 Option (a): port the vendor camsys/imgsys drivers out-of-tree

**What it would take**

* Port `camera_isp.c` (12.9k lines), `camera_dip.c` (8.9k lines), `ccu`
  (7.8k lines), and enough of `imgsensor` to probe sensors.
* Replace vendor APIs: ION (137 references in `camera_isp.c`, 824 in
  `camera_dip.c`, plus CCU), `mtk_iommu_ext`/`m4u_*` (32-bit/34-bit M4U),
  `smi_public` (`smi_bus_prepare_enable`), vendor CCF power callbacks
  (`register_pg_callback`, `Prepare_Enable_ccf_clock`), `mt-plat/sync_write.h`,
  `archcounter_timesync`, and the vendor `kd_imgsensor` framework.
* Port the private ioctl ABI and register `mmap()` (major 251, custom
  structures). This is not V4L2 and cannot be consumed by libcamera.
* Supply a userspace HAL that writes the register sequences. The vendor HAL
  is proprietary and not in either tree.

**Pros:** fastest way to reuse known-good register programming *if* you also
run the Android HAL; useful as a trace/reference oracle on an Android kernel.

**Cons:** large 4.14->7.3 API surface; ION is gone; no V4L2; no upstream
future; the result is still useless on a mainline Linux userspace without a
new HAL. This is an 8-12 person-week build/boot project, plus roughly 20-40
person-weeks for a minimal HAL replacement, and it remains non-upstreamable.
It is **not recommended** as the mainline path.

### 3.2 Option (b): develop a minimal mainline ISP driver

Write a new GPL-2.0 driver set under
`drivers/media/platform/mediatek/camsys/`:

* `mtk-camsys-core.c` — clocks, power, syscon, top-level reset, common IRQ.
* `mtk-seninf.c` — V4L2 subdev: D-PHY attach, CSI-2 lane/VC config, routing to
  MRAW, error handling.
* `mtk-mraw.c` — V4L2 video node: TG timing from the sensor mode, IMGO/RRZO
  DMA, buffer queue, SOF/DON IRQ, metadata.
* `mtk-camsv.c` — optional simple-capture path (only if a YUV sensor or a
  proven RAW10 CAMSV path is needed).
* `mtk-dip.c` — phase 2, only if hardware YUV is required.

Use the existing sensor drivers, CCF clock gates, GCE (for DIP later), and
CMA buffers initially (no IOMMU). This is clean, upstreamable, and the only
option that leads to a libcamera-capable mainline camera.

**Effort:** ~6-9 months for RAW, +6-9 months for hardware DIP YUV. This is
the recommended long-term architecture.

### 3.3 Option (c): bypass the ISP and stream RAW only

Strictly, this still needs SENINF + MRAW (the RAW capture part of "camsys");
it bypasses **DIP, CCU, statistics and post-processing**, not the whole ISP.
The RAW10 frames are debayered on the CPU.

* Kernel: same SENINF + MRAW driver as option (b), stage 1 only.
* Userspace: `v4l2-ctl`/`yavta` + a debayer, or libcamera's simple pipeline
  with SoftISP.
* No DIP tpipe, no GCE, no CCU firmware, no 3A calibration needed for first
  light. Fixed exposure/gain/focus are enough.

**Effort:** ~5-7 months for one developer (including the unavoidable SENINF
and MRAW work). This is the recommended **first milestone**; option (b) then
extends it.

### 3.4 Comparison

| | (a) vendor out-of-tree | (b) minimal mainline ISP | (c) RAW + software ISP |
|---|---|---|---|
| Upstreamable | no | yes | yes (stage 1 of b) |
| Kernel work | 2-4 months to boot, much more to use | 6-9 months RAW, +6-9 DIP | 5-7 months |
| Userspace | proprietary HAL; must reimplement | libcamera/new handler | libcamera simple+SoftISP or plain V4L2 |
| First YUV | only with Android HAL | software first, hardware later | software |
| Main risk | no HAL/source, ION/IOMMU, ABI | SENINF/MRAW docs, MIPI tuning | sensor/CSI bring-up |
| Licensing | GPL kernel + proprietary HAL | clean GPL | clean GPL |
| Recommendation | reference only | long-term | **first deliverable** |

---

## 4. Recommended plan

### 4.1 Architecture

Stage 1 (first YUV):

```
sensor (mainline s5kjn1) -> CSI D-PHY -> SENINF -> MRAW -> /dev/video0 (RAW10)
                                                               |
                                                     libcamera simple + SoftISP
                                                               |
                                                          RGB/YUV frame
```

Stage 2 (production RAW): add IOMMU/SMI, runtime PM, second/third cameras,
CAMSV if a YUV sensor appears, and proper tuning.

Stage 3 (hardware YUV, optional): add DIP_A/B and a custom libcamera pipeline
handler, or keep SoftISP if power/quality is acceptable.

### 4.2 Device-tree sketch

Keep the existing clock-controller nodes and add camera devices at
non-overlapping addresses. The CAMSYS clock controller is only 0x1000, so it
does not conflict with SENINF at 0x1a004000 or MRAW at 0x1a030000.

```dts
csi0_rx: phy@11c80000 {
    compatible = "mediatek,mt6833-csi-rx";
    reg = <0 0x11c80000 0 0x2000>;
    num-lanes = <4>;
    #phy-cells = <1>;
};

seninf: csi2@1a004000 {
    compatible = "mediatek,mt6833-seninf";
    reg = <0 0x1a004000 0 0x6000>;
    interrupts = <GIC_SPI ... IRQ_TYPE_LEVEL_LOW>;
    clocks = <&topckgen CLK_TOP_SENINF_SEL>,
             <&camsys_main CLK_CAM_M_SENINF>,
             <&camsys_main CLK_CAM_M_CAM>;
    clock-names = "seninf", "seninf_gate", "cam";
    power-domains = <&spm MT6833_POWER_DOMAIN_CAM>;
    phys = <&csi0_rx PHY_TYPE_DPHY>;
    phy-names = "dphy";
    ports {
        port@0 { endpoint { remote-endpoint = <&s5kjn1_out>; }; };
        port@1 { endpoint { remote-endpoint = <&mraw_in>; }; };
    };
};

mraw: isp@1a030000 {
    compatible = "mediatek,mt6833-mraw";
    reg = <0 0x1a030000 0 0x8000>;
    interrupts = <GIC_SPI 320 IRQ_TYPE_LEVEL_LOW>;
    clocks = <&camsys_rawa CLK_CAM_RA_CAM>,
             <&camsys_rawa CLK_CAM_RA_CAMTG>,
             <&camsys_rawa CLK_CAM_RA_LARBX>;
    clock-names = "cam", "camtg", "larb";
    power-domains = <&spm MT6833_POWER_DOMAIN_CAM_RAWA>;
    ports {
        port@0 { endpoint { remote-endpoint = <&seninf_out>; }; };
    };
};
```

The sensor endpoint needs `data-lanes = <1 2 3 4>` and
`link-frequencies = /bits/ 64 <700000000>` to match the mainline S5KJN1
driver's 4-lane requirement (`s5kjn1.c:1163-1197`), plus `vdda`/`vddd`/`vddio`
supplies.

### 4.3 Driver decomposition and programming model

* **SENINF**: model as a V4L2 subdev with one sink pad (sensor) and one source
  pad (MRAW). On `s_stream(1)`: power the D-PHY, program lane mapping, CSI-2
  data type RAW10, virtual channel, enable CSI-2, route to the MRAW TG mux.
  Handle ECC/CRC/short-packet errors.
* **MRAW**: model as a V4L2 video node (`V4L2_CAP_VIDEO_CAPTURE_MPLANE`) or a
  subdev + video node. Program TG timing from the active sensor mode,
  configure IMGO (and optionally RRZO) with a stride, allocate CMA buffers
  with `vb2-dma-contig`, start/stop via `TG_VF_CON`, and use SOF/DON IRQs.
  The first version can write registers directly at stream-on; per-frame
  updates can use the MRAW internal CQ later.
* **Buffers**: start with `dma_alloc_coherent`/CMA and physical addresses in
  `CAM_REG_IMGO_BASE_ADDR`. This avoids IOMMU/SMI for the first frame. Move to
  `iommus`/`dma-buf` once the basic path works.
* **Power/clocks**: use CCF gates and genpd. Add CAM, CAM_RAWA, CAM_RAWB
  domains with SPM `0x35c/0x360/0x364`, status bits 23/24/25, and bus
  protection masks from `clk-mt6833-pg.c:374-387`. ISP/ISP2 (`0x334/0x338`,
  bits 13/14) are only needed for DIP.
* **Reset**: CAMSYS reset registers are in the 0x1a000000 block; use
  `regmap`/`syscon` and the vendor offsets as documentation. A full reset
  controller is optional for the first frame.
* **IRQ**: CAM A is SPI 320 (`0x140`), CAM B 321 (`0x141`); CAMSV3..8 are
  0x147-0x14a and 0x14e-0x14f; DIP_A 0x15a, DIP_B 0x161. Use the vendor
  `cam_regs.h` bit definitions for SOF/DON/error.

### 4.4 What to reuse from vendor code

Reuse **facts**, not code structure:

* register offsets and bit fields from `cam_regs.h`, `camera_isp.h`;
* sensor mode timing (HTS/VTS, lane count, link frequency) from the vendor
  sensor driver and the mainline sensor driver;
* DMA port names and the IMGO/RRZO/AAO/AFO/PDO layout;
* SPM power-domain registers and bus-protection masks from
  `clk-mt6833-pg.c`;
* DIP tpipe/CMDQ format only if stage 3 is attempted;
* WL2866D regulator behaviour from `wl2866d.c`/`wl2866d.dtsi` (or write a
  small mainline regulator driver).

Do **not** copy the vendor sensor framework or the private ioctl ABI into
mainline; use V4L2/media-controller and the existing mainline sensor drivers.

---

## 5. Userspace

### 5.1 Plain V4L2 application (fastest bring-up)

* Tools: `v4l2-ctl`, `yavta`, `media-ctl`.
* Configure the sensor subdev format (`SRGGB10_1X10`, 4080x3072), the SENINF
  and MRAW formats, then stream to a raw file. Example commands (not run
  here):

  ```
  media-ctl -d /dev/media0 --set-v4l2 '"s5kjn1 2-0010":0[fmt:SRGGB10_1X10/4080x3072]'
  v4l2-ctl -d /dev/video0 --set-fmt-video=width=4080,height=3072,pixelformat=RG10
  v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=10 --stream-to=frame.raw
  ```

* Debayer offline with `rawtherapee`, `dcraw`, `libcamera`'s tools, or a
  small Python/OpenCV script. This is the quickest way to prove the sensor,
  CSI and DMA path.
* Controls: `v4l2-ctl -c exposure=...,analogue_gain=...` once the sensor
  driver is bound. The mainline S5KJN1 driver exposes exposure, analogue
  gain, VBLANK, HBLANK, test pattern, link frequency and pixel rate
  (`s5kjn1.c:802-860`), which is enough for fixed-exposure bring-up and for
  SoftISP.

### 5.2 libcamera

There is **no upstream libcamera pipeline handler for MT6833**. The ChromeOS
`mtkisp7` handler targets MT8195's ISP7 and the downstream `mtk-isp` kernel
driver; it is not applicable to MT6833/ISP6s.

The realistic libcamera route is the generic **simple pipeline handler with
software ISP (SoftISP)**:

* It consumes a sensor subdev plus a CSI-2 receiver/video node and performs
  debayer, AE, AWB and AGC on the CPU.
* Upstream libcamera has had SoftISP since the 0.5.x series; raw-stream
  handling on top of SoftISP was still being finalized in 2025
  ([libcamera v0.5.0 announcement](https://lists.libcamera.org/pipermail/libcamera-devel/2025-April/049672.html),
  [raw-stream series](https://lists.libcamera.org/pipermail/libcamera-devel/2025-March/049429.html)).
  For our use case the **processed** stream is the target; raw capture can be
  done with plain V4L2 in parallel.
* Requirements: a media-controller graph sensor -> CSI receiver -> capture
  video node, V4L2 sensor controls, and correct timestamps/metadata. The
  driver must not hide the sensor behind a non-V4L2 interface.
* A per-sensor tuning file may be needed for good colour. Minimal tuning can
  start from generic values; a proper file should be generated with
  `camshark`/`ctt` from raw captures once the RAW path works.
* For hardware DIP YUV, a new libcamera pipeline handler would be needed
  (per-frame tuning, 3A, DIP parameter packing). That is a separate
  multi-month project and should not block first light.

If SoftISP performance is inadequate at 4080x3072, use MRAW's **RRZO**
resized-RAW output for preview (e.g. 1920x1080 or 1280x720) and keep IMGO for
stills. RRZO is a resized RAW DMA, not a YUV ISP, but it makes CPU debayer
practical.

### 5.3 Calibration data availability

* The hardware has module EEPROMs: cam0 at i2c2 `0x50`, cam1 at i2c4 `0x51`,
  cam2 at i2c4 `0x50`, cam4 at i2c8 `0x51`
  (`cust_evergo_camera.dtsi:327-352`). The vendor `imx355` drivers read
  SPC/DCC calibration (`imx355ofilm_eeprom.c:46-125`), and the vendor S5KJN1
  driver reads a 9-byte OTP block (`s5kjn1sunny_mipi_raw.c:3946-3958`).
* Mainline has **no** NVMEM/EEPROM driver for these modules and the mainline
  sensor drivers do not read OTP. A small NVMEM driver can expose the EEPROM
  later; it is not needed for the first frame.
* The vendor camera tuning (`lib3a.ccu`, `/vendor/etc/camera`, `libcam*`) is
  proprietary and not in either tree. The prior note in
  `D:\evergo\camera-doc.md:36` says the AF/flash/LSC data is in
  `/vendor/etc/camera` with an undocumented format.
* For libcamera, generate a tuning file from your own raw captures, or start
  with generic SoftISP tuning. Do not redistribute vendor tuning blobs.
* The MT6833 `cam_cal` list only contains older sensors
  (`cam_cal_list.c:21-40`), so it does not directly cover S5KJN1/IMX355/
  OV16A1Q/OV50C40; calibration for these modules is sensor/EEPROM-specific.

---

## 6. Milestones and effort

Assumptions: one full-time experienced embedded Linux developer, device
available for experiments, vendor source usable as documentation, no TRM.
Ranges are person-weeks; double them for a part-time/new-to-MIPI developer.

| # | Milestone | Scope | Effort | Exit criterion |
|---|---|---|---:|---|
| M0 | Platform foundation | CAM/RAWA/RAWB genpd, CAMSYS/IMGSYS DT, SMI/IOMMU optional, reset, CMA pool | 3-5 | clock/power register reads match vendor; no bus hangs |
| M1 | Sensor + rails + MCLK | WL2866D regulator driver/DTS, AVDD/DVDD/DOVDD, reset, CMMCLK gate+mux, sensor graph endpoint | 2-3 | `s5kjn1` probes; test pattern streams; 24 MHz MCLK measured |
| M2 | CSI D-PHY + SENINF | reuse/adapt `phy-mtk-mipi-csi-0-5`, new SENINF subdev, lane/VC/RAW10, errors | 8-12 | CSI-2 packets received; no ECC/CRC errors; sensor test pattern reaches MRAW |
| M3 | MRAW raw capture | TG timing, IMGO DMA, vb2 buffers, SOF/DON IRQ, CMA physical addresses | 8-12 | stable RAW10 frames in `/dev/video0` |
| M4 | Userspace RAW | `v4l2-ctl`/`yavta`, debayer, first PPM/PNG | 2-3 | **first YUV frame (software)** |
| M5 | libcamera SoftISP | simple pipeline integration, controls, tuning, preview | 4-6 | `cam`/`libcamera` preview at usable resolution |
| M6 | DIP hardware YUV | DIP_A/B driver, GCE CMDQ, tpipe ABI, IOMMU/SMI, minimal R2Y tuning | 20-30 | hardware YUV frame with acceptable colour |
| M7 | 3A/AF/multi-cam/CAMSV | CCU or userspace 3A, AF, second/third sensor, CAMSV if useful | 12-20 | all cameras, AE/AWB/AF |
| M8 | Upstreaming/hardening | bindings, DT, runtime PM, suspend/resume, review | 6-10 | patches on list; suspend/resume stable |

**Cumulative:**

* first YUV via software (M0-M4): **23-35 person-weeks** (~5-8 months);
* usable libcamera preview (M0-M5): **27-41 person-weeks** (~6-9 months);
* hardware-DIP YUV (M0-M6): **47-71 person-weeks** (~11-16 months);
* full multi-camera/3A product: **~2 years**.

Vendor-port option for comparison: 8-12 person-weeks to build/boot, 20-40
more to replace the HAL, and it remains non-upstreamable.

---

## 7. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| No public SENINF/MRAW register documentation | schedule slips | use vendor `cam_regs.h` + HAL register traces from an Android device; start with sensor test pattern |
| MIPI D-PHY register layout differs from MT8365 | PHY reuse fails | compare `phy-mtk-mipi-csi-0-5-rx-reg.h` with vendor `seninf` writes; be ready to write a small MT6833 PHY driver |
| CAM power left on only by bootloader | works at boot, fails after suspend | implement CAM/RAWA/RAWB genpd early; validate with `pm_test` |
| IOMMU/SMI missing | DMA faults under memory pressure | start with CMA; add MT6833 IOMMU/larb data (mt8183/mt6893 as starting point) |
| libcamera SoftISP too slow at 12.5 MP | unusable preview | use RRZO resized RAW for preview; add a binning sensor mode; consider DIP later |
| DIP tpipe/tuning unknown | no hardware YUV | defer DIP; SoftISP first; only attempt with TRM/HAL traces |
| Sensor population varies (S5KJN1 vs OV50C40, IMX355 vs OV16A1Q) | wrong sensor driver | read EEPROM/`/proc` on device; add per-variant DT; OV50C40/OV16A1Q need new drivers |
| Legal/proprietary HAL | cannot ship | clean-room GPL implementation; no vendor blobs in upstream |
| Upstream review | long | split into small series: DT bindings, SENINF, MRAW, then DIP |

---

## 8. GPL and licensing notes

* The vendor camera kernel files are GPL-2.0: `camera_isp.c:2-9,12877`,
  `camera_dip.c:2-9,8901`, `ccu_drv.c:2-9,1578`, `wl2866d.c:949`. They may be
  used as reference and, if copied, the derivative must remain GPL-2.0
  (these headers say "version 2", i.e. GPL-2.0-only, not "or later").
* Not every vendor file is clearly licensed: the Sunny S5KJN1 sensor driver
  has no SPDX/license header at all
  (`s5kjn1sunny_mipi_raw.c:1-25`). Do not copy it; use the mainline
  `drivers/media/i2c/s5kjn1.c` instead.
* The vendor **userspace** HAL, `lib3a.ccu`, and camera tuning files are
  proprietary. They are not in either tree and cannot be redistributed.
  libcamera is LGPL-2.1-or-later, so a pipeline handler must not link
  proprietary HAL code; a clean-room implementation is required.
* Calibration/tuning data is copyrighted by MediaTek/Xiaomi. Extract it only
  from your own device for personal use; do not ship it. Generate your own
  libcamera tuning from raw captures.
* Mainline contributions should be `GPL-2.0-only`, with SPDX tags and
  original copyright notices. Do not import `mt-plat`, `ion`, `mtk_iommu_ext`
  or other vendor-only headers.
* Reverse engineering for interoperability (register tracing, HAL ABI
  observation) may be restricted by contract/EULA and is jurisdiction
  dependent; keep a clean-room record and do not copy proprietary code.

---

## 9. Open questions / data still needed

1. Is the MT6833 CSI D-PHY register-compatible with
   `phy-mtk-mipi-csi-0-5.c`? Compare the vendor `seninf`/`seninf_clk` writes
   with `phy-mtk-mipi-csi-0-5-rx-reg.h`.
2. Does CAMSV support RAW10 pass-through? If yes, it may be a simpler capture
   engine than MRAW for preview.
3. What exact MRAW CQ/DMA sequence does the vendor HAL use for S5KJN1
   4080x3072? A register trace on Android would shorten M3 substantially.
4. Are CAM/RAWA/RAWB left powered by the bootloader on evergo? Measure before
   implementing genpd.
5. Which sensor variants exist on this unit (S5KJN1 vs OV50C40, IMX355 vs
   OV16A1Q)? The vendor DTS lists alternatives.
6. Are the MT6833 IOMMU/SMI blocks compatible with `mt8183`/`mt6893` data?
7. Is there a usable tpipe for a minimal DIP R2Y path, or must it be
   reverse-engineered from the HAL?
8. Legal: may the vendor HAL be traced for interoperability? Keep clean-room
   documentation.

---

## Appendix A: primary citations

Vendor:

* `drivers/misc/mediatek/cameraisp/src/mt6853/camera_isp.c` — base/probe
  `:7126`, `of_iomap` `:7165`, clocks `:7295-7296`, MMIO mmap `:6988-7044`,
  buffer control `:3328`, CAMSV config `:12534`, license `:12877`.
* `.../inc/camera_isp.h` — bases `:42-58`, DMA enum `:315-390`, buffer struct
  `:550-559`, ioctls `:752-895`.
* `.../inc/cam_regs.h` — IMGO/RRZO/stats `:463-480`, CAMSV `:754-763`.
* `drivers/misc/mediatek/cameraisp/dip/isp_6s/camera_dip.c` — bases
  `:515-534`, cmdq include `:65`, tpipe `:485,3976`, license `:8901`.
* `.../camera_dip.h` — bases `:42-58`, ioctls `:315-348`.
* `drivers/misc/mediatek/ccu/src/mt6853/ccu_hw.c` — `lib3a.ccu` `:940`;
  `ccu_drv.c:79,1429,1578`.
* `drivers/misc/mediatek/imgsensor/src/common/v1_1/seninf.c` — mmap/ioctl
  `:200-215,350-470`; `seninf_impl.c:153-163`;
  `seninf_cfg.h:21-27` (`0x11c80000`).
* `drivers/misc/mediatek/imgsensor/src/common/v1_1/s5kjn1sunny_mipi_raw/
  s5kjn1sunny_mipi_raw.c:3946-3958` (OTP).
* `drivers/misc/mediatek/imgsensor/src/common/v1_1/imx355ofilm_mipi_raw/
  imx355ofilm_eeprom.c:46-125` (SPC/DCC).
* `drivers/misc/mediatek/cam_cal/src/mt6833/cam_cal_list.c:21-40`.
* `drivers/regulator/wl2866d.c:912-913,949`; `wl2866d.dtsi:1-50`.
* `arch/arm64/boot/dts/mediatek/cust_evergo_camera.dtsi` — rails `:298-310`,
  sensors `:318-322`, EEPROM `:327-352`.
* `/root/vendor_dtb.dts` — SENINF `:3407-3440`, CCU `:4130-4140`, IMGSYS
  `:4228-4236`, DIP_A `:4244-4300`, DIP_B `:4374-4382`, CAMSYS/CAM/CAMSV
  `:4635-4740`.
* `drivers/clk/mediatek/clk-mt6833-pg.c` — SPM `:156-165`, status
  `:410-423`, domains `:634-656`, bus protection `:374-387`.

Mainline:

* `drivers/media/platform/mediatek/Kconfig:5-9` (no ISP).
* `arch/arm64/boot/dts/mediatek/mt6833.dtsi` — GCE `:15-19,1167-1177`,
  IMGSYS `:1320-1327`, CAMSYS `:1344-1360`, IOMMU comment `:1458-1471`.
* `drivers/pmdomain/mediatek/mt6833-pm-domains.h:14-16`.
* `drivers/clk/mediatek/clk-mt6833-cam_m.c:30-47`;
  `clk-mt6833-cam_ra.c:31-33`; `clk-mt6833-cam_rb.c:31-33`;
  `clk-mt6833-imgsys1.c:31-34`; `clk-mt6833-imgsys2.c:31-36`.
* `drivers/phy/mediatek/phy-mtk-mipi-csi-0-5.c:1-120,277-279`;
  `Documentation/devicetree/bindings/phy/mediatek,mt8365-csi-rx.yaml`.
* `drivers/mailbox/mtk-cmdq-mailbox.c:851-866`.
* `drivers/iommu/mtk_iommu.c:1903-1921`; `drivers/memory/mtk-smi.c:549-560,
  844-859`.
* `arch/arm64/boot/dts/mediatek/mt6833-xiaomi-evergo.dts:1807-1858`.
* `drivers/media/i2c/s5kjn1.c:717-740,1163-1197,1324-1363,1467-1468`;
  `imx355.c:1340`; `ov8856.c:2475`; `ov02a10.c:983`; `ov02c10.c:994`.
* `arch/arm64/boot/dts/mediatek/mt8195.dtsi:749-790,2654,2751` (camera
  power/clock infrastructure but no capture nodes).
* `drivers/pinctrl/mediatek/pinctrl-mtk-mt6833.h:597-625` (CMMCLK).

---

## Appendix B: recommended immediate next steps (no device access)

1. Write the DT bindings and skeleton for `mediatek,mt6833-seninf` and
   `mediatek,mt6833-mraw`; model them on `phy-mtk-mipi-csi-0-5` and the
   vendor register headers.
2. Add the CAM/RAWA/RAWB genpd domains and the missing sensor supplies to the
   evergo DTS; add the sensor `port`/`endpoint`.
3. Implement the SENINF subdev against the sensor test pattern; then MRAW
   IMGO capture into CMA buffers.
4. Bring up `v4l2-ctl` RAW capture and an offline debayer. That is the first
   YUV frame.
5. Only after that, decide whether SoftISP is sufficient or whether the DIP
   and hardware 3A are required.
