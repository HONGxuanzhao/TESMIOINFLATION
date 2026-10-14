# TESMIOINFLATION: source recovery and validation

Recovery date: 2026-10-15 (Asia/Shanghai).

Original C++ files and their later edits were recovered from local development records. The source was not generated from decompiler pseudocode. Private conversation logs, credentials, local build products and backup folders are excluded from this repository.

The retained TESMIO API 4 header is a minimal ABI declaration. Build scripts were adapted to keep output in `build/`; the POPMOD header include path was made independent of the checkout directory name. No license for the recovered author code has been assigned by this upload.

## Verification

- Successfully rebuilt with Microsoft Visual C++ 14.51.36231 for Windows x64.
- The complete `.text` machine-code section is byte-for-byte identical to the published DLL.
- The entire rebuilt DLL is byte-for-byte identical after zeroing only the COFF header timestamp and IMAGE_DEBUG_DIRECTORY timestamp fields.
- The original published DLL remains in `plugins/`.
- No fresh live-game session was run for this publication.

Original DLL SHA-256: `8c41bea3b3fa627dd5827ff70d88a0f45653287b39efd7a50e0d42268765a7d7`

Rebuilt DLL SHA-256: `8fa10dba5e8896f1ed3e368a6f9ba5a5520478d230b95defd586b15dfafa1d42`

SHA-256 after timestamp normalization: `29a4a5b243c9875ab5d43f11e889d1a5175370afbf65914cc324587ee10ab95d`

归档源码从本机原始开发记录与修改记录恢复。5 个工程均重新编译成功；此 MOD 重建 DLL 的完整机器码与原发布 DLL 相同，整个文件仅有 PE 时间戳字段不同。发布包保留原 DLL；这次上传没有进行新的游戏运行测试。
