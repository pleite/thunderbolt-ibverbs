{ lib }:

let
  linuxPerfModuleArgs = {
    profile = "linux_perf";
    compat = "off";
    tbnet = "prefer_rdma";
    tbnet_identity = "off";
    roce_netdev = "br0.lan";
    lanes = "2";
    bind_services = "1";
    allocate_rings = "1";
    start_rings = "1";
    negotiate_native = "1";
    enable_tunnels = "1";
    native_data = "1";
    apple_data = "0";
    register_verbs = "1";
    # Tuning-sweep recommended defaults (see bench/results/strix-2p-noiommu-2x40g/tuning.md
    # and docs/TUNING.md for the rationale).
    zcopy_min_bytes = "4096";            # +17% BW at 1 MiB; no cost below threshold
    native_fragment_striping = "0";      # enable to "1" on ≥4-rail topologies for ~7% BW gain
    nhi_interrupt_throttle_ns = "50000"; # +10% BW at 50 µs; set 0 for latency-critical paths
  };

  renderModuleArgs = attrs:
    lib.concatStringsSep " "
      (lib.mapAttrsToList (name: value: "${name}=${toString value}") attrs);
in
rec {
  hosts = {
    # Override `address` / `lanIp` for your network. The values below are
    # placeholders; the bench just needs ssh-reachable hostnames or IPs.
    strix-1 = {
      address = "192.168.1.10";
      lanIp = "192.168.1.10";
      role = "head";
      gpu = "gfx1151";
    };
    strix-2 = {
      address = "192.168.1.11";
      lanIp = "192.168.1.11";
      role = "worker";
      gpu = "gfx1151";
    };
  };

  moduleProfiles = {
    linuxPerf = {
      name = "linux_perf";
      args = linuxPerfModuleArgs;
      argsString = renderModuleArgs linuxPerfModuleArgs;
      nativeBackend = "native";
      rc = true;
      uc = true;
      apple = false;
    };
  };

  topologies =
    let
      strixPair = {
        name = "strixPair";
        head = "strix-1";
        worker = "strix-2";
        headIp = hosts.strix-1.lanIp;
        workerIp = hosts.strix-2.lanIp;
        controlIfname = "br0.lan";
        socketIfname = "br0.lan";
        rdmaDev = "usb4_rdma0";
        rdmaHca = "usb4_rdma0";
        gidIndex = 1;
        moduleProfile = moduleProfiles.linuxPerf;
        expect = {
          nativeRails = 2;
          nativeRailSpeed = "20Gb/s";
        };
      };
    in
    {
      inherit strixPair;
      strixPairFourRail = strixPair // {
        name = "strixPairFourRail";
        expect = {
          nativeRails = 4;
          nativeRailSpeed = "20Gb/s";
        };
      };
    };
}
