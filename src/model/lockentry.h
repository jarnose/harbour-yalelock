#pragma once

#include <QString>

// Non-secret metadata for one saved lock. The offline handshake key itself
// is never stored here - it lives in the Sailfish Secrets vault, keyed by
// LockEntry::id (see secrets/secretvault.h and controller/appcontroller.cpp).
struct LockEntry
{
    QString id;         // internal UUID, also the vault's secret name
    QString name;       // person-chosen label, e.g. "Front door"
    QString bleAddress; // BlueZ MAC address, e.g. "AA:BB:CC:DD:EE:FF"
    int keyIndex = 0;   // handshake key slot (0-255; 0 = Admin, 255 = Mercury)
    int sortOrder = 0;
};
