#!/usr/bin/env bash
# Memory for template-heavy compilation, as every stage of both workflows
# sets it up (their setup actions run this first): the runner's unused SDKs
# removed for room, then compressed swap in memory (zram) used first, and a
# swap file on the disk after it (32 GiB, or MUX_SWAP_GB).
#
# zram: pages swapped out are compressed (zstd, about 3:1 on a compiler's
# heap) into memory instead of written to the disk -- far faster to swap in
# than the file, so a unit just over the runner's memory goes on at memory
# speed rather than crawling on the disk. Its size is the runner's memory
# (what it holds compressed takes about a third of that). Where the kernel
# has no zram module (it is in linux-modules-extra on Ubuntu's Azure
# kernels), the file alone is the swap, as before.
set -euo pipefail
sudo rm -rf /usr/local/lib/android /usr/share/dotnet /opt/ghc /usr/local/.ghcup
sudo swapoff -a || true
sudo rm -f /mnt/mux.swap

zram() {
  sudo modprobe zram 2>/dev/null ||
    { sudo apt-get install -y "linux-modules-extra-$(uname -r)" >/dev/null && sudo modprobe zram; } || return 1
  local memory device
  memory=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
  device=$(sudo zramctl --find --size "${memory}KiB" --algorithm zstd) || return 1
  sudo mkswap "$device" >/dev/null
  # Taken before the file: the higher priority is used first.
  sudo swapon --priority 100 "$device"
  # Swapping to memory is cheap: swapped to rather than dropping the page
  # cache, and a page at a time (no read-ahead, which only pays on a disk).
  sudo sysctl -q -w vm.swappiness=180 vm.page-cluster=0
}
zram || echo "zram unavailable here: swap on the disk only"

# 32 GiB by default; a stage that needs more says so.
sudo fallocate -l "${MUX_SWAP_GB:-32}G" /mnt/mux.swap
sudo chmod 600 /mnt/mux.swap
sudo mkswap /mnt/mux.swap >/dev/null
sudo swapon --priority 10 /mnt/mux.swap
swapon --show
free -h
df -h / /mnt
