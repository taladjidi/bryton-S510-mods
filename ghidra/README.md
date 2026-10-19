# Ghidra scripts

Headless scripts (Ghidra 12.x, `analyzeHeadless … -postScript X.java args`) written while analysing the S510
firmware. Most are generic for big stripped ARM firmware built with IAR, using Embedded Wizard and µC/OS-II.
Read-only unless the description says APPLY. They read their arguments from the command line; any
name lists / CSVs they mention are your own analysis outputs, not included here.

## Setting up a project

Main firmware (`firmware/R039/IMG.raw`, from `tools/fetch_firmware.py`):

```sh
analyzeHeadless <projdir> S510 -import firmware/R039/IMG.raw \
  -loader BinaryLoader -loader-baseAddr 0xd0000000 -processor ARM:LE:32:Cortex
```

then, in this order, the structure scripts: `MapITCM.java` (map the µC/OS kernel the firmware copies to ITCM at
0x0), `MarkOutlined.java` (flag IAR outlined stubs as inline), `FixInlineTails.java` (repair tail-call stubs that
break the decompiler), `RamMap.java` (RAM regions; makes literal-pool loads resolve to named globals),
`EwClasses.java` → `ApplyNames.java` (Embedded Wizard class/method names), `EwStructs.java` (typed EW class
structs).

Bootloader (`iap.bin`, base `0x08000000`, same loader/processor) + `IapSetup.java` (functions at every vector).

`dump.sh` runs `Dump.java` (decompile / callers / listings / references) without opening the GUI.

## Scripts

| Script | Purpose |
|--------|---------|
| `ApplyNames.java` | bulk-rename functions in the current program from a text file. |
| `BodyProbe.java` | read-only probe: body ranges of a function, and decompile result with all inline callees un-inlined. |
| `BsimMatch.java` | read-only: query every function of the current program against a BSim database of compiled open-source libraries; write the best matches as TSV for review. |
| `CallSeq.java` | read-only: for the named functions (args after the outfile), print the ordered sequence of calls (thunks resolved to their target's name) and string literals referenced, in address order. |
| `Coverage.java` | read-only: count named vs default-named (FUN_/thunk_FUN_) functions per region. |
| `Dump.java` | read-only helper: writes decompiler output to a text file. |
| `EwClasses.java` | read-only: walk Embedded Wizard class descriptors ("CLAS" magic) and emit candidate names as "0xADDR name" lines (arg 0 = names file, arg 1 = class table csv). |
| `EwStructs.java` | build EW_<Class> structures for the Embedded Wizard classes and type `this` of their methods. |
| `ExportChunks.java` | read-only: export still-unnamed functions (FUN_*) in [START,END) to text chunks for offline review by agents. |
| `ExportEwHeader.java` | read-only: write the /EW structures (built by EwStructs.java) as a C header, with _Static_assert(sizeof == descriptor size) per class so a C reconstruction compiled against it checks the layouts. |
| `FailScan.java` | read-only: decompile every function whose name matches a regex, list failures + error. |
| `FileMap.java` | read-only: for each string naming a source file (*.c/*.cpp/*.h), list how many functions reference it and their address span. |
| `FixInlineTails.java` | inline-flagged functions (IAR outlined stubs/wrappers) that are not disassembled, or that end in a plain branch out of their body (tail call), break the decompiler when inlined ("Bad partial clone"). |
| `FragStats.java` | read-only: classify default-named functions by how they are reached. |
| `FuncNameStrings.java` | read-only: propose original function names from __func__ usage. |
| `GlobalCtx.java` | read-only: for the most-used RAM globals (still unnamed DAT_*), print their named users and the decompiled lines that touch them, as evidence for naming. |
| `GlobalRefs.java` | read-only: memory blocks, then rank RAM addresses by how many functions load them from a literal pool (ldr rX,[pc,#..] -> 32-bit word whose value points outside the image). |
| `Histo.java` | read-only: function count and code bytes per 256 KB bucket (layout overview). |
| `IapSetup.java` | one-time setup of the IAP (bootloader) project: create functions at every Cortex-M vector table entry (reset, faults, IRQs) so analysis reaches the boot code. |
| `ImageWrites.java` | read-only: WRITE references whose target lies in the image block "ram" (is the image read-only?). |
| `InlineBisect.java` | run with -readOnly (changes are discarded): for each failing function, binary-search the set of ALL inline-flagged functions for a small subset whose un-inlining makes the decompile succeed. |
| `InlineCallees.java` | read-only: for the functions at the given addresses, count inline-flagged callees. |
| `InlineJumps.java` | read-only: list inline-flagged functions containing a computed (non-return) jump. |
| `ListExt.java` | read-only: list external (unmapped-address) functions with caller counts. |
| `ListNames.java` | read-only: "0xADDR name" for every function (current project state). |
| `MakeFuncs.java` | create Thumb functions at the given addresses (args: 0xADDR ...). |
| `MapITCM.java` | maps the ITCM kernel copy (exec 0x00000000, load 0xd0351a68, 64 KB) as a byte-mapped block, then replaces Ghidra's EXT_FUN_xxxx placeholders with real Thumb functions and re-points their thunks. |
| `MarkOutlined.java` | find IAR compiler-outlined code stubs and (optionally) mark them inline so the decompiler folds them into their callers. |
| `NullSubs.java` | read-only: emit "0xADDR nullsub_<addr>" for default-named functions whose body is a single return (bx lr). |
| `PatchCheck.java` | read-only: write patch bytes into memory, disassemble them and print the result, so hand-encoded instructions are checked by Ghidra before flashing. |
| `RamMap.java` | map the STM32H747 RAM regions as uninitialised blocks and make literal-pool words that point into them constant pointers, so the decompiler shows the global itself (DAT_2406cac0) instead of a pool … |
| `StringIndex.java` | read-only: string cross-reference index. |
| `TopCallees.java` | read-only: default-named (FUN_) functions ranked by number of call sites. |
| `Unname.java` | undo bad generated names. |
