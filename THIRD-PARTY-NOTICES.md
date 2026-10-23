# Third-party notices

## yalexs-ble (and yalexs)

The BLE wire protocol implemented in `src/protocol/` (frame layout, opcodes,
checksums, and the AES handshake sequence in `src/ble/yalelink.cpp`) is
ported from:

- **yalexs-ble** - <https://github.com/Yale-Libs/yalexs-ble>
- **yalexs** - <https://github.com/Yale-Libs/yalexs> (its `BRAND_CONFIG`
  constants are the reference for the `api.aaecosystem.com` endpoint shapes
  mentioned in README.md's "Future work" section - not used by this app's
  BLE-only v1)

Both are licensed under the Apache License 2.0; a copy is kept at
`third_party/yalexs-ble/LICENSE`. Both projects are hosted under the
"Yale-Libs" GitHub organization and are the engine behind Home Assistant's
official Yale/August Bluetooth and cloud integrations.

This is a from-scratch C++/Qt port (different language, different structure -
a merged BLE-transport-and-session class instead of separate asyncio
Session/SecureSession/Lock classes), not a copy of the Python source, written
for Sailfish OS's BlueZ-D-Bus-only environment. No file from either project
is included verbatim; `lockprotocol.cpp`/`lockcipher.cpp`/`yalelink.cpp`
reimplement the same protocol these projects already documented and
maintained. The per-file header comments say where this applies.

Nothing in this app's protocol or crypto code was extracted from the Yale
Home Android app's decompiled source. Static analysis of that app (its
native `libaugustLockComm.so` and its Kotlin/Java classes) was used only to
independently confirm that yalexs-ble/yalexs already document this exact
lock family's protocol correctly - see README.md's "How this was built"
section.
