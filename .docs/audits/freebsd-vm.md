# Running FreeBSD binaries locally (Sprint 51 D5)

The sysroot proves compile and link without a VM, the way musl does. A VM
answers exactly one further question — **does the product actually exec?** —
and that turned out to be worth asking.

## The answer

**Yes.** On FreeBSD 15.0-RELEASE under qemu+KVM, a statically linked binary
cross-built here prints and exits 0, from BOTH assemblers (bundled afs-as and
the system `as`).

That settles the ELF-branding question. FreeBSD's kernel brands a binary from
the `.note.tag` FreeBSD note first and the `EI_OSABI` byte second. crt1.o
carries the note (`__FreeBSD_version` 1500068) and we inherit it, so the
binary is branded correctly even though `readelf -h` reports
`OS/ABI: UNIX - GNU` — afs-as writes `System V` and GNU ld overwrites it.

So the sprint's "wire the target → OSABI plumbing" is **cosmetic for
execution**. It is still worth doing upstream for a linker that trusts the
byte, but it is not a blocker, and `scripts/freebsd_cross_lane.sh` checks the
note explicitly rather than assuming it.

## Reproducing the VM, and the four things that made it awkward

```sh
curl -o base.txz https://download.freebsd.org/ftp/releases/amd64/15.0-RELEASE/base.txz
tar -xf base.txz -C sysroot ./usr/include ./usr/lib ./lib      # the sysroot
curl -O https://download.freebsd.org/ftp/releases/VM-IMAGES/15.0-RELEASE/amd64/Latest/FreeBSD-15.0-RELEASE-amd64-ufs.qcow2.xz
```

1. **The image consoles to VGA, not serial.** The loader talks to serial under
   `-nographic`, but the kernel and getty do not follow. Interrupt the
   ten-second autoboot (space), press `3` for the loader prompt, then
   `set console="comconsole"` and `boot`.
2. **The serial input drops characters on a burst write.** Everything must be
   typed one character at a time with a small delay — both at the loader and
   at the shell. `/tmp/cgf-fbsd-type.sh` in the session notes does this.
3. **A qemu `fat:rw:` share appears as `/dev/vtbd1s1`, not `/dev/vtbd1`.**
   Mounting the raw device fails with `Invalid argument`, `/mnt` stays empty,
   and running the binary reports `not found` — which reads exactly like an
   exec rejection and is not one. Check the mount before believing the
   result.
4. **The Unix socket for `-serial unix:` must live somewhere short.**
   `sun_path` is 108 bytes and the scratchpad path alone exceeds it.

CI's path is different and simpler: `vmactions/freebsd-vm` boots a VM on a
Linux runner, which is what the sprint plans. This file exists so the local
route is reproducible without rediscovering the four traps above.
