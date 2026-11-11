# Shared device kernel

This kernel supports OnePlus Pad 2 Pro (OPD2413 / erhai / SM8750) across Linux distributions.
Keep drivers, device trees and hardware configuration independent of distribution tooling.
Distribution configuration fragments, packages, initramfs, services and boot installation belong
in their respective distribution projects. Do not require Nix or NixOS files to build this tree.
Document standard Kbuild usage in Documentation/arch/arm64/erhai.rst.
Retain upstream source copyright and license notices.
