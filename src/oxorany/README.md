# oxorany

Vendored from https://github.com/llxiaoyuan/oxorany (MIT, see
`../../LICENSES/Oxorany-MIT.txt`).

A compile-time constant obfuscator. `oxorany(literal)` encrypts the literal
into the binary and decrypts it at the point of use, so the plaintext does not
appear in the shipped image.

`oxorany.h` and `oxorany.cpp` are the build integration and are compiled into
every platform target (Windows, macOS, Android and iOS); the PC licence table is
the only current consumer. Upstream `main.cpp` is the upstream demo (not
compiled). The upstream `oxorany_include.h` is
deliberately NOT used by the build: it defines
`OXORANY_DISABLE_OBFUSCATION`, which turns the obfuscation off (it exists to
dodge an MSVC `/GL` warning in the demo). Including it would silently ship
plaintext.

Decrypted values live only for the duration of the full expression that used
`oxorany(...)`; copy the bytes out immediately (see `assignString` in
`pc/src/protection.cpp.in`). For ordinary per-call string uses the lighter
`src/obfuscate.h` (AY_OBFUSCATE, public domain, same compile-time hiding) is
preferred; oxorany's heavier opaque-decrypt stays for the audited license
identity table.
