OnePlus Pad 2 Pro (OPD2413 / erhai)
=================================

This device kernel is shared by Linux distributions. Its original base is
yzddmr6/linux-sm8750-erhai commit
870167fad062e74f6de27e76db84d17cc49d8b84 (Linux 7.2.15), with upstream updates
merged through fb18d39b1baeeac3698a8786c134a326235ead9f on erhai/v0.7.
Existing source copyright and license notices are retained.

Device support
--------------

* ath12k routes OPD2413 / WCN7860 firmware, including board API fallbacks,
  to ath12k/WCN7860/hw2.0/oneplus/opd2413/. The change is ported from
  ArmadaOS touch18's upstream-to-touch18.patch (SHA-256
  7520350aab464ec97c4945657a32fdf6725db342b0fde49959ad0bdcbd782fc1).
* The board device tree includes oneplus,opd2413 as well as oneplus,erhai
  and qcom,sm8750, so firmware selection works without a distribution's
  device identity overlay. The saved qcom,mainline-compatible list retains
  the same identifiers when QCOM_ABL_ROOT_IDENTITY restores the root after
  vendor ABL overlays.
* CPS8601 requests optional firmware, preserves errors other than missing
  firmware and retains the factory firmware when no replacement is supplied.

Building with standard Kbuild
----------------------------

With an AArch64-capable LLVM toolchain and the normal kernel build
dependencies installed, run from the source root::

    mkdir -p build
    make O=build ARCH=arm64 LLVM=1 defconfig
    scripts/kconfig/merge_config.sh -m -O build build/.config \
        arch/arm64/configs/sm8750-peripherals.config \
        arch/arm64/configs/erhai-firstboot.config
    make O=build ARCH=arm64 LLVM=1 olddefconfig
    make O=build ARCH=arm64 LLVM=1 -j"$(nproc)" Image dtbs modules

For GCC cross compilation, replace LLVM=1 with
CROSS_COMPILE=aarch64-linux-gnu- in each make command.
The platform and peripheral fragments are shared starting points, including
RAM diagnostics; each distribution must review and extend the resulting
configuration for its own userspace, security policy and root filesystem.
Merge any distribution fragment last, before olddefconfig. Set LOCALVERSION
as appropriate and install matching modules built with the same configuration
and release. No Nix tooling or distribution-specific configuration file is
required by this tree.

The outputs include build/arch/arm64/boot/Image,
build/arch/arm64/boot/dts/qcom/sm8750-erhai.dtb and the modules in build/.
Firmware binaries, per-device data, initramfs, root filesystems and boot
images are maintained separately. Each distribution supplies the matching
firmware and its own packaging, initramfs and boot integration.

Bluetooth identity
------------------

OPD2413's WCN7860 firmware does not supply a usable per-unit public address
by itself. The Android 16 vendor stack obtains the factory address from an
OEM NVRAM service, backed by the redundant ``nonmodemst1`` and ``nonmodemst2``
GPT partitions, and supplies it to the controller. It is not supplied in the
stock device tree on the tested tablet.

Distributions can initialize the controller through the standard Linux
Bluetooth MGMT interface before BlueZ starts. The per-device factory record
is bound to the SoC serial exposed by the standard ``socinfo`` driver; keep
``CONFIG_QCOM_SOCINFO`` enabled and load ``socinfo`` before userspace factory
identity extraction. Do not hard-code an individual tablet's address in the
shared DTS, kernel or firmware package, and do not erase the factory partitions.
Factory data extraction, integrity checks, state files and service packaging
belong in distribution userspace. This identity initialization does not replace
the UART, power sequencing or firmware setup performed by the kernel driver.

Display precision and HDR
-------------------------

The XN242 / NT36536 panel uses RGB888, DSC 1.2 and 8 bits per component,
matching the stock device-tree configuration. A 10-bit scanout framebuffer
is independent of this link format. Do not change the DSC depth or DSI
format based on another tablet's panel configuration.

The optional ``qcom,dpu-8bpc-dither`` panel property enables the existing DPU
ordered output dither matrix at 8 bpc. The driver leaves temporal dithering
disabled. This is adapted from ArmadaOS patch 0049 by enjihn
<150061999+enjihn@users.noreply.github.com>; source license notices remain.
The board opts in to retain more information when quantizing high precision
composition for the stock 8-bit link. This does not implement the panel's
advertised FRC algorithm, a native 10-bit link, or BT.2100/PQ output.

Run the focused policy and bridge-reference check from the source root::

    python3 tools/testing/selftests/drm/dpu-output-dither-test.py

Native HDR additionally requires verified panel EOTF, primaries, luminance
and an actual color conversion path through the display engine or compositor.
No synthetic HDR connector metadata is supplied by this patch. The equivalent
DPU change has been tested on this tablet in the NixOS ``color1`` kernel.
That validated kernel must remain available until a new build from this
shared tree passes hardware testing; source integration alone does not
validate a replacement kernel.

OPD2413 OEM Bluetooth bootstrap
------------------------------

The board Bluetooth node selects ``oneplus,opd2413-brahma20-bt`` at 8 Mbps.
This is an explicit OEM protocol choice, separate from generic
``qcom,wcn7860-bt`` support. The shared WCN PMU owns the supplies and BT_EN;
do not add another enable GPIO or override the firmware-name property.

The bootstrap first loads the Peripheral patch/NVM and TME-L image, then
activates Bluetooth and downloads its patch and board-selected NVM. Required
files under ``qca/oneplus/opd2413/`` are ``brhperifw20.mbn``,
``brhperinv20.bin``, ``tmel_peach_20.elf``, ``brhbtfw20.mbn`` and
``brhbtnv20.bin`` (or the measured board-ID variant). Firmware absence or
unexpected ROM identity fails setup; this path does not silently run ROM
firmware. Distributions must package the original-device firmware separately.
The hardware's factory Bluetooth address is still provisioned by userspace.

The protocol and board UART RX/baud/CTS handling are adapted from ArmadaOS
``devices/oneplus-pad2pro/kernel/current/final-modules`` at reference
``937d88a2c28a67153d572bcf7f89049eaada93f2`` (GPL-2.0-only), whose implementation
was verified against the OPD2413 Android bootstrap. Original driver copyright
notices remain. This port preserves the newer base's IBS power-off fixes,
serial suspend/resume implementation and generic WCN7860 support. Full kernel
bring-up and device acceptance are required before promoting this candidate.

Run the protocol parser and transport failure checks from the source root::

    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
      tools/testing/selftests/bluetooth/brahma-peripheral-test.c -o /tmp/brahma-test
    /tmp/brahma-test
