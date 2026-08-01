#!/usr/bin/env bash
# Boot a 32-bit ARM (armhf) Linux guest under QEMU to test the HIDS collectors
# on a kernel that behaves like the Raspberry Pi 3B+ 32-bit target: real perf
# tracepoints, the sys_enter_geteuid32-style syscall names, and /proc/diskstats.
#
# This is pure TCG emulation on an x86 host (no KVM for ARM on Intel), so it is
# faithful for FUNCTIONALITY, not for timing or hardware PMU counters.
#
# ---------------------------------------------------------------------------
# Prereqs on the x86 host (Debian/Ubuntu):
#   sudo apt install qemu-system-arm qemu-user-static
#
# You need an armhf kernel + rootfs. Easiest reproducible option is a Debian
# armhf install under -M virt. Grab the installer kernel/initrd:
#   KERNEL=vmlinuz-*-armmp   INITRD=initrd.img   (Debian armhf, -M virt capable)
# and a qcow2 rootfs you have installed into, or a prebuilt armhf cloud image.
# ---------------------------------------------------------------------------
set -euo pipefail

KERNEL="${KERNEL:?set KERNEL=/path/to/armhf/vmlinuz}"
ROOTFS="${ROOTFS:?set ROOTFS=/path/to/armhf/rootfs.qcow2}"
INITRD="${INITRD:-}"                 # optional
PROJECT_DIR="${PROJECT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
MEM="${MEM:-1024}"
SMP="${SMP:-2}"

INITRD_ARG=()
[[ -n "$INITRD" ]] && INITRD_ARG=(-initrd "$INITRD")

# The project is shared into the guest read-only over 9p/virtfs so you can
# build in-guest or copy the cross-built binary across. Mount in the guest with:
#   mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt
exec qemu-system-arm \
  -M virt -cpu cortex-a15 -smp "$SMP" -m "$MEM" \
  -kernel "$KERNEL" "${INITRD_ARG[@]}" \
  -append "root=/dev/vda2 rw console=ttyAMA0 loglevel=7" \
  -drive if=none,file="$ROOTFS",format=qcow2,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -netdev user,id=net0,hostfwd=tcp::5522-:22 \
  -device virtio-net-device,netdev=net0 \
  -fsdev local,id=fsdev0,path="$PROJECT_DIR",security_model=none,readonly=on \
  -device virtio-9p-device,fsdev=fsdev0,mount_tag=hostshare \
  -nographic

# ---------------------------------------------------------------------------
# Inside the guest, once booted (root shell):
#
#   # perf tracepoints need privilege; run the service as root, or:
#   echo -1 > /proc/sys/kernel/perf_event_paranoid
#
#   # confirm tracefs is present (collector reads tracepoint ids from here):
#   ls /sys/kernel/tracing/events/syscalls/ | head
#
#   # THE parity check -- these must exist on a 32-bit kernel or the model is
#   # fed zeros for those columns:
#   for t in geteuid32 getegid32 setgroups32 statfs64 dup2 pipe2 prlimit64; do
#     test -d /sys/kernel/tracing/events/syscalls/sys_enter_$t \
#       && echo "OK   $t" || echo "MISS $t"
#   done
#
#   # kernel/fs tracepoints used by the collector:
#   for t in block/block_unplug ext4/ext4_ext_rm_leaf jbd2/jbd2_handle_extend \
#            ext4/ext4_da_update_reserve_space ext4/ext4_ext_remove_space_done \
#            writeback/sb_clear_inode_writeback; do
#     test -d /sys/kernel/tracing/events/$t && echo "OK   $t" || echo "MISS $t"
#   done
#
#   # note: under -M virt the disk is 'vda', not 'mmcblk0' -- good check that
#   # ProcCollector scans /proc/diskstats instead of hardcoding a device name.
#   cat /proc/diskstats
#
# Then build in-guest (apt install g++ cmake make) or run the cross-built
# binary from /mnt, pointing it at model.bin. Live counts will be low/benign,
# so expect 'normal'; to exercise malware classes, use Tier-0 replay instead.
# ---------------------------------------------------------------------------
