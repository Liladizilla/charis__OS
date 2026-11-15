# CharisOS installer storage discovery

## Current device boundary

The current kernel storage driver supports one legacy primary-master ATA device
through 28-bit LBA PIO and 512-byte logical sectors. It does not support AHCI,
NVMe, USB mass storage, secondary-channel discovery, primary-slave discovery,
ATAPI packet devices or LBA48 sector I/O.

The ATA IDENTIFY record now provides the device model, serial number, firmware,
logical sector size, total reported capacity and LBA capabilities. Capacity
larger than the current LBA28 command window is recorded accurately, while
the separately exposed addressable-sector count is clamped to the range the
driver can actually reach.

A detected but unsupported device remains visible in inventory with
\`io_supported=false\`. The legacy \`ata_is_present()\` keeps its stricter meaning:
the primary-master device can be read through the current 512-byte LBA28 path.

## Installer safety gates

The installer must not call the existing write path until all of the following
are implemented and independently tested:

1. Stable device identity and exact sector capacity.
2. Bounded MBR and GPT parsing with overflow-safe partition ranges.
3. Explicit selection of the target device and partition plan.
4. Dry-run output that shows every proposed region and preserved partition.
5. A second destructive-action confirmation bound to the selected device ID.
6. Transactional writes with verification, interruption recovery and rollback.

The existing FAT32 mount assumes a filesystem volume starts at sector zero. It
must not be reused as a partition-aware installer formatter. There is no
installer partition writer in this milestone, and no disk is modified by
storage discovery.


## MBR parser delivered

The kernel now has a host-testable parser for the 512-byte legacy MBR sector.
It validates the 0xAA55 signature, boot indicators, empty-slot consistency,
non-zero partition starts and lengths, overflow-safe end positions, disk
capacity and overlap between primary entries.

Extended partition containers are reported but their EBR chains are not
followed, so the result is marked incomplete. A single protective 0xEE entry is
classified as a GPT protective MBR, not as proof that GPT metadata has been
validated. Hybrid protective MBRs are rejected. Partitions that fit the disk
but extend beyond the current LBA28 access window are inventoried as present
but inaccessible through this driver.


## GPT parser delivered

The scanner follows a protective MBR with read-only reads of the primary GPT
header and partition-entry array. It validates the GPT 1.0 signature, revision,
header size and CRC32; checks LBA geometry and array size before allocation;
then validates the array CRC32, unique partition GUIDs, usable-LBA bounds,
disk capacity and overlap.

The array is capped at 128 entries and 64 KiB. The scanner reads no metadata
outside the current ATA LBA28 access window, even when IDENTIFY reports a
larger LBA48 capacity. A GPT partition beyond that window remains visible as
present but not accessible through this driver.

Only the primary GPT metadata is read in this milestone. Backup GPT validation,
repair, hybrid MBR support, EBR logical partitions, partition formatting and
all disk writes remain out of scope.
