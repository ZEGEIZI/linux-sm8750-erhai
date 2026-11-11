OnePlus Pad 2 Pro (OPD2413 / erhai)
=================================

This device kernel is shared by Linux distributions. It is based on
yzddmr6/linux-sm8750-erhai commit
870167fad062e74f6de27e76db84d17cc49d8b84 (Linux 7.2.15).
Existing source copyright and license notices are retained.

Device support
--------------

* ath12k routes OPD2413 / WCN7860 firmware, including board API fallbacks,
  to ath12k/WCN7860/hw2.0/oneplus/opd2413/. The change is ported from
  ArmadaOS touch18's upstream-to-touch18.patch (SHA-256
  7520350aab464ec97c4945657a32fdf6725db342b0fde49959ad0bdcbd782fc1).
* The board device tree includes oneplus,opd2413 as well as oneplus,erhai
  and qcom,sm8750, so firmware selection works without a distribution's
  device identity overlay.
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
