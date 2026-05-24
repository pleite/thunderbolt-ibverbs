{
  description = "Thunderbolt/USB4 host-to-host RDMA verbs kernel module";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      lib = nixpkgs.lib;
      optionalKernelPatches = [
        {
          name = "thunderbolt-nhi-ring-throttling-helper";
          patch = ./patches/linux/0001-thunderbolt-nhi-add-per-ring-interrupt-throttling-helper.patch;
        }
      ];
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f:
        lib.genAttrs systems (system:
          f (import nixpkgs { inherit system; }));
    in {
      packages = forAllSystems (pkgs:
        let
          module = pkgs.linuxPackages.callPackage ./nix/module.nix { };
        in {
          default = module;
          thunderbolt-ibverbs = module;
        });

      overlays.default = final: prev: {
        thunderbolt-ibverbs =
          final.linuxPackages.callPackage ./nix/module.nix { };
      };

      lib.kernelPatches = optionalKernelPatches;

      legacyPackages = forAllSystems (_pkgs: {
        kernelPatches = optionalKernelPatches;
      });

      nixosModules.default = { config, lib, ... }:
        let
          cfg = config.hardware.thunderbolt-ibverbs;
          module =
            config.boot.kernelPackages.callPackage ./nix/module.nix { };
        in {
          options.hardware.thunderbolt-ibverbs.enable =
            lib.mkEnableOption "the thunderbolt_ibverbs kernel module";

          config = lib.mkIf cfg.enable {
            boot.extraModulePackages = [ module ];
            boot.kernelModules = [ "thunderbolt_ibverbs" ];
          };
        };
    };
}
