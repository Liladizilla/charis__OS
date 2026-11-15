# CharisOS installer storage discovery

## Current device boundary

The current kernel storage driver supports one legacy primary-master ATA device
through 28-bit LBA PIO and 512-byte logical sectors. It does not support AHCI,
NVMe, USB mass storage, secondary-channel discovery, primary-slave discovery,
ATAPI packet devices or LBA48 sector I/O.

The ATA IDENTIFY record now provides the device model, serial number, firmware,
logical sector size, total reported capacity and LBA capabilities. Capacity
larger than the current LBA28 command window is recorded accurately. The
separately exposed addressable-sector count is clamped to the range the driver
can actually reach, and is zero whenever the current 512-byte PIO path cannot
safely perform I/O (including unsupported logical-sector sizes). The first-run
storage screen shows both total reported capacity and current PIO access so
they cannot be mistaken for the same capability.

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

Extended partition containers are traversed through a bounded EBR
chain, up to 32 logical partitions. The first EBR entry is interpreted relative
to its own EBR sector; the second link is interpreted relative to the extended
partition base. Every logical extent and next-record link must remain inside
the extended container and device. Each link's declared extent becomes
the upper bound for the next record and its logical data. Link extents may not
overlap any discovered logical partition. Cycles, overlaps with primary or
logical partitions, self-links, non-empty reserved entries and malformed
records are rejected. An unreadable link or a chain beyond the hard limit is
reported as incomplete rather than being presented as fully enumerated.

A single protective 0xEE entry is classified as a GPT protective MBR, not as
proof that GPT metadata has been validated. Hybrid protective MBRs are
rejected. Partitions that fit the disk but extend beyond the current LBA28
access window are inventoried as present but inaccessible through this driver.


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

The scanner now validates both GPT copies without writing to disk. The
primary header must identify LBA 1 and point to the device's final LBA; the
backup header must be at that final LBA and point back to LBA 1. Both headers
must agree on disk GUID, usable-LBA bounds, entry count, entry size, header size
and entry-array CRC. The backup entry array is read in bounded sectors and
compared byte-for-byte over the declared array length with the already
CRC-validated primary array.

If the primary GPT is valid but the backup header or array falls outside the
current LBA28 access window or cannot be read, the inventory retains the
primary partition table and reports that backup verification is unavailable.
A malformed or inconsistent backup receives a separate failure status. The
first-run screen distinguishes a fully verified backup from primary-only
inventory; it never presents primary-only as complete redundancy validation.

Backup repair, hybrid MBR support, partition formatting and all disk writes
remain out of scope. EBR and GPT discovery are read-only and do not make any
partition an installation target. The disk layout rules follow the UEFI
Specification's GPT definition (see the GPT disk-layout and header fields
sections in UEFI Specification 2.11).
