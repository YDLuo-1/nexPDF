# Permission fixture

`empty-user-restricted.pdf` is a synthetic, one-page PDF generated from
`tests/fuzz/seeds/text.pdf` with MuPDF 1.28.6. Its user password is empty,
its owner password is `owner`, and only printing is permitted. These are
public test credentials, not confidential data.

The fixture exercises automatic user access and explicit owner authentication
when `fz_needs_password` would return false. It is intentionally generated
outside the application, which requires nonempty passwords for new encryption.

To regenerate, compile `make_empty_user_fixture.cpp` against the pinned MuPDF
SDK (using `cmake/FindMuPDF.cmake` and `MuPDF::MuPDF`) and run:

```text
permission_fixture tests/fuzz/seeds/text.pdf tests/fixtures/empty-user-restricted.pdf
```

Encryption uses random salts, so regeneration changes bytes. The regression
checks authentication and permissions rather than an exact byte identity.
