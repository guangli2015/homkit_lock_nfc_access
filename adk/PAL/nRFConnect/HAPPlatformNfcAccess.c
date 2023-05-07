// Disclaimer: IMPORTANT: This Apple software is supplied to you, by Apple Inc. ("Apple"), in your
// capacity as a current, and in good standing, Licensee in the MFi Licensing Program. Use of this
// Apple software is governed by and subject to the terms and conditions of your MFi License,
// including, but not limited to, the restrictions specified in the provision entitled "Public
// Software", and is further subject to your agreement to the following additional terms, and your
// agreement that the use, installation, modification or redistribution of this Apple software
// constitutes acceptance of these additional terms. If you do not agree with these additional terms,
// you may not use, install, modify or redistribute this Apple software.
//
// Subject to all of these terms and in consideration of your agreement to abide by them, Apple grants
// you, for as long as you are a current and in good-standing MFi Licensee, a personal, non-exclusive
// license, under Apple's copyrights in this Apple software (the "Apple Software"), to use,
// reproduce, and modify the Apple Software in source form, and to use, reproduce, modify, and
// redistribute the Apple Software, with or without modifications, in binary form, in each of the
// foregoing cases to the extent necessary to develop and/or manufacture "Proposed Products" and
// "Licensed Products" in accordance with the terms of your MFi License. While you may not
// redistribute the Apple Software in source form, should you redistribute the Apple Software in binary
// form, you must retain this notice and the following text and disclaimers in all such redistributions
// of the Apple Software. Neither the name, trademarks, service marks, or logos of Apple Inc. may be
// used to endorse or promote products derived from the Apple Software without specific prior written
// permission from Apple. Except as expressly stated in this notice, no other rights or licenses,
// express or implied, are granted by Apple herein, including but not limited to any patent rights that
// may be infringed by your derivative works or by other works in which the Apple Software may be
// incorporated. Apple may terminate this license to the Apple Software by removing it from the list
// of Licensed Technology in the MFi License, or otherwise in accordance with the terms of such MFi License.
//
// Unless you explicitly state otherwise, if you provide any ideas, suggestions, recommendations, bug
// fixes or enhancements to Apple in connection with this software ("Feedback"), you hereby grant to
// Apple a non-exclusive, fully paid-up, perpetual, irrevocable, worldwide license to make, use,
// reproduce, incorporate, modify, display, perform, sell, make or have made derivative works of,
// distribute (directly or indirectly) and sublicense, such Feedback in connection with Apple products
// and services. Providing this Feedback is voluntary, but if you do provide Feedback to Apple, you
// acknowledge and agree that Apple may exercise the license granted above without the payment of
// royalties or further consideration to Participant.

// The Apple Software is provided by Apple on an "AS IS" basis. APPLE MAKES NO WARRANTIES, EXPRESS OR
// IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR
// IN COMBINATION WITH YOUR PRODUCTS.
//
// IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION
// AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
// (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (C) 2020-2021 Apple Inc. All Rights Reserved.

#include "HAPCharacteristicTypes.h"
#include "HAPCrypto.h"
#include "HAPPlatform.h"

#if HAP_FEATURE_ENABLED(HAP_FEATURE_NFC_ACCESS)

/**
 * The salt value used with a key value to create a hash for the Identifier field
 */
#define kHAPPlatformNfcAccessKeyIdentifierSalt "key-identifier"

/**
 * Default number of NFC Access Issuer Keys supported
 */
#define kHAPPlatformNfcAccessIssuerKeyListSize 15

/**
 * Default number of active NFC Access Device Credential Keys supported
 */
#define kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize 10

/**
 * Default number of suspended NFC Access Device Credential Keys supported
 */
#define kHAPPlatformNfcAccessDeviceCredentialKeySuspendedListSize \
    (kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize * 2)

/**
 * Calculate the number of bytes of a key list to persist
 */
#define GET_LIST_BYTES(listType, list, entryType) HAP_OFFSETOF(listType, entries) + list.numEntries * sizeof(entryType)

/**
 * NFC Access platform configuration
 */
static struct {
    /**
     * Maximum number of issuer keys
     */
    uint16_t maximumIssuerKeys;

    /**
     * Maximum number of suspended device credential keys
     */
    uint16_t maximumSuspendedDeviceCredentialKeys;

    /**
     * Maximum number of active device credential keys
     */
    uint16_t maximumActiveDeviceCredentialKeys;

    /**
     * Counter keeping track of every change (add/update/remove) to issuer key, device credential key, and reader key
     */
    uint16_t configurationState;

    /**
     * Key value store to store NFC access lists
     */
    HAPPlatformKeyValueStoreRef _Nonnull keyValueStore;

    /**
     * Key value store domain dedicated for NFC access lists
     */
    HAPPlatformKeyValueStoreDomain storeDomain;

    /**
     * Configuration state change callback function
     */
    HAPConfigurationStateChangeCallback _Nullable configurationStateChangeCallback;

    /**
     * NFC transaction detected callback function
     */
    HAPNfcTransactionDetectedCallback _Nullable nfcTransactionDetectedCallback;

    /**
     * Flag denoting platform layer has been initialized
     */
    bool initialized;
} nfcAccessPlatform = { .maximumIssuerKeys = kHAPPlatformNfcAccessIssuerKeyListSize,
                        .maximumSuspendedDeviceCredentialKeys =
                                kHAPPlatformNfcAccessDeviceCredentialKeySuspendedListSize,
                        .maximumActiveDeviceCredentialKeys = kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize,
                        .configurationState = 0,
                        .initialized = false };

/**
 * Data structure of an NFC Access Issuer Key entry
 */
typedef struct {
    /**
     * Type of the issuer key
     */
    uint8_t type;

    /**
     * Public key
     */
    uint8_t key[NFC_ACCESS_ISSUER_KEY_BYTES];

    /**
     * Identifier that uniquely identifies the issuer key
     */
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];

    /**
     * Key is the HAP pairing LTPK for a Home user which is also persisted with HAP pairings
     */
    bool homeUserKey;
} NfcAccessIssuerKeyEntry;

/**
 * Data structure of an NFC Access Issuer Key list
 */
typedef struct {
    /**
     * Number of NFC Access Issuer Key entries
     */
    uint16_t numEntries;

    /**
     * NFC Access Issuer Key entries
     */
    NfcAccessIssuerKeyEntry entries[kHAPPlatformNfcAccessIssuerKeyListSize];
} NfcAccessIssuerKeyList;

/**
 * NFC Access Issuer Key list for non-Home users - stored in the key value storage
 */
static NfcAccessIssuerKeyList nfcAccessIssuerKeyList = { .numEntries = 0 };

/**
 * Data structure of an NFC Access Device Credential Key entry
 */
typedef struct {
    /**
     * Type of the device credential key
     */
    uint8_t type;

    /**
     * Public key
     */
    uint8_t key[NFC_ACCESS_DEVICE_CREDENTIAL_KEY_BYTES];

    /**
     * Issuer key identifier
     */
    uint8_t issuerKeyIdentifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];

    /**
     * State of the device credential key (active/suspended)
     */
    uint8_t state;

    /**
     * Identifier that uniquely identifies the credential key
     */
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];

    /**
     * Counter keeping track of when this key is added/used. This value is to be updated with the global counter and
     * is used to determine LRU key.
     */
    uint64_t counter;
} NfcAccessDeviceCredentialKeyEntry;

/**
 * Data structure of an NFC Access Device Credential Key list
 */
typedef struct {
    /**
     * Global counter used by each key entry when being added/used. To be incremented after each key entry uses the
     * current counter.
     */
    uint64_t counter;

    /**
     * Number of active NFC Access Device Credential Key entries
     *
     * NOTE: Do not update this directly, use
     * IncrementDeviceCredentialKeyNumEntries/DecrementDeviceCredentialKeyNumEntries
     */
    uint16_t numActiveEntries;

    /**
     * Number of suspended NFC Access Device Credential Key entries
     *
     * NOTE: Do not update this directly, use
     * IncrementDeviceCredentialKeyNumEntries/DecrementDeviceCredentialKeyNumEntries
     */
    uint16_t numSuspendedEntries;

    /**
     * Total number of NFC Access Device Credential Key entries (active and suspended)
     *
     * NOTE: Do not update this directly, use
     * IncrementDeviceCredentialKeyNumEntries/DecrementDeviceCredentialKeyNumEntries
     */
    uint16_t numEntries;

    /**
     * NFC Access Device Credential Key entries
     */
    NfcAccessDeviceCredentialKeyEntry
            entries[kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize +
                    kHAPPlatformNfcAccessDeviceCredentialKeySuspendedListSize];
} NfcAccessDeviceCredentialKeyList;

/**
 * NFC Access Device Credential Key list stored in the key value storage
 */
static NfcAccessDeviceCredentialKeyList nfcAccessDeviceCredentialKeyList = {
    .numActiveEntries = 0,
    .numSuspendedEntries = 0,
    .numEntries = 0,
    .counter = 0,
};

/**
 * Data structure of an NFC Access Reader Key entry
 */
typedef struct {
    /**
     * Type of the reader key
     */
    uint8_t type;

    /**
     * Private key
     */
    uint8_t key[NFC_ACCESS_READER_KEY_BYTES];

    /**
     * Identifier that uniquely identifies a reader
     */
    uint8_t readerIdentifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];

    /**
     * Identifier that uniquely identifies the reader key
     */
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];
} NfcAccessReaderKeyEntry;

static NfcAccessReaderKeyEntry nfcAccessReaderKey = { .type = 0 };

/**
 * log object
 */
static const HAPLogObject logObject = { .subsystem = kHAPPlatform_LogSubsystem, .category = "NfcAccess" };

/**
 * Key for storing issuer key list
 */
#define kKeyValueStoreKeyIssuerKeyList ((HAPPlatformKeyValueStoreKey) 0x01)

/**
 * Key for storing device credential (active and suspended) key list
 */
#define kKeyValueStoreKeyDeviceCredentialKeyList ((HAPPlatformKeyValueStoreKey) 0x02)

/**
 * Key for storing reader key
 */
#define kKeyValueStoreKeyReaderKey ((HAPPlatformKeyValueStoreKey) 0x03)

/**
 * Key for storing configuration state
 */
#define kKeyValueStoreKeyConfigurationState ((HAPPlatformKeyValueStoreKey) 0x04)

/**
 * Loads issuer key list into cache
 *
 * @return   kHAPError_Unknown if number of bytes loaded does not match expected size. Other errors otherwise.
 */
static HAPError HAPPlatformNfcAccessLoadIssuerKeyList(void) {
    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    size_t numBytes;
    bool found;

    HAPError err = HAPPlatformKeyValueStoreGet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyIssuerKeyList,
            &nfcAccessIssuerKeyList,
            sizeof nfcAccessIssuerKeyList,
            &numBytes,
            &found);
    if (err) {
        return err;
    }

    if (!found) {
        nfcAccessIssuerKeyList.numEntries = 0;
        return kHAPError_None;
    }

    if (numBytes != GET_LIST_BYTES(NfcAccessIssuerKeyList, nfcAccessIssuerKeyList, NfcAccessIssuerKeyEntry)) {
        HAPLogError(
                &logObject,
                "List size mismatch for issuer key list: actual=%zu, expected=%zu",
                numBytes,
                GET_LIST_BYTES(NfcAccessIssuerKeyList, nfcAccessIssuerKeyList, NfcAccessIssuerKeyEntry));
        return kHAPError_Unknown;
    }

    return kHAPError_None;
}

/**
 * Loads device credential key list into cache
 *
 * @return   kHAPError_Unknown if number of bytes loaded does not match expected size. Other errors otherwise.
 */
static HAPError HAPPlatformNfcAccessLoadDeviceCredentialKeyList(void) {
    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    size_t numBytes;
    bool found;

    HAPError err = HAPPlatformKeyValueStoreGet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyDeviceCredentialKeyList,
            &nfcAccessDeviceCredentialKeyList,
            sizeof nfcAccessDeviceCredentialKeyList,
            &numBytes,
            &found);
    if (err) {
        return err;
    }

    if (!found) {
        nfcAccessDeviceCredentialKeyList.counter = 0;
        nfcAccessDeviceCredentialKeyList.numActiveEntries = 0;
        nfcAccessDeviceCredentialKeyList.numSuspendedEntries = 0;
        nfcAccessDeviceCredentialKeyList.numEntries = 0;
        return kHAPError_None;
    }

    if (numBytes != GET_LIST_BYTES(
                            NfcAccessDeviceCredentialKeyList,
                            nfcAccessDeviceCredentialKeyList,
                            NfcAccessDeviceCredentialKeyEntry)) {
        HAPLogError(
                &logObject,
                "List size mismatch for device credential key list: actual=%zu, expected=%zu",
                numBytes,
                GET_LIST_BYTES(
                        NfcAccessDeviceCredentialKeyList,
                        nfcAccessDeviceCredentialKeyList,
                        NfcAccessDeviceCredentialKeyEntry));
        return kHAPError_Unknown;
    }

    return kHAPError_None;
}

/**
 * Loads reader key into cache
 *
 * @return   kHAPError_Unknown if number of bytes loaded does not match expected size. Other errors otherwise.
 */
static HAPError HAPPlatformNfcAccessLoadReaderKey(void) {
    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    size_t numBytes;
    bool found;

    HAPError err = HAPPlatformKeyValueStoreGet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyReaderKey,
            &nfcAccessReaderKey,
            sizeof nfcAccessReaderKey,
            &numBytes,
            &found);
    if (err) {
        return err;
    }

    if (!found) {
        nfcAccessReaderKey.type = 0;
        HAPRawBufferZero(&nfcAccessReaderKey, sizeof nfcAccessReaderKey);
        return kHAPError_None;
    }

    if (numBytes != sizeof nfcAccessReaderKey) {
        HAPLogError(
                &logObject,
                "List size mismatch for reader key: actual=%zu, expected=%zu",
                numBytes,
                sizeof nfcAccessReaderKey);
        return kHAPError_Unknown;
    }

    return kHAPError_None;
}

/**
 * Loads configuration state into cache
 *
 * @return   kHAPError_Unknown if number of bytes loaded does not match expected size. Other errors otherwise.
 */
static HAPError HAPPlatformNfcAccessLoadConfigurationState(void) {
    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    size_t numBytes;
    bool found;

    HAPError err = HAPPlatformKeyValueStoreGet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyConfigurationState,
            &nfcAccessPlatform.configurationState,
            sizeof nfcAccessPlatform.configurationState,
            &numBytes,
            &found);
    if (err) {
        return err;
    }

    if (!found) {
        nfcAccessPlatform.configurationState = 0;
        return kHAPError_None;
    }

    if (numBytes != sizeof nfcAccessPlatform.configurationState) {
        HAPLogError(
                &logObject,
                "Size mismatch for configuration state: actual=%zu, expected=%zu",
                numBytes,
                sizeof nfcAccessPlatform.configurationState);
        return kHAPError_Unknown;
    }

    return kHAPError_None;
}

/**
 * Finds the least recently used entry (smallest counter) from the device credential key list
 *
 * @param   state        The state of the device credential key to filter for
 * @param   index[out]   The index of the evicted entry
 *
 * @return A pointer to the entry that is least recently used for the state passed in
 */
static NfcAccessDeviceCredentialKeyEntry* _Nullable FindLRUDeviceCredentialEntry(
        uint8_t state,
        uint16_t* _Nonnull index) {
    HAPPrecondition(index);
    HAPAssert(
            nfcAccessDeviceCredentialKeyList.numActiveEntries >=
            HAPPlatformNfcAccessGetMaximumActiveDeviceCredentialKeys());

    NfcAccessDeviceCredentialKeyEntry* lruEntry = NULL;
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        NfcAccessDeviceCredentialKeyEntry* entry = &nfcAccessDeviceCredentialKeyList.entries[i];
        if (entry->state == state) {
            if (!lruEntry || (entry->counter < lruEntry->counter)) {
                lruEntry = entry;
                *index = i;
            }
        }
    }

    return lruEntry;
}

/**
 * Increment the device credential key entries based on the state of the key
 *
 * @param   state   The state of device credential key (active/suspended)
 */
static void IncrementDeviceCredentialKeyNumEntries(uint8_t state) {
    switch (state) {
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active:
            nfcAccessDeviceCredentialKeyList.numActiveEntries++;
            break;
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended:
            nfcAccessDeviceCredentialKeyList.numSuspendedEntries++;
            break;
    }
    nfcAccessDeviceCredentialKeyList.numEntries =
            nfcAccessDeviceCredentialKeyList.numActiveEntries + nfcAccessDeviceCredentialKeyList.numSuspendedEntries;
}

/**
 * Decrement the device credential key entries based on the state of the key
 *
 * @param   state   The state of device credential key (active/suspended)
 */
static void DecrementDeviceCredentialKeyNumEntries(uint8_t state) {
    switch (state) {
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active:
            nfcAccessDeviceCredentialKeyList.numActiveEntries--;
            break;
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended:
            nfcAccessDeviceCredentialKeyList.numSuspendedEntries--;
            break;
    }
    nfcAccessDeviceCredentialKeyList.numEntries =
            nfcAccessDeviceCredentialKeyList.numActiveEntries + nfcAccessDeviceCredentialKeyList.numSuspendedEntries;
}

/**
 * Increment the configuration state value and persist to memory
 *
 * @return Error from persisting to memory
 */
static HAPError IncrementConfigurationState(void) {
    nfcAccessPlatform.configurationState++;
    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyConfigurationState,
            &nfcAccessPlatform.configurationState,
            sizeof nfcAccessPlatform.configurationState);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        HAPFatalError();
    }

    if (nfcAccessPlatform.configurationStateChangeCallback) {
        nfcAccessPlatform.configurationStateChangeCallback();
    }
    return err;
}

/**
 * Update the state of a device credential entry. As a result, the corresponding entry counts and the entry counter are
 * also updated.
 *
 * @param   entry   The entry to update
 * @param   state   The state of device credential key to update to
 *
 * @return Error from persisting to memory
 */
static HAPError UpdateDeviceCredentialEntryState(NfcAccessDeviceCredentialKeyEntry* _Nonnull entry, uint8_t state) {
    HAPPrecondition(entry);

    entry->state = state;
    entry->counter = nfcAccessDeviceCredentialKeyList.counter++;

    switch (state) {
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active:
            IncrementDeviceCredentialKeyNumEntries(kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active);
            DecrementDeviceCredentialKeyNumEntries(
                    kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended);
            break;
        case kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended:
            IncrementDeviceCredentialKeyNumEntries(
                    kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended);
            DecrementDeviceCredentialKeyNumEntries(kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active);
            break;
    }

    if (nfcAccessDeviceCredentialKeyList.counter == 0) {
        // TODO: Handle overflow and reassign counter for all entries
    }

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyDeviceCredentialKeyList,
            &nfcAccessDeviceCredentialKeyList,
            GET_LIST_BYTES(
                    NfcAccessDeviceCredentialKeyList,
                    nfcAccessDeviceCredentialKeyList,
                    NfcAccessDeviceCredentialKeyEntry));
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    return kHAPError_None;
}

void HAPPlatformNfcAccessGenerateIdentifier(
        const uint8_t* _Nonnull key,
        size_t keyNumBytes,
        uint8_t* _Nonnull identifier) {
    HAPPrecondition(key);
    HAPPrecondition(identifier);

    uint8_t md[SHA256_BYTES];
    uint8_t dataNumBytes = HAPStringGetNumBytes(kHAPPlatformNfcAccessKeyIdentifierSalt) + keyNumBytes;
    uint8_t data[dataNumBytes];

    HAPRawBufferCopyBytes(
            data, kHAPPlatformNfcAccessKeyIdentifierSalt, HAPStringGetNumBytes(kHAPPlatformNfcAccessKeyIdentifierSalt));
    HAPRawBufferCopyBytes(&data[HAPStringGetNumBytes(kHAPPlatformNfcAccessKeyIdentifierSalt)], key, keyNumBytes);
    HAP_sha256(&md[0], data, dataNumBytes);

    HAPRawBufferCopyBytes(identifier, md, NFC_ACCESS_KEY_IDENTIFIER_BYTES);
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessCreate(
        HAPPlatformKeyValueStoreRef _Nonnull keyValueStore,
        HAPPlatformKeyValueStoreDomain storeDomain,
        HAPConfigurationStateChangeCallback _Nullable configurationStateChangeCallback,
        HAPNfcTransactionDetectedCallback _Nullable nfcTransactionDetectedCallback) {
    HAPPrecondition(keyValueStore);

    nfcAccessPlatform.keyValueStore = keyValueStore;
    nfcAccessPlatform.storeDomain = storeDomain;
    nfcAccessPlatform.configurationStateChangeCallback = configurationStateChangeCallback;
    nfcAccessPlatform.nfcTransactionDetectedCallback = nfcTransactionDetectedCallback;
    nfcAccessPlatform.initialized = true;

    HAPError err = HAPPlatformNfcAccessLoadIssuerKeyList();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadDeviceCredentialKeyList();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadReaderKey();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadConfigurationState();
    if (err) {
        return err;
    }

    // VENDOR-TODO: Platform is initialized and can start polling the reader

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessPurge(void) {
    // Purge NFC access store domain
    HAPError err = HAPPlatformKeyValueStorePurgeDomain(nfcAccessPlatform.keyValueStore, nfcAccessPlatform.storeDomain);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
    }

    // Reload all key lists
    err = HAPPlatformNfcAccessLoadIssuerKeyList();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadDeviceCredentialKeyList();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadReaderKey();
    if (err) {
        return err;
    }

    err = HAPPlatformNfcAccessLoadConfigurationState();
    if (err) {
        return err;
    }

    // VENDOR-TODO: Clear all keys from the reader

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessIssuerKeyList(
        HAPPlatformNfcAccessIssuerKey* _Nonnull issuerKeyList,
        uint8_t* _Nonnull numIssuerKeys,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(issuerKeyList);
    HAPPrecondition(numIssuerKeys);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    for (size_t i = 0; i < nfcAccessIssuerKeyList.numEntries; i++) {
        // Copy each issuer key identifier cached
        HAPRawBufferCopyBytes(
                issuerKeyList[i].identifier,
                nfcAccessIssuerKeyList.entries[i].identifier,
                NFC_ACCESS_KEY_IDENTIFIER_BYTES);
    }

    *numIssuerKeys = nfcAccessIssuerKeyList.numEntries;
    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessIssuerKeyAdd(
        const HAPPlatformNfcAccessIssuerKey* _Nonnull issuerKey,
        NfcAccessIssuerKeyCacheType cacheType,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(issuerKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Identifier is generated based on key value
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];
    HAPPlatformNfcAccessGenerateIdentifier(issuerKey->key, issuerKey->keyNumBytes, identifier);

    // Check for duplicate value
    for (size_t i = 0; i < nfcAccessIssuerKeyList.numEntries; i++) {
        // Checking for duplicate identifier implicitly checks for duplicate key
        if (HAPRawBufferAreEqual(
                    nfcAccessIssuerKeyList.entries[i].identifier, identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            if ((cacheType == NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_READ) &&
                nfcAccessIssuerKeyList.entries[i].homeUserKey) {
                // This is expected since existing HAP pairings should already have been previously added and persisted.
                // This is extra verification that all HAP pairings LTPK exist in the issuer key list.
                *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
                return kHAPError_None;
            }

            HAPLogError(&logObject, "%s: Identifier is a duplicate", __func__);
            *statusCode = NFC_ACCESS_STATUS_CODE_DUPLICATE;
            return kHAPError_None;
        }
    }

    if (nfcAccessIssuerKeyList.numEntries >= HAPPlatformNfcAccessGetMaximumIssuerKeys()) {
        HAPLogError(&logObject, "%s: Issuer key list is full", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_OUT_OF_RESOURCES;
        return kHAPError_None;
    }

    NfcAccessIssuerKeyEntry* entry = &nfcAccessIssuerKeyList.entries[nfcAccessIssuerKeyList.numEntries];
    HAPAssert(issuerKey->keyNumBytes <= sizeof entry->key);

    // Make sure the key can be added
    switch (cacheType) {
        case NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_READ:
            // This cache type should never be used to explicitly add a HAP pairing LTPK as the issuer key. Allowing an
            // add here to catch any off chance that HAP pairings and issuer key list are out of sync.
        case NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_ADD:
            entry->homeUserKey = true;
            break;
        case NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_REQUEST_ADD:
            entry->homeUserKey = false;
            break;
        default:
            HAPLogError(&logObject, "%s: Cache type not allowed to add issuer key=%d", __func__, cacheType);
            return kHAPError_InvalidData;
    }

    entry->type = issuerKey->type;
    HAPRawBufferCopyBytes(entry->key, issuerKey->key, issuerKey->keyNumBytes);
    HAPRawBufferCopyBytes(entry->identifier, identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);

    // VENDOR-TODO: Add issuer key to the reader

    nfcAccessIssuerKeyList.numEntries++;

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyIssuerKeyList,
            &nfcAccessIssuerKeyList,
            GET_LIST_BYTES(NfcAccessIssuerKeyList, nfcAccessIssuerKeyList, NfcAccessIssuerKeyEntry));
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessIssuerKeyRemove(
        const HAPPlatformNfcAccessIssuerKey* _Nonnull issuerKey,
        NfcAccessIssuerKeyCacheType cacheType,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(issuerKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Make sure the key can be removed
    if ((cacheType != NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_REMOVE) &&
        (cacheType != NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_REQUEST_REMOVE)) {
        HAPLogError(&logObject, "%s: Cache type not allowed to remove issuer key=%d", __func__, cacheType);
        return kHAPError_InvalidData;
    }

    // Look for the identifier to remove
    bool found = false;
    for (size_t i = 0; i < nfcAccessIssuerKeyList.numEntries; i++) {
        if (HAPRawBufferAreEqual(
                    nfcAccessIssuerKeyList.entries[i].identifier,
                    issuerKey->identifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            if (nfcAccessIssuerKeyList.entries[i].homeUserKey &&
                (cacheType != NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_REMOVE)) {
                // Issuer keys for Home users can only be removed by an unpair
                HAPLogError(&logObject, "%s: Cannot remove issuer key", __func__);
                *statusCode = NFC_ACCESS_STATUS_CODE_NOT_SUPPORTED;
                return kHAPError_None;
            }

            // VENDOR-TODO: Remove issuer key from the reader

            // Delete the entry by copying over the rest
            size_t remainderCount = nfcAccessIssuerKeyList.numEntries - i - 1;
            if (remainderCount > 0) {
                HAPRawBufferCopyBytes(
                        &nfcAccessIssuerKeyList.entries[i],
                        &nfcAccessIssuerKeyList.entries[i + 1],
                        remainderCount * sizeof(NfcAccessIssuerKeyEntry));
            }
            nfcAccessIssuerKeyList.numEntries--;
            found = true;
            break;
        }
    }

    if (!found) {
        HAPLogError(&logObject, "%s: Key not found", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_DOES_NOT_EXIST;
        return kHAPError_None;
    }

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyIssuerKeyList,
            &nfcAccessIssuerKeyList,
            GET_LIST_BYTES(NfcAccessIssuerKeyList, nfcAccessIssuerKeyList, NfcAccessIssuerKeyEntry));
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    // Remove all device credential keys associated with this issuer key
    found = false;
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        if (HAPRawBufferAreEqual(
                    nfcAccessDeviceCredentialKeyList.entries[i].issuerKeyIdentifier,
                    issuerKey->identifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            // VENDOR-TODO: Remove device credential key from the reader

            uint8_t state = nfcAccessDeviceCredentialKeyList.entries[i].state;

            // Delete the entry by copying over the rest
            size_t remainderCount = nfcAccessDeviceCredentialKeyList.numEntries - i - 1;
            if (remainderCount > 0) {
                HAPRawBufferCopyBytes(
                        &nfcAccessDeviceCredentialKeyList.entries[i],
                        &nfcAccessDeviceCredentialKeyList.entries[i + 1],
                        remainderCount * sizeof(NfcAccessDeviceCredentialKeyEntry));
                // Rest of entries are copied over so check the current index again
                i--;
            }

            DecrementDeviceCredentialKeyNumEntries(state);
            found = true;
        }
    }
    if (found) {
        HAPError err = HAPPlatformKeyValueStoreSet(
                nfcAccessPlatform.keyValueStore,
                nfcAccessPlatform.storeDomain,
                kKeyValueStoreKeyDeviceCredentialKeyList,
                &nfcAccessDeviceCredentialKeyList,
                GET_LIST_BYTES(
                        NfcAccessDeviceCredentialKeyList,
                        nfcAccessDeviceCredentialKeyList,
                        NfcAccessDeviceCredentialKeyEntry));
        if (err) {
            HAPAssert(err == kHAPError_Unknown);
            return err;
        }
    }

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessDeviceCredentialKeyList(
        HAPPlatformNfcAccessDeviceCredentialKey* _Nonnull deviceCredentialKeyList,
        uint8_t* _Nonnull numDeviceCredentialKeys,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(deviceCredentialKeyList);
    HAPPrecondition(numDeviceCredentialKeys);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    uint8_t numKeys = 0;
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        // Only return entries with matching state
        if (deviceCredentialKeyList[numKeys].state == nfcAccessDeviceCredentialKeyList.entries[i].state) {
            // Copy each device credential key identifier and issuer key identifier cached
            HAPRawBufferCopyBytes(
                    deviceCredentialKeyList[numKeys].identifier,
                    nfcAccessDeviceCredentialKeyList.entries[i].identifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES);
            HAPRawBufferCopyBytes(
                    deviceCredentialKeyList[numKeys].issuerKeyIdentifier,
                    nfcAccessDeviceCredentialKeyList.entries[i].issuerKeyIdentifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES);
            numKeys++;
        }
    }

    *numDeviceCredentialKeys = numKeys;
    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessDeviceCredentialKeyAdd(
        const HAPPlatformNfcAccessDeviceCredentialKey* _Nonnull deviceCredentialKey,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(deviceCredentialKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Identifier is generated based on key value
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];
    HAPPlatformNfcAccessGenerateIdentifier(deviceCredentialKey->key, deviceCredentialKey->keyNumBytes, identifier);

    // Check if issuer key identifier has been cached
    bool issuerKeyFound = false;
    for (size_t i = 0; i < nfcAccessIssuerKeyList.numEntries; i++) {
        if (HAPRawBufferAreEqual(
                    nfcAccessIssuerKeyList.entries[i].identifier,
                    deviceCredentialKey->issuerKeyIdentifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            issuerKeyFound = true;
            break;
        }
    }

    if (!issuerKeyFound) {
        HAPLogError(&logObject, "%s: Issuer key identifier cannot be found", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_DOES_NOT_EXIST;
        return kHAPError_None;
    }

    // Check for duplicate value
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        // Checking for duplicate identifier implicitly checks for duplicate key
        if (HAPRawBufferAreEqual(
                    nfcAccessDeviceCredentialKeyList.entries[i].identifier,
                    identifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            if (nfcAccessDeviceCredentialKeyList.entries[i].state == deviceCredentialKey->state) {
                HAPLogError(&logObject, "%s: Identifier is a duplicate", __func__);
                *statusCode = NFC_ACCESS_STATUS_CODE_DUPLICATE;
                return kHAPError_None;
            } else {
                // Treat this like a state update if device credential key already exists but state differs
                HAPError err = UpdateDeviceCredentialEntryState(
                        &nfcAccessDeviceCredentialKeyList.entries[i], deviceCredentialKey->state);
                if (err) {
                    return err;
                }

                // VENDOR-TODO: Update the state of the device credential key on the reader
                *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
                return kHAPError_None;
            }
        }
    }

    if ((deviceCredentialKey->state == kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Suspended) &&
        (nfcAccessDeviceCredentialKeyList.numSuspendedEntries >=
         HAPPlatformNfcAccessGetMaximumSuspendedDeviceCredentialKeys())) {
        HAPLogError(&logObject, "%s: Suspended device credential key list is full", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_OUT_OF_RESOURCES;
        return kHAPError_None;
    }

    NfcAccessDeviceCredentialKeyEntry* entry = NULL;
    bool entryEvicted = false;
    uint16_t index = 0;
    if ((deviceCredentialKey->state == kHAPCharacteristicValue_NfcAccessDeviceCredentialKey_State_Active) &&
        (nfcAccessDeviceCredentialKeyList.numActiveEntries >=
         HAPPlatformNfcAccessGetMaximumActiveDeviceCredentialKeys())) {
        HAPLogError(
                &logObject, "%s: Active device credential key list is full, evicting least recently used", __func__);
        // Evict the least recently used entry
        entry = FindLRUDeviceCredentialEntry(deviceCredentialKey->state, &index);
        DecrementDeviceCredentialKeyNumEntries(deviceCredentialKey->state);
        entryEvicted = true;
    }

    // There are free entries to be used (eviction did not need to occur)
    if (!entryEvicted) {
        entry = &nfcAccessDeviceCredentialKeyList.entries[nfcAccessDeviceCredentialKeyList.numEntries];
    }

    HAPAssert(deviceCredentialKey->keyNumBytes <= sizeof entry->key);

    entry->type = deviceCredentialKey->type;
    entry->state = deviceCredentialKey->state;
    entry->counter = nfcAccessDeviceCredentialKeyList.counter++;
    HAPRawBufferCopyBytes(entry->key, deviceCredentialKey->key, deviceCredentialKey->keyNumBytes);
    HAPRawBufferCopyBytes(
            entry->issuerKeyIdentifier, deviceCredentialKey->issuerKeyIdentifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);
    HAPRawBufferCopyBytes(entry->identifier, identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);

    // VENDOR-TODO: Either add device credential key to the reader or update the entry with a new key if the LRU is
    // evicted

    IncrementDeviceCredentialKeyNumEntries(deviceCredentialKey->state);

    if (nfcAccessDeviceCredentialKeyList.counter == 0) {
        // TODO: Handle overflow and reassign counter for all entries
    }

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyDeviceCredentialKeyList,
            &nfcAccessDeviceCredentialKeyList,
            GET_LIST_BYTES(
                    NfcAccessDeviceCredentialKeyList,
                    nfcAccessDeviceCredentialKeyList,
                    NfcAccessDeviceCredentialKeyEntry));
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessDeviceCredentialKeyRemove(
        const HAPPlatformNfcAccessDeviceCredentialKey* _Nonnull deviceCredentialKey,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(deviceCredentialKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Look for the identifier to remove
    bool found = false;
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        if (HAPRawBufferAreEqual(
                    nfcAccessDeviceCredentialKeyList.entries[i].identifier,
                    deviceCredentialKey->identifier,
                    NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
            uint8_t state = nfcAccessDeviceCredentialKeyList.entries[i].state;

            // VENDOR-TODO: Remove device credential key from the reader

            // Delete the entry by copying over the rest
            size_t remainderCount = nfcAccessDeviceCredentialKeyList.numEntries - i - 1;
            if (remainderCount > 0) {
                HAPRawBufferCopyBytes(
                        &nfcAccessDeviceCredentialKeyList.entries[i],
                        &nfcAccessDeviceCredentialKeyList.entries[i + 1],
                        remainderCount * sizeof(NfcAccessDeviceCredentialKeyEntry));
            }
            DecrementDeviceCredentialKeyNumEntries(state);
            found = true;
            break;
        }
    }

    if (!found) {
        HAPLogError(&logObject, "%s: Key not found", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_DOES_NOT_EXIST;
        return kHAPError_None;
    }

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyDeviceCredentialKeyList,
            &nfcAccessDeviceCredentialKeyList,
            GET_LIST_BYTES(
                    NfcAccessDeviceCredentialKeyList,
                    nfcAccessDeviceCredentialKeyList,
                    NfcAccessDeviceCredentialKeyEntry));
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessReaderKeyList(
        HAPPlatformNfcAccessReaderKey* _Nonnull readerKey,
        bool* _Nonnull readerKeyFound,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(readerKey);
    HAPPrecondition(readerKeyFound);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    *readerKeyFound = false;
    if (nfcAccessReaderKey.type != 0) {
        // Copy Reader Key identifier cached
        HAPRawBufferCopyBytes(readerKey->identifier, nfcAccessReaderKey.identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);
        *readerKeyFound = true;
    }

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessReaderKeyAdd(
        const HAPPlatformNfcAccessReaderKey* _Nonnull readerKey,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(readerKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Identifier is generated based on key value
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];
    HAPPlatformNfcAccessGenerateIdentifier(readerKey->key, readerKey->keyNumBytes, identifier);

    // Checking for duplicate identifier implicitly checks for duplicate key
    if (HAPRawBufferAreEqual(nfcAccessReaderKey.identifier, identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
        HAPLogError(&logObject, "%s: Identifier is a duplicate", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_DUPLICATE;
        return kHAPError_None;
    }

    // Check for an existing key (non-duplicate case)
    if (nfcAccessReaderKey.type != 0) {
        HAPLogError(&logObject, "%s: Reader key already exists", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_OUT_OF_RESOURCES;
        return kHAPError_None;
    }

    HAPAssert(readerKey->keyNumBytes <= sizeof nfcAccessReaderKey.key);

    nfcAccessReaderKey.type = readerKey->type;
    HAPRawBufferCopyBytes(nfcAccessReaderKey.key, readerKey->key, readerKey->keyNumBytes);
    HAPRawBufferCopyBytes(
            nfcAccessReaderKey.readerIdentifier, readerKey->readerIdentifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);
    HAPRawBufferCopyBytes(nfcAccessReaderKey.identifier, identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES);

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyReaderKey,
            &nfcAccessReaderKey,
            sizeof nfcAccessReaderKey);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    // VENDOR-TODO: Add reader key to the reader

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessReaderKeyRemove(
        const HAPPlatformNfcAccessReaderKey* _Nonnull readerKey,
        NfcAccessStatusCode* _Nonnull statusCode) {
    HAPPrecondition(readerKey);
    HAPPrecondition(statusCode);

    if (!nfcAccessPlatform.initialized) {
        HAPLogError(&logObject, "%s: Platform not initialized", __func__);
        return kHAPError_InvalidState;
    }

    // Look for the identifier to remove
    if (HAPRawBufferAreEqual(nfcAccessReaderKey.identifier, readerKey->identifier, NFC_ACCESS_KEY_IDENTIFIER_BYTES)) {
        // Delete the entry
        HAPRawBufferZero(&nfcAccessReaderKey, sizeof nfcAccessReaderKey);
    } else {
        HAPLogError(&logObject, "%s: Key not found", __func__);
        *statusCode = NFC_ACCESS_STATUS_CODE_DOES_NOT_EXIST;
        return kHAPError_None;
    }

    HAPError err = HAPPlatformKeyValueStoreSet(
            nfcAccessPlatform.keyValueStore,
            nfcAccessPlatform.storeDomain,
            kKeyValueStoreKeyReaderKey,
            &nfcAccessReaderKey,
            sizeof nfcAccessReaderKey);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    err = IncrementConfigurationState();
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        return err;
    }

    // VENDOR-TODO: Remove reader key from the reader

    *statusCode = NFC_ACCESS_STATUS_CODE_SUCCESS;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
uint16_t HAPPlatformNfcAccessGetMaximumIssuerKeys(void) {
    return nfcAccessPlatform.maximumIssuerKeys;
}

HAP_RESULT_USE_CHECK
uint16_t HAPPlatformNfcAccessGetMaximumActiveDeviceCredentialKeys(void) {
    return nfcAccessPlatform.maximumActiveDeviceCredentialKeys;
}

HAP_RESULT_USE_CHECK
uint16_t HAPPlatformNfcAccessGetMaximumSuspendedDeviceCredentialKeys(void) {
    return nfcAccessPlatform.maximumSuspendedDeviceCredentialKeys;
}

HAP_RESULT_USE_CHECK
uint16_t HAPPlatformNfcAccessGetConfigurationState(void) {
    return nfcAccessPlatform.configurationState;
}

#if (HAP_TESTING == 1)
HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessSetMaximumIssuerKeys(uint16_t value) {
    if (value > kHAPPlatformNfcAccessIssuerKeyListSize) {
        HAPLog(&logObject, "Maximum number of issuer keys cannot exceed %d", kHAPPlatformNfcAccessIssuerKeyListSize);
        return kHAPError_InvalidData;
    }

    nfcAccessPlatform.maximumIssuerKeys = value;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessSetMaximumActiveDeviceCredentialKeys(uint16_t value) {
    if (value > kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize) {
        HAPLog(&logObject,
               "Maximum number of active device credential keys cannot exceed %d",
               kHAPPlatformNfcAccessDeviceCredentialKeyActiveListSize);
        return kHAPError_InvalidData;
    }

    nfcAccessPlatform.maximumActiveDeviceCredentialKeys = value;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformNfcAccessSetMaximumSuspendedDeviceCredentialKeys(uint16_t value) {
    if (value > kHAPPlatformNfcAccessDeviceCredentialKeySuspendedListSize) {
        HAPLog(&logObject,
               "Maximum number of suspended device credential keys cannot exceed %d",
               kHAPPlatformNfcAccessDeviceCredentialKeySuspendedListSize);
        return kHAPError_InvalidData;
    }

    nfcAccessPlatform.maximumSuspendedDeviceCredentialKeys = value;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError FindDeviceCredentialKeyIndex(uint8_t* _Nonnull key, uint16_t* _Nonnull index) {
    HAPPrecondition(key);
    HAPPrecondition(index);

    bool found = false;
    for (size_t i = 0; i < nfcAccessDeviceCredentialKeyList.numEntries; i++) {
        if (HAPRawBufferAreEqual(
                    nfcAccessDeviceCredentialKeyList.entries[i].key, key, NFC_ACCESS_DEVICE_CREDENTIAL_KEY_BYTES)) {
            *index = i;
            found = true;
            break;
        }
    }

    if (!found) {
        return kHAPError_Unknown;
    }

    return kHAPError_None;
}
#endif

#endif
