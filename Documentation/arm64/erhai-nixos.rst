NixOS on OnePlus Pad 2 Pro (OPD2413)
===================================

This branch preserves the Linux 7.2.15 device baseline from
yzddmr6/linux-sm8750-erhai commit
870167fad062e74f6de27e76db84d17cc49d8b84 and includes the NixOS port's
local changes. Existing source copyright and license notices are retained.

* ath12k: route OPD2413 / WCN7860 firmware, including board API fallbacks,
  to ath12k/WCN7860/hw2.0/oneplus/opd2413/. The change is ported from
  ArmadaOS touch18's upstream-to-touch18.patch (SHA-256
  7520350aab464ec97c4945657a32fdf6725db342b0fde49959ad0bdcbd782fc1).
* CPS8601: request optional firmware without the generic missing-file
  warning, preserve other errors, and retain factory firmware when absent.
  MODULE_VERSION remains nixos-optional-fw1.
* arch/arm64/configs/erhai-nixos.config: the NixOS configuration fragment,
  including the validated built-in Wi-Fi stack, storage, namespaces,
  cgroups, firewall modules, zram and USB NCM.

The NixOS build merges arm64 defconfig, sm8750-peripherals.config,
erhai-firstboot.config and erhai-nixos.config, then runs olddefconfig and
checks the requested settings. Do not apply the NixOS project's old
ath12k or CPS8601 patches again when building this branch.

The release suffix retains the original baseline identifier for module
compatibility; the exact source revision is pinned by the NixOS flake lock.

Firmware binaries, per-device data, Nix store contents and boot images
are not part of this source repository. The OPD2413 OEM firmware archive
remains a separate hash-pinned NixOS input. The complete NixOS hardware
module also supplies the OPD2413 DT identity overlay, initrd firmware,
USB management and kShimC integration; those are not kernel-source patches.
