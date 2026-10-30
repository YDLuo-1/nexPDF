# nexPDF 1.0.5-rc.2 second-pass acceptance

Date: 2026-10-30. Baseline: `v1.0.5-rc.1`, commit
`4a4f15e6b1f547de21850a9dd91618ac16437861`. Candidate identity is the immutable
`v1.0.5-rc.2` tag. This round follows the [first maintenance report](maintenance-1.0.5.md).

## Confirmed failures and repairs / 已确认问题与修复

| Boundary | Reproduction | Repair |
|---|---|---|
| Security changes | A reader-password session saved plaintext or replaced encryption despite owner restrictions. | Core saving requires owner authentication for every explicit security change. Keep preserves existing security. |
| Import permissions | A source denying copying was grafted into an unrestricted destination with its user password. | Authenticate the source, including explicit credentials when its user password is empty, and check copying before grafting. |
| Immediate close/open | Queue Insert Blank Page, immediately call discard confirmation: no prompt appeared. | A synchronous pending-mutation count plus atomic modified state covers edits, watermarks, undo and redo before UI signals arrive. |
| Obsolete rendering | Start slow-page tiles, change zoom: old requests completed without cancellation. | Cancel on zoom/rotation/document invalidation; prune canvas and thumbnail work outside prefetch bounds. |
| Failed import history | Undo an edit, import page selection `{0, 99}` from a one-page source: the first page is rolled back, but redo is lost. | Defer MuPDF redo invalidation until the outer operation commits. Abandoned operations retain the previous history. |

普通用户密码不得解密或重设安全设置；页面导入遵守源文档复制权限。
“文件 → 使用所有者密码重新打开”允许免输用户密码的受限文件获得合法所有者访问，
并保留保存/放弃/取消确认。错误密码或取消打开仍保留原会话。

The journal fix and unsigned AES shift fix join two earlier small patches in
`tools/patch_mupdf.py`. Two further image-filter/mask fixes bring the total to six;
all are applied
idempotently to Windows, Linux, macOS, sanitizer builds and the full source archive.
The MuPDF submodule stays at 1.28.6 (`da5f5eb890d7b3fb76903160b5f7f48ba1301492`).
Patch application rejects a changed source layout and must be reviewed on an engine upgrade.

## Verified locally / 本地实测

Windows 11, Visual Studio 2022, Qt 6.11.2, existing MuPDF SDK/build directory reused.

- Before repair: three unauthorized-operation rows and two UI regressions fail.
  A subsequent failure-recovery test additionally exposed lost redo history.
- Final core Qt Test: **31 passed**, including initialization/cleanup and data rows;
  UI Qt Test: **8 passed**. CTest **2/2**, **16.43 s**, after all six engine patches
  and the application resource fixes.
- Six permission rows cover decrypt/re-encrypt/import with nonempty and empty user passwords,
  rejection without output/state changes, Keep saves and successful owner operations.
- Failed import/annotation regression covers rollback from the original document and
  a saved edited document, reopening the saved result and successfully redoing the earlier edit.
- UI regression checks the immediate-close Cancel response, obsolete-render cancellation,
  and menu-based owner reopening followed by decryption of an empty-user-password PDF.
- Chinese translation coverage: **188 strings**, none missing. CI YAML parses.
- DLL preflight preceded execution; all EXEs had explicit test timeouts.

The Linux sanitizer job now runs the **full core regression suite** as well as
seed replay and bounded mutation fuzzing, so the journal fix is exercised under
ASan/UBSan rather than only by the open/render fuzz harness. Three-platform CI and
all four packaging jobs must pass before publishing; the matching commit/run links
are retained on GitHub Actions and in the published release verification notes.

The first expanded sanitizer run
[41003525553](https://github.com/YDLuo-1/nexPDF/actions/runs/41003525553) stopped at
`crypt-aes.c:228`: an AES substitution-table byte was promoted to signed `int`
before shifting left by 24. The patch casts the 12 affected key/encode/decode
expressions to `uint32_t` before shifting, preserving the intended AES bit pattern.
Core UBSan now collects diagnostics across the suite and explicitly fails on any
`runtime error:`; ASan remains fail-fast. No sanitizer category is suppressed.

The next complete run
[41004543284](https://github.com/YDLuo-1/nexPDF/actions/runs/41004543284) exposed null
page-wrapper access, optional-mask zero-offset arithmetic, and **892,756 leaked bytes
in 33 allocations**. The application now drops its owned `pdf_create_annot` reference
on success and failure and guards nullable page cleanup. MuPDF's affine renderer
advances mask pointers only when present. The image was allocated during watermark
scanning, but its leaked reference was retained later by the redaction filter when
it returned the original image unchanged; that extra owned reference is now released.
The failure-history regression also covers an annotation created before loading a
missing stamp image. Successful and failed edits remain in the instrumented suite.

Local compact evidence: `out/second-pass-20261030` (ignored), with baseline/final
test logs, build logs, fixture hash and release verification. Rebuildable downloads,
fixture-generator binaries and package extraction are removed after validation.

## Acceptance limits / 验收边界

This is a focused second audit, not a proof that arbitrary PDFs are vulnerability-free.
PDF permission flags rely on cooperating readers; they cannot prevent extraction by
all other tools. Cancellation remains cooperative inside native rendering/parsing.
No throughput improvement is claimed from this round's cancellation regression.
Physical printers, macOS hardware and the complete high-DPI/input-method matrix still
require acceptance. `v1.0.5-rc.2` remains a prerelease and does not replace stable Latest.
