# Memory target

The target for the supplied Matrix workload is **50 MiB at peak**. The latest
Massif report before this batch measured **108.04 MiB peak allocated heap**.
The target has not yet been verified with a new profile. Massif's heap total
and process resident memory are different measurements; report both when
checking total client memory.

The memory reductions in this batch are:

- Load shaping tables from the typeface on demand instead of retaining whole
  font files, including unused emoji bitmap tables, in HarfBuzz.
- Keep at most 20 protocol timeline events per room. Apply state and redactions
  while enforcing the bound before copying events, including during startup.
- Request one latest message for background sliding-sync rooms; the open room
  still requests 50. Older messages remain available through history paging.
- Serialize sync checkpoints one room at a time instead of copying the entire
  account's typed state before writing its JSON.
- Release inactive message vectors' backing allocations when trimming history.
- Bound the text cache to two generations of 1,024 entries, width cache to
  2,048 entries, and word cache to 2,048 entries per face.
- Default to 500 cached messages and 4 MiB of cached images. Existing saved
  cache preferences continue to apply and can be changed under Storage.

Compare a new Massif run using the same account, fonts, window size, sync
workload and cache preferences. Include startup, initial synchronization,
opening rooms, history paging and checkpoint writing. Use the highest total
across the entire run, including allocator overhead, rather than its final
snapshot. A lower steady-state value does not establish a 50 MiB peak.
