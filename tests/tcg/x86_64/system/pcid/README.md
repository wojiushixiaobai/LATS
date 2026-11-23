# PCID and HAMT ASID system test

This is an opt-in x86-64 system-emulation feature. Use the same LATS binary
and CPU model for all three configurations:

| Mode | Additional LATS options (after `-latx select`) | Extra `-cpu` features |
| --- | --- | --- |
| A | none | none |
| B | `pcid=on` | `+pcid,+invpcid` |
| C | `pcid=on,pcidasid=on` | `+pcid,+invpcid` |
| C with software banks | `pcid=on,pcidasid=on,pcidcache=on,aotsys=off` | `+pcid,+invpcid` |

Omit the additional options and features to return to A. A and B use
conservative invalidation. C requires pinned HAMT vCPUs in DMW mode and
hardware ASIDs enabled. It reserves tags 16–31 in each vCPU, retaining a
translation only on an explicitly no-flush CR3 write that hits the full
guest CR3 identity. The slot index mixes all 12 PCID bits so that the
user and kernel PCIDs of a KPTI pair need not share a slot. Slot
replacement invalidates the hardware tag before
publishing the new owner. Tag 0 remains for the translator and 15 for the
untagged guest. Existing software TLBs, fast instruction caches and jump
caches are invalidated on address-space switches. With hardware and software
banks enabled, INVPCID types 0 and 1 clear only the specified PCID's bank.
Other INVPCID paths conservatively flush all contexts. Hardware cache contents
are transient across reset and VM state load.

`pcidcache=on` additionally retains the software TLB, fast instruction
caches and both jump caches in each ASID slot. It requires compact NJC.
JIT and saved AOT execution are supported. Valid no-flush switches change roots
without clearing entry arrays. Explicit mapping changes expire inactive banks
before reuse. Physical write-protection notifications update matching write
flags in every live bank. Physical TB invalidation reaches every
allocated jump-cache bank. See `docs/devel/lats-pcid-caches.md` for the
lifetime and invalidation rules. This remains an opt-in experimental mode.

For diagnostics, add `pcidstats=on` in **all** compared modes. HMP `info jit`
then shows per-vCPU CR3 writes, PCIDE enables, PCIDs observed, no-flush
requests, retained bindings, new bindings, counts by INVPCID type, time
inside the CR3 write helper, and HAMT guest hardware refill handler entries
and reference-counter ticks. Refill ticks cover the emitted handler, not
work later performed by a slow fault handler. Enable the `x86_pcid_cr3`
trace event for a bounded sample to collect LATX guest instruction addresses.
The normal execution path has this event disabled.

## Run a controlled guest

Assemble the ROM on a machine with an x86-targeting clang. Extract the
relocation-free text section with GNU objcopy. Only Python 3 and a runnable
LATS build are needed on the LoongArch test machine:

    clang -target x86_64-none-elf -DPCID_SMP -DPCID_BENCH -DPCID_WAIT \
        -c pcid.S -o pcid.o
    readelf -r pcid.o
    objcopy -I elf64-little -O binary -j .text pcid.o pcid.bin
    python3 run.py /path/to/lats-x86_64 pcid.bin A --output A.json
    python3 run.py /path/to/lats-x86_64 pcid.bin B --output B.json
    python3 run.py /path/to/lats-x86_64 pcid.bin C --output C.json

The executable must have its normal renderer/runtime library search path.
Use `taskset` around the runner if reserving specific physical cores.
For an independently configured `--enable-latx-tcg-mode` build, run A and B
with `--engine tcg`. The test never attaches a guest disk.

The BSP enters long mode with PAE, PSE, NXE and WP, making the controlled
data accesses eligible for the HAMT hardware page walker. It checks feature
enumeration, PCIDE enable constraints, bit 63 readback, privileged faults at
CPL3, bad encodings, noncanonical addresses, a page-split descriptor, code
and data at the same VA in different roots, write revocation, global-page
invalidation and forced tag collisions. The AP receives a real local-APIC
IPI, invalidates its mapping and executes the remapped code.

The timed workload alternates two address spaces for 4096 pairs of switches.
The JSON records guest TSC ticks for the loop and hardware counters. A
successful C run must actually reuse tags; a successful B/C HAMT run must
exercise guest hardware refill. Treat the result as mechanism evidence.
Windows adoption requires observing CR4.PCIDE and no-flush writes in the
target Windows workload itself.

Add `-DPCID_LEGACY` at assembly time to run 32-bit protected-mode checks
before entering long mode. These include a non-flat FS descriptor, a
16-byte operand crossing its segment limit, a null selector, and attempts
to enable PCIDE outside IA-32e mode. The same runner checks the result.

Add `-DPCID_CACHE_SMC` to exercise code modification through an identity
alias in another address space and on another vCPU while the old bank is
inactive. The executable pages are in ordinary RAM above 1 MiB, where the
HAMT physical SMC bitmap applies. The low BIOS area follows a different
tracking path and is outside this test. For the complete bank regression:

    clang -target x86_64-none-elf -DPCID_LEGACY -DPCID_SMP \
        -DPCID_BENCH -DPCID_WAIT -DPCID_CACHE_SMC -c pcid.S -o pcid.o
    objcopy -I elf64-little -O binary -j .text pcid.o pcid.bin
    python3 run.py /path/to/lats-x86_64 pcid.bin C --cache --reset

`--reset` resets the same live VM and requires a second complete pass with
fresh architectural counters and reused software banks. Compare with the
same executable and ROM without `--cache` for mechanism cost; do not infer
Windows application speed from the switch loop alone.

Add `-DPCID_KPTI_ALIAS` to use PCIDs 1 and 2049 during the timed switches.
The ROM changes a mapping in an inactive bank, invalidates it twice, and
checks both roots after the switch. It then remaps the active bank,
invalidates the address, reloads the same PCID with no-flush and executes
from the new mapping.

The full SMC run also ejects AP 1 through the i440fx ACPI CPU hotplug
interface after recording the counters. It checks that just the BSP remains.
For deterministic coverage of the overlap between CPU removal and a remote
reader, run `python3 test-retirement.py` on any host with a C compiler and
pthreads. It schedules removal at jump-cache dispatch and inside a dirty
walk, using the production reader functions.

The ROM checks addr32 EIP-relative GS addressing with a segment base above
4 GiB. Run B with and without `--la57` to check both target widths while
using four-level paging. To exercise nested SVM, also assemble with
`-DPCID_SVM` and use B with `--engine tcg` on a TCG build configured without
`--enable-latx-opt` and `--enable-latx-opt-bql`. The optimized build stubs
SVM interception. The L1
checks the exit code, faulting RIP, descriptor address and register operand,
then disables interception and runs the instruction through to VMMCALL.

Run `python3 test-rr.py /path/to/rr/lats-x86_64` on a build with
`--enable-latx-rr` to check single-thread rejection. The host test
`tests/tcg/i386/latx-aot/test-aottucp-cache-policy.py` exercises saved
core and user AOT tails with both settings of `pcidasid`.

The main ROM also remaps a 2 MiB PDE and issues INVLPG at a different 4 KiB
offset from the indirect branch target. Mode A covers the untagged jump cache.
B and C cover the active address space with PCID. In compatibility mode the
ROM tests all 15 possible descriptor splits across the 4 GiB linear boundary,
and the long-mode check requires #GP with unchanged CR4 when PAE and PCIDE
are cleared together.

For the 16-bit vendor and decoder matrix:

    clang -target x86_64-none-elf -c modes.S -o modes.o
    objcopy -I elf64-little -O binary -j .text modes.o modes.bin
    python3 test-modes.py /path/to/lat/lats-x86_64 /path/to/tcg/lats-x86_64 modes.bin

The guest checks the exception vector and saved instruction pointer in real
mode and VM86. A 16-bit protected code segment executes both address sizes.
The host retirement test also checks that a bank allocation requests the
alignment of its embedded TLB descriptors and uses the matching free routine.
