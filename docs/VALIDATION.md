# Validation and boundaries

Application/SDK **0.1.1**, Microsoft-signed driver **0.1.0.0**, Windows x64. The complete ZIP includes self-contained UI/console installers embedding the exact returned driver. Kernel implementation and signing material stay private.

## Signed package identity

The returned INF is byte-identical to the submission. Windows catalog hashing confirms that protected SYS content matches the qualified original image, apart from Authenticode exclusions. Microsoft kernel-policy catalog trust and exact INF/SYS membership were verified on the development host and again inside the guest. Both installers were inspected as PE resources without running host installer code; their three embedded files match the return exactly.

- Original qualified SYS SHA-256: `9D4DD9CD78B995F94B7E59DD32E1FF87D964BC834B1B711DC9C283EBD8B002BB`.
- Returned signed SYS SHA-256: `2c41a02b421f17b9fc8f6aa67f9ae1118defa4c5c6eed418d9ff4b2ecdc9c84b`.
- No kernel rebuild, catalog regeneration, lab certificate or replacement driver was used for this return.

## Actual signed Windows tests

On 2026-10-26, the exact returned image and complete console installer passed isolated **Windows 10 build 19045 x64** campaigns: normal: **3180 native checks**, targeted Driver Verifier: **3182 native checks**. Test-signing was explicitly disabled before installation. The guest's existing Secure Boot setting was preserved; Secure Boot/HVCI qualification is not claimed.

Each campaign used the independently Microsoft-signed Vdevice fixture to create one standalone vendor-defined HID device, with no keyboard/mouse usage or host hardware target. Actual control and interrupt IN/OUT observations retained independently verified original data. An IN request pending before capture produced its sampled completion with the pre-existing flag. The 600-report pressure loop exercised bounded ring overwrite. Malformed requests, forged/replayed permissions, real expiry, Stop, owner cleanup and exact original filter-property restoration passed.

The normal PnP restart of this Vdevice fixture reproduced an input timeout with configured=false even without USB Trace. The test recreated only that owned fixture with identical serial/descriptors and verified its exact instance identity before checking the filtered stack. This is a recorded fixture limitation; physical USB restart/reconnect behavior remains unqualified. The check count includes fixture coordination assertions, not thousands of distinct devices.

The live HID fixture qualifies control/interrupt paths. Bulk shares the implemented bounded URB parser, but no live bulk/WinUSB workload, large/chained-MDL transfer or physical SuperSpeed device was exercised. Isochronous payloads, electrical bus analysis, USB redirection and universal support are not implemented or claimed.

Offline CTest passes 200,007 ABI mutation/bounds and read-only installer refusal checks. The same updated sources also pass a separate AddressSanitizer configuration. The private KMDF driver has zero PREfast XML defects. Requests always continue unchanged; only bounded observations are copied. Kernel URB pointers/direct buffers must be kernel addresses; safe no-execute MDL mappings and bounded byte counts are used. External user IOCTLs do not enter the internal USB parser. Unknown/chained layouts are flagged, and kernel pointers/pipe handles are not exposed.

Tracked requests retain bounded completion metadata while inactive without copying inactive payloads. Stop/close/new-owner generations prevent old samples being relabelled for another owner. Previously inactive requests can be admitted at authorized completion with an explicit flag; pre-capture OUT payloads are never read. Publication rechecks permission, module and generation. Counters and flags distinguish truncation, unavailable samples, unknown length, ring overflow and in-flight pressure.

Restricted-token control access, administrator zero/read-only/write-only opens and a second owner were refused. Actual console installation and uninstallation passed, every owned package was removed and all product/fixture services unloaded. No new minidump was observed. The VM returned to its original powered-off baseline with all nine historical checkpoints retained. No driver, certificate or host boot/device policy was installed or changed on the development PC.

Standard Driver Verifier targeted only TitanUserBackedDisk.sys and TitanUSBTrace.sys in the signed campaigns. The independent Vdevice fixture and Windows drivers were not targeted. UI/console setups share an installation core; UI click automation is not claimed. User-mode builds use W4/WX, CFG, ASLR, NX and static CRT. Original InfVerif and submission evidence is preserved.

Windows 11, Secure Boot/HVCI, physical USB/controllers, broader storage backends/filesystems, third-party filters, suspend/resume, surprise removal, low-resource simulation and prolonged pressure/performance benchmarks remain separate qualification work. Microsoft signing is not WHQL certification, zero overhead or a guarantee of defect-free/universal compatibility.

Product-specific keys obtain signed per-handle permissions lasting at most one hour, explicitly renewable while enabled. Revocation refuses future grants; accepted permissions expire normally. No machine binding or automatic billing. Kernel sources, CABs, PDBs, private probes and signing credentials are excluded from public repositories/downloads.
