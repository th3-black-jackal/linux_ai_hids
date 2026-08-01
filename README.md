# HIDS — ML host-based intrusion detection for embedded Linux

A C++ Linux service that runs the exported MLP over live host behavior and
publishes detections to an MQTT (Mosquitto) broker on a remote server. Trained
on the DFL IoT crowdsensing dataset; targets are Raspberry Pi 3B+/4 and
Beaglebone Black (32-bit armhf userspace, hence the `*32` syscall names in
FEATURE_ORDER).

Per-window pipeline:

    collect (perf tracepoints + /proc/diskstats + resource)
      -> aggregate into FEATURE_ORDER
      -> normalize (baked-in min/max) + MLP inference
      -> debounce
      -> publish JSON to MQTT   hids/<host>/events   and   hids/<host>/alerts

No Wazuh integration and no desktop app in this build (telemetry is MQTT-only).

## Layout

    CMakeLists.txt                 builds hids (service) + hids_replay (Tier-0 tool)
    cmake/toolchain-armhf.cmake    cross-compile to 32-bit armhf
    config/hids.conf.example       sample configuration
    systemd/hids.service           unit file
    include/hids/
      model.hpp                    .bin loader + scaler + MLP forward     [VALIDATED]
      model_meta.hpp               generated: FEATURE_ORDER, CLASS_NAMES
      meta.hpp                     std::string accessors over meta arrays
      collector.hpp                ICollector interface
      feature_routing.hpp          FEATURE_ORDER name -> collector/tracepoint
      perf_collector.hpp/.cpp      perf tracepoint counters (syscalls, kernel/fs)
      proc_collector.hpp/.cpp      /proc/diskstats-derived I/O features
      resource_collector.hpp       seconds_RES_data  [PARITY-UNCONFIRMED, see file]
      window_aggregator.hpp        merged map -> FEATURE_ORDER vector
      inference_engine.hpp         normalize + predict wrapper
      debouncer.hpp                edge-triggered alert confirmation
      config.hpp / src/config.cpp  key=value config loader
      mqtt_publisher.hpp/.cpp      dependency-free MQTT 3.1.1 publisher (QoS 0)
      hids_service.hpp/.cpp        orchestrator + per-window loop
      detection_event.hpp          event struct + JSON
    src/main.cpp                   entrypoint (--config/--check/--dump-features)
    src/replay_main.cpp            Tier-0 offline replay / agreement tool [VALIDATED]
    tools/                         make_ref_model.py, bin_oracle.py (NumPy reference)
    scripts/                       tier1-qemu-user.sh, run-qemu-armhf.sh

### Validation status

- Inference path (model.hpp + replay): validated on x86, on armhf under
  qemu-user, and against an independent NumPy oracle.
- Service pipeline: builds on x86 and cross-builds to armhf; MQTT publish of
  events + alerts verified end-to-end against a live Mosquitto broker.
- NOT yet validated: live tracepoint resolution and, above all, FEATURE PARITY
  (that the live 31-vector matches the training distribution). Must be checked
  on the real armhf target / Tier-2 qemu-system. `seconds_RES_data` is a
  best-guess whose training semantics are unconfirmed.

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
    # armhf:
    cmake -B build-armhf -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armhf.cmake
    cmake --build build-armhf -j

## Run

    cp config/hids.conf.example /etc/hids/hids.conf   # edit mqtt_host etc.
    cp your-model.bin /etc/hids/model.bin
    sudo ./build/hids --config /etc/hids/hids.conf     # needs perf privilege

    ./build/hids --config hids.conf --check            # print routing + report
                                                       # unresolved tracepoints
    ./build/hids --config hids.conf --dump-features     # + CSV of live vectors

Subscribe to telemetry from anywhere pointed at the broker:

    mosquitto_sub -h <broker> -t 'hids/#' -v

## Testing tiers

Tier 0 (offline, any arch) — prove the C++ forward pass reproduces the model:

    ./build/hids_replay  model.bin  dataset/test_split/          # accuracy
    ./build/hids_replay  model.bin  dataset/ --dump > cpp.txt    # port check:
    python3 tools/bin_oracle.py model.bin dataset/ --dump > o.txt
    diff cpp.txt o.txt                                           # must be empty

Tier 1 (armhf under qemu-user) — cross-build + ABI/float check:

    ./scripts/tier1-qemu-user.sh  model.bin  dataset/

Tier 2 (armhf under qemu-system) — the only place collectors are meaningful:
boot a 32-bit ARM guest (scripts/run-qemu-armhf.sh), then on the guest run
`hids --check` to confirm the 31 tracepoints resolve, and `--dump-features` to
overlay live vectors on the training distribution before trusting detections.
