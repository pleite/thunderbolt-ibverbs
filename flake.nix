{
  description = "Thunderbolt/USB4 host-to-host RDMA verbs kernel module";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    linux-src = {
      # Thunderbolt maintainer tree carrying the USB4STREAM/XDomain base series.
      url = "git+https://git.kernel.org/pub/scm/linux/kernel/git/westeri/thunderbolt.git?ref=refs/heads/next&shallow=1";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, linux-src }:
    let
      lib = nixpkgs.lib;
      thunderboltKernelPatches = import ./kernel-workflow/patches;
      systems = [
        "x86_64-linux"
      ];
      forAllSystems = f:
        lib.genAttrs systems (system:
          f (import nixpkgs { inherit system; }));
      linuxSrcMakefile = builtins.readFile "${linux-src}/Makefile";
      linuxSrcMakeVars =
        let
          lines = lib.splitString "\n" linuxSrcMakefile;
          readVar = name:
            let
              matches = lib.filter (match: match != null)
                (map (line: builtins.match "${name}[[:space:]]*=[[:space:]]*(.*)" line) lines);
            in
            if matches == [ ] then
              throw "could not read ${name} from Linux Makefile"
            else
              lib.head (lib.head matches);
        in
        {
          version = readVar "VERSION";
          patchlevel = readVar "PATCHLEVEL";
          sublevel = readVar "SUBLEVEL";
          extraversion = readVar "EXTRAVERSION";
        };
      linuxSrcVersion =
        "${linuxSrcMakeVars.version}.${linuxSrcMakeVars.patchlevel}.${linuxSrcMakeVars.sublevel}${linuxSrcMakeVars.extraversion}";
      mkThunderboltKernel = pkgs:
        let
          testingKernel = pkgs.linuxPackages_testing.kernel;
          kernelPatches =
            (testingKernel.passthru.kernelPatches or [ ])
            ++ thunderboltKernelPatches;
        in
        testingKernel.override {
          argsOverride = {
            pname = "linux-thunderbolt";
            version = linuxSrcVersion;
            modDirVersion = linuxSrcVersion;
            src = linux-src;
            inherit kernelPatches;
          };
        };
      mkThunderboltLinuxPackages = pkgs:
        pkgs.linuxPackagesFor (mkThunderboltKernel pkgs);
      mkScriptSyntaxCheck = pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "thunderbolt-ibverbs-script-syntax";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.bash ];

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            bash -n \
              tools/ci/distro-build.sh \
              tools/ci/vm-guest-smoke.sh \
              tools/ci/vm-smoke.sh
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            runHook postInstall
          '';
        };
      mkProtoSmoke = pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "thunderbolt-ibverbs-proto-smoke";
          version = "0.1.0";
          src = ./.;

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            $CC -std=c11 -Wall -Wextra -Werror -I. \
              tools/ci/proto-smoke.c \
              -o tbv-proto-smoke
            ./tbv-proto-smoke
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            runHook postInstall
          '';
        };
      mkDockerDistroBuild = pkgs: { name, image }:
        pkgs.stdenv.mkDerivation {
          pname = "thunderbolt-ibverbs-distro-${name}";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [
            pkgs.bash
            pkgs.docker-client
          ];

          requiredSystemFeatures = [ "docker" ];
          __noChroot = true;
          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            export HOME="$TMPDIR/home"
            mkdir -p "$HOME"
            export DOCKER="${pkgs.docker-client}/bin/docker"
            bash tools/ci/distro-build.sh ${lib.escapeShellArg image}
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            runHook postInstall
          '';
        };
      mkNixosVmSmoke = pkgs:
        let
          module = pkgs.linuxPackages.callPackage ./nix/module.nix { };
        in
        pkgs.testers.runNixOSTest {
          name = "thunderbolt-ibverbs-vm-smoke";

          nodes.machine = { pkgs, ... }: {
            boot.extraModulePackages = [ module ];
            environment.systemPackages = [
              pkgs.kmod
              pkgs.rdma-core
            ];
          };

          testScript = ''
            machine.start()
            machine.wait_for_unit("multi-user.target")
            machine.succeed("modprobe thunderbolt_ibverbs profile=linux_perf bind_services=0 register_verbs=1")
            machine.succeed("grep '^thunderbolt_ibverbs ' /proc/modules")
            machine.succeed("modinfo thunderbolt_ibverbs")
            machine.succeed("rdma link show")
            machine.succeed("ibv_devices")
            machine.succeed("rmmod thunderbolt_ibverbs")
          '';
        };
      mkVerbsSmokeBuild = pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "thunderbolt-ibverbs-verbs-smoke-build";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.pkg-config ];
          buildInputs = [ pkgs.rdma-core ];

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            $CC -std=c11 -Wall -Wextra -Werror \
              tools/ci/verbs-smoke.c \
              $(pkg-config --cflags --libs libibverbs) \
              -o tbv-verbs-smoke
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            runHook postInstall
          '';
        };
    in
    {
      packages = forAllSystems (pkgs:
        let
          module = pkgs.linuxPackages.callPackage ./nix/module.nix { };
          thunderboltKernel = mkThunderboltKernel pkgs;
          thunderboltLinuxPackages = mkThunderboltLinuxPackages pkgs;
          moduleForThunderboltKernel =
            thunderboltLinuxPackages.callPackage ./nix/module.nix { };
        in
        {
          default = module;
          linux-thunderbolt = thunderboltKernel;
          linux-thunderbolt-dev = thunderboltKernel.dev;
          linux-thunderbolt-modules = thunderboltKernel.modules;
          thunderbolt-ibverbs = module;
          thunderbolt-ibverbs-linux-thunderbolt = moduleForThunderboltKernel;
        });

      checks = forAllSystems (pkgs: {
        thunderbolt-ibverbs =
          self.packages.${pkgs.stdenv.hostPlatform.system}.thunderbolt-ibverbs;
        script-syntax = mkScriptSyntaxCheck pkgs;
        proto-smoke = mkProtoSmoke pkgs;
        verbs-smoke-build = mkVerbsSmokeBuild pkgs;
      });

      hydraJobs = forAllSystems (pkgs: {
        thunderbolt-ibverbs =
          self.packages.${pkgs.stdenv.hostPlatform.system}.thunderbolt-ibverbs;
        linux-thunderbolt =
          self.packages.${pkgs.stdenv.hostPlatform.system}.linux-thunderbolt;
        linux-thunderbolt-dev =
          self.packages.${pkgs.stdenv.hostPlatform.system}.linux-thunderbolt-dev;
        linux-thunderbolt-modules =
          self.packages.${pkgs.stdenv.hostPlatform.system}.linux-thunderbolt-modules;
        thunderbolt-ibverbs-linux-thunderbolt =
          self.packages.${pkgs.stdenv.hostPlatform.system}.thunderbolt-ibverbs-linux-thunderbolt;
        checks = self.checks.${pkgs.stdenv.hostPlatform.system};
        vm-smoke.nixos = mkNixosVmSmoke pkgs;
        distro = {
          debian-sid = mkDockerDistroBuild pkgs {
            name = "debian-sid";
            image = "debian:sid";
          };
          fedora-rawhide = mkDockerDistroBuild pkgs {
            name = "fedora-rawhide";
            image = "fedora:rawhide";
          };
          archlinux-latest = mkDockerDistroBuild pkgs {
            name = "archlinux-latest";
            image = "archlinux:latest";
          };
        };
      });

      overlays.default = final: prev: {
        thunderbolt-ibverbs =
          final.linuxPackages.callPackage ./nix/module.nix { };
      };

      lib.kernelPatches = thunderboltKernelPatches;

      legacyPackages = forAllSystems (_pkgs: {
        kernelPatches = thunderboltKernelPatches;
      });

      nixosModules.default = { config, lib, ... }:
        let
          cfg = config.hardware.thunderbolt-ibverbs;
          module =
            config.boot.kernelPackages.callPackage ./nix/module.nix { };
        in
        {
          options.hardware.thunderbolt-ibverbs.enable =
            lib.mkEnableOption "the thunderbolt_ibverbs kernel module";

          config = lib.mkIf cfg.enable {
            boot.extraModulePackages = [ module ];
            boot.kernelModules = [ "thunderbolt_ibverbs" ];
          };
        };
    };
}
