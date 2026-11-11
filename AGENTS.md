# Shared device kernel

This kernel supports OnePlus Pad 2 Pro (OPD2413 / erhai / SM8750) across Linux distributions.
Keep drivers, device trees and hardware configuration independent of distribution tooling.
Distribution configuration fragments, packages, initramfs, services and boot installation belong
in their respective distribution projects. Do not require Nix or NixOS files to build this tree.
Document standard Kbuild usage in Documentation/arch/arm64/erhai.rst.
Retain upstream source copyright and license notices.

This is a personal downstream fork. Use erhai-personal for ongoing development and merge
updates from upstream/erhai/v0.7 without rewriting published history. Name any new branches
for their project purpose; do not add an agent-name prefix. Do not open upstream pull requests
unless explicitly requested. The upstream contribution workflow in .github/CONTRIBUTING.md
applies when preparing an upstream contribution; personal downstream integration retains
the distribution boundaries above. Preserve validated kernel inputs until device acceptance.
Verify affected local behavior after merges and distinguish offline checks from hardware tests.
