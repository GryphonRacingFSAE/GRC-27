#include "PreferencesCalibrationStore.h"

#include <Preferences.h>

static constexpr const char* kKey = "cal";

// Opened read-write even for reads: a read-only open of a namespace that
// was never written fails (and would log an error), read-write creates it.
StoreRead PreferencesCalibrationStore::read(uint8_t* buf, size_t capacity) {
    StoreRead r;
    Preferences prefs;
    if (!prefs.begin(namespace_, false)) {
        return r; // NVS unusable: treated as "nothing stored"
    }
    // isKey first: asking for the length of a missing key logs an error.
    if (prefs.isKey(kKey)) {
        r.length = prefs.getBytesLength(kKey);
        r.present = r.length > 0;
        if (r.present && r.length <= capacity) {
            // A short copy (flash error) leaves a short record: the codec
            // then reports it as Corrupt, never uses half a record.
            r.length = prefs.getBytes(kKey, buf, capacity);
        }
    }
    prefs.end();
    return r;
}

bool PreferencesCalibrationStore::write(const uint8_t* data, size_t length) {
    Preferences prefs;
    if (!prefs.begin(namespace_, false)) {
        return false;
    }
    // putBytes writes and commits; it returns the length only if both worked.
    const bool ok = prefs.putBytes(kKey, data, length) == length;
    prefs.end();
    return ok;
}
