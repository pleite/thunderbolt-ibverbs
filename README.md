# Thunderbolt IB Verbs

This repository contains an out-of-tree Thunderbolt/USB4 host-to-host RDMA
verbs kernel module. It builds against stock kernels; the kernel patch in
`patches/linux/` is optional and only enables lower-level NHI interrupt
throttling control.

## Build The Module

```sh
make KDIR=/lib/modules/$(uname -r)/build
```

With Nix:

```sh
nix build .#thunderbolt-ibverbs
```

On NixOS, import the module and enable it:

```nix
{
  inputs.thunderbolt-ibverbs.url = "github:hellas-ai/thunderbolt-ibverbs";

  outputs = { nixpkgs, thunderbolt-ibverbs, ... }: {
    nixosConfigurations.host = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        thunderbolt-ibverbs.nixosModules.default
        {
          hardware.thunderbolt-ibverbs.enable = true;
        }
      ];
    };
  };
}
```

## Optional Kernel Patch

`patches/linux/0001-thunderbolt-nhi-add-per-ring-interrupt-throttling-helper.patch`
adds `tb_ring_throttling()` to the Thunderbolt core. The module detects this
symbol at load time. If the patch is not present, the module still loads and
uses the stock NHI interrupt behavior.

The patch is useful when tuning:

```text
nhi_interrupt_throttle_ns=<ns>
```

`0` disables throttling for the module's data rings. Non-zero values request
that interrupt moderation interval in nanoseconds. The kernel helper accepts
the setting only while each ring is stopped.

### NixOS Kernel Patch

The flake exposes the patch in `lib.kernelPatches` and
`legacyPackages.${system}.kernelPatches`.

```nix
{ pkgs, lib, inputs, ... }:
let
  linuxPackagesTbv = pkgs.linuxPackages_latest.extend (_self: super: {
    kernel = super.kernel.override {
      kernelPatches =
        (super.kernel.kernelPatches or [])
        ++ inputs.thunderbolt-ibverbs.lib.kernelPatches;
    };
  });
in {
  imports = [
    inputs.thunderbolt-ibverbs.nixosModules.default
  ];

  boot.kernelPackages = linuxPackagesTbv;
  hardware.thunderbolt-ibverbs.enable = true;

  boot.extraModprobeConfig = ''
    options thunderbolt_ibverbs nhi_interrupt_throttle_ns=0
  '';
}
```

Rebuild and reboot into the patched kernel:

```sh
sudo nixos-rebuild boot --flake .#host
sudo reboot
```

### Generic Distro Kernel Source

For a locally built Debian, Ubuntu, Fedora, Arch, or upstream kernel tree:

```sh
cd linux
git am /path/to/thunderbolt-ibverbs/patches/linux/0001-thunderbolt-nhi-add-per-ring-interrupt-throttling-helper.patch
make olddefconfig
make -j"$(nproc)" bindeb-pkg
```

Install the generated kernel packages with the distro package manager, reboot
into that kernel, then build/install this module through DKMS or `make`.

Fedora's kernel dist-git works similarly: apply the patch to the kernel source
tree or add it to the RPM spec's patch list, build the kernel RPMs, install
them with `dnf`, reboot, then build this module against the matching headers.
