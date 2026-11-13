# Release and branch integration policy

CharisOS is developed as one operating system. The release image must represent one coherent, tested source tree, not a collection of independently built feature branches.

## Authoritative source

- `main` is the authoritative integration branch and the only branch from which a release ISO may be produced.
- Feature branches are temporary development workspaces. A feature is not part of CharisOS until its changes have been reviewed, tested against the current integration baseline and merged.
- A successful build on an isolated branch does not make that branch release-ready.
- Do not merge every open branch mechanically. Old branches can contain duplicate work, superseded implementations, dependency stacks or code that conflicts with newer kernel interfaces.

## Integrating work

1. Start new work from the latest `main` commit.
2. Keep each pull request focused on one coherent subsystem or dependency stage.
3. If work depends on another pull request, state the dependency and merge the prerequisite first. Rebase or recreate the dependent branch against the newly updated `main` before validation.
4. Before merge, confirm the branch has no commits missing from current `main`, inspect the complete diff, and run all required tests on the final pull-request head.
5. For an old or diverged branch, compare its unique changes against current `main`. Backport only changes that are still needed. Port them onto a fresh branch, add or preserve regression tests, and close the superseded branch after its replacement is integrated.
6. After merge, delete or archive the temporary branch. Keep `main` free of dead implementations, compatibility shims and abandoned alternate copies.

## Evidence required

A subsystem is not complete merely because it compiles.

- **Build:** freestanding kernel and boot images compile with warnings treated as errors; the x86-64 baseline and link checks pass.
- **Host tests:** bounded parsers, policy checks, ownership rules and deterministic lifecycle logic have targeted regression coverage.
- **QEMU runtime tests:** scheduler, boot, memory isolation, process lifecycle, device emulation and desktop behavior are exercised through the real kernel path where applicable.
- **Hardware validation:** features that depend on real firmware, controllers, storage devices, displays, networking or input are marked as requiring hardware verification until that verification has actually happened.
- **Documentation:** README, subsystem design documents and the roadmap describe the tested behavior and clearly label partial or unavailable functionality.

A test that was not run must not be described as passing. A QEMU pass must not be presented as proof of broad hardware compatibility.

## ISO and release provenance

A release ISO is built from an exact, recorded commit on `main`, after all required pull requests have been merged and the release validation run has passed. The release record must include:

- the `main` commit SHA and release tag;
- the CI workflow run and its required job results;
- the ISO and USB-image artifact names and SHA-256 checksums;
- the boot configurations actually tested (BIOS, UEFI, virtual machine and any physical-hardware matrix);
- known limitations and hardware tests that remain outstanding.

Pull-request artifacts are validation outputs, not release images. Never assemble an ISO by copying files from several branch workspaces or by mixing artifacts built from different commits. The kernel, boot files, configuration, desktop assets and included applications must all come from the same integrated tree.

## Current release boundary

The current project can boot a freestanding x86-64 kernel through GRUB2/Multiboot2 and run its native kernel-owned desktop shell. It is not yet a general-purpose production desktop OS. Complete userspace, a stable public executable/application ABI, broader device drivers, complete security enforcement, writable everyday file workflows and real-hardware compatibility still require implementation and validation. Release claims must match the evidence available for the specific commit and image.
