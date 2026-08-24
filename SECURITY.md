# Security policy / 安全策略

Please report suspected vulnerabilities privately through GitHub Security Advisories after the public repository is created. Do not attach confidential PDFs; provide the smallest synthetic reproducer possible.

安全问题请在公开仓库创建后通过 GitHub Security Advisories 私密报告。不要上传机密 PDF；请尽量提供最小的合成复现文件。

Supported releases receive fixes on the latest stable branch. nexPDF does not provide password cracking, DRM bypass, silent external links, embedded-file execution, or automatic PDF JavaScript execution. Malicious PDFs should still be treated as untrusted input and processed in an isolated account or virtual machine when risk is high.

Encrypted output requires non-empty user and owner passwords. Matching passwords are permitted because they do not weaken AES confidentiality, but some readers may treat the credential only as a user password and fail to grant owner privileges. Password byte buffers and the document-session password cache are overwritten when they are no longer needed; operating-system paging and copies owned by GUI/platform internals remain outside the process's secure-erasure guarantee.

加密输出必须设置非空用户密码和所有者密码。两个密码相同不会削弱 AES 保密性，因此仍允许使用；但部分阅读器可能只把它识别为用户密码，无法授予所有者权限。密码字节缓冲区和文档会话密码缓存在不再需要时会被覆盖清理；操作系统换页以及 GUI/平台内部持有的副本不在进程安全擦除保证范围内。
