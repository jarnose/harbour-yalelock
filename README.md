# Yale Lock

Native control for a Yale Access / August smart lock on Sailfish OS - lock,
unlock and read the door/lock status directly over Bluetooth LE, with no
cloud account and no Internet permission.

## Scope (v1): BLE-only, no cloud login

This app talks to the lock *only* over Bluetooth. It does not sign in to
Yale's cloud service (`api.aaecosystem.com`, used by the "Yale Home"
Android app) and holds no Yale account password.

Instead, each lock is added once with three values:

- its Bluetooth MAC address (found with the in-app scanner),
- its **offline handshake key** (16 bytes, entered as 32 hex characters),
- the **key index** (0-255) that key is stored in on the lock.

These three values are exactly what Home Assistant's own, Yale-sponsored
["Yale Access Bluetooth"](https://www.home-assistant.io/integrations/yale_access_bluetooth/)
integration asks for too - even Yale's own open-source Python libraries
(see "Third-party protocol source" below) don't automate fetching them for
this lock family, because that needs a real OAuth sign-in through Yale's own
login page, not a scriptable API call. Getting them onto this app is
therefore a one-time manual step:

1. Make sure the lock already works with the official Yale Home app on an
   Android phone (this proves the offline key exists and is current).
2. Capture the key and index once, from your own traffic, with a tool like
   `mitmproxy` pointed at your own phone's own Yale Home app - the same
   general technique the Home Assistant integration's setup documentation
   describes. The value is per-lock and does not change until the lock is
   factory-reset or the key slot is rotated.
3. Enter the three values in this app's "Add lock" page (or scan for the
   lock first, to fill in its address).

After that, every lock/unlock/status read happens entirely over Bluetooth -
no further cloud dependency, ever.

### Future work: cloud login

A later version could add a proper account-login screen (Yale's real OAuth
sign-in, opened in the system browser with a `harbour-yalelock://` redirect
so the app never sees the password) purely to automate step 2 above. This is
deliberately not in v1: it is real interactive-OAuth work needing a live
account to test against, and scripting it blind from static analysis alone
would be unreliable. If you want this, say so and we can build and verify it
against your own account's real traffic.

## Third-party protocol source

The BLE protocol (GATT UUIDs, 18-byte frame layout, checksums, the AES
handshake) is ported from the Apache-2.0 licensed
[yalexs-ble](https://github.com/Yale-Libs/yalexs-ble) project, hosted under
the "Yale-Libs" GitHub org - the engine behind Home Assistant's official
Yale/August Bluetooth integration. See `THIRD-PARTY-NOTICES.md`.

## How this was built

Reverse-engineered by static analysis of the official "Yale Home" Android
app (`com.assaabloy.yale`, decompiled with Androguard) and its native
`libaugustLockComm.so` (disassembled with Capstone) - which confirmed the
app speaks exactly the protocol yalexs-ble already documents - then ported
to C++/Qt for Sailfish OS. See `THIRD-PARTY-NOTICES.md` for exactly what was
and wasn't taken from the Android app vs. from yalexs-ble.

## Building

Like this account's other native apps (see e.g. harbour-edgesync): open
`CMakeLists.txt` in Qt Creator with the Sailfish SDK, pick a target, build
and deploy to device. This environment can't compile Sailfish C++ itself -
build on-device/in the SDK and paste back the build log for any fixes.

## Status

Not yet built or tested against a real lock. The protocol and crypto layers
(`src/protocol/`) are a direct, careful port with a known-good reference;
the BlueZ D-Bus plumbing (`src/ble/`) follows the same pattern as this
account's other Sailfish BLE apps but has not run against real hardware yet.
Expect to iterate once it's on a device next to the lock.
