# Changelog / 更新日志

All notable changes follow Semantic Versioning. / 重要变更遵循语义化版本。

## [Unreleased]

## [1.0.0-rc.1] - 2026-08-24

- Initial C++20/Qt/MuPDF architecture and public domain types.
- Cohesive view/edit icon set, bilingual tooltips, and native application icons for Windows, Linux, and macOS.
- Hybrid command bars keep familiar controls icon-only while adding short labels to primary and high-risk operations.
- Background document session, password opening, rendering, search, practical edits, MuPDF journal undo/redo, permanent redaction, encryption/decryption, and validated atomic save.
- Continuous Qt canvas, bilingual runtime translation, watermark creation/scanning/confirmed removal, and initial integration tests.
- Slim MuPDF build tooling and automated Windows x64, Linux x86_64, and macOS Universal release packaging.
- Release linking, integration tests, qpdf/Poppler cross-validation, and Windows portable-package dependency checks use the unified Qt 6.11.2 baseline.
- Encrypted output rejects empty user/owner passwords, confirms both credentials, warns without rejecting matching passwords, and overwrites temporary/session password buffers when released.
