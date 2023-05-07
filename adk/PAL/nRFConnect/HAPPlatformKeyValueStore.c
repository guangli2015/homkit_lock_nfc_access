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
// Copyright (C) 2015-2020 Apple Inc. All Rights Reserved.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "HAPAssert.h"
#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformKeyValueStore+SDKDomains.h"

LOG_MODULE_REGISTER(pal_key_value_store, CONFIG_PAL_KEY_VALUE_STORE_LOG_LEVEL);

/**
 * Using "happl"/"fileID"/"recordID" as the tree structure for the settings subsys.
 *
 */

#define MODULE_NAME            "happl"
#define MAX_RECORD_LEN         60
#define kMaxDomainSubtreeDepth 40

#define SIZE_OF_WORD      4
#define PADDING_INFO_SIZE 1

#define SIZE_IN_WORDS(bytes) (((bytes) / SIZE_OF_WORD) + (size_t)((numBytesWithPaddingInfo) % SIZE_OF_WORD != 0))

#define DOMAIN_CHARS    5
#define KEY_CHARS       5

static char separator_char = '/';

struct settings_load_args {
    char file_record_key[MAX_RECORD_LEN];
    void* buf;
    size_t file_record_size;
    bool is_loading;
    bool is_enumerating;
    bool record_found;
} settings_load_args;

static struct {
    bool initialized;
    uint8_t* IOBuffor;
    size_t IOBufforCapacity;

    /* Since all the set handlers in the settings are called once before the control reaches back to
     * the caller of settings_load, we need to save the found keys under the given domain subtree
     */
    uint8_t keyDepthCount;
    uint8_t keys[kMaxDomainSubtreeDepth];

    char domainKeyID[MAX_RECORD_LEN];
    struct k_mutex settings_load_subtree_mutex;
} local_context;

struct direct_loader_param {
// arbitrary value, optimized for maximum expected number of keys under domain
// in case of bigger domain, purge will still work - it will use tail recurency to remove all elements from domain
#define MAX_REPO_SIZE 10
    uint8_t id_repo[MAX_REPO_SIZE];
    uint8_t id;
    enum { SUCCESS, NO_SPACE_FOR_ID } status;
};

static int setFromSettings(const char* key, size_t len, settings_read_cb read_cb, void* cb_arg) {
    if (local_context.initialized) {
        // loading from record and enumerating cannot happen at the same time
        HAPPrecondition(!(settings_load_args.is_loading && settings_load_args.is_enumerating));
        if (settings_load_args.is_loading) {

            // load the value from persistance storage into given buffer
            if (!strcmp(key, settings_load_args.file_record_key)) {
                // Found the record
                size_t read_len = read_cb(cb_arg, settings_load_args.buf, len);

                settings_load_args.file_record_size = read_len;
                settings_load_args.record_found = true;

                // Found the key, stop loading more values into the buffer
                settings_load_args.is_loading = false;
            }
        }

        else if (settings_load_args.is_enumerating) {
            // just compare the key and extract the recordId string
            if (!strncmp(key, settings_load_args.file_record_key, DOMAIN_CHARS)) {
                // New RecordID found, return this the caller
                if (local_context.keyDepthCount >= kMaxDomainSubtreeDepth) {
                    LOG_ERR("More keys under given domain. Increase kMaxDomainSubtreeDepth value");
                    HAPFatalError();
                }

                const int key_begin  = DOMAIN_CHARS + sizeof(separator_char);
                if (strlen(key) <= key_begin) {
                    LOG_DBG("There is no key in this file |%s| (len = %d)", key, key_begin);
                    return 0;
                }

                const char* recordID_str = (char*) &key[key_begin];
                const uint8_t recordID_int = atoi(recordID_str);

                if ((0 == recordID_int) && (NULL == strstr(recordID_str,"0"))) {
                    LOG_DBG("Invalid key |%s|", recordID_str);
                }
                else {
                    local_context.keys[local_context.keyDepthCount++] = recordID_int;
                    settings_load_args.record_found = true;
                }
            }
        }
    }
    return 0;
}

static int commit(void) {
    // All settings are loaded
    settings_load_args.is_loading = false;
    settings_load_args.is_enumerating = false;

    return 0;
}

static struct settings_handler HAPPlatform_settings = {
    .name = MODULE_NAME,
    .h_set = setFromSettings,
    .h_commit = commit,
};

static int direct_loader(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg, void* param) {
    struct direct_loader_param* loader = (struct direct_loader_param*) param;
    if (loader->id < MAX_REPO_SIZE) {
        loader->id_repo[loader->id++] = (uint8_t) atoi(name);
        loader->status = SUCCESS;
    } else {
        loader->status = NO_SPACE_FOR_ID;
    }

    return loader->status;
}

static char digit_to_char(uint8_t digit) {
    static char map[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    if (digit < sizeof(map))
        return map[digit];
    else
        return ' ';
}

static void print_dec(char* out, size_t val, uint8_t chars) {
    if (val == 0) {
        memcpy(out, "    0", 6);
        return;
    }
    for (int8_t i = chars - 1; i >= 0; i--) {
        if (val != 0) {
            out[i] = digit_to_char(val % 10);
            val /= 10;
        } else {
            out[i] = ' ';
        }
    }
    out[chars] = 0;
}

static void domainKeyId_query(
        char* out,
        size_t capacity,
        const char* module,
        HAPPlatformKeyValueStoreDomain* domain,
        HAPPlatformKeyValueStoreKey* key) {
    HAPAssert(capacity >= strlen(module ? module : "") + sizeof(separator_char) + DOMAIN_CHARS + sizeof(separator_char) + KEY_CHARS + 1);

    memset(out, 0, capacity);
    size_t written = 0;
    if (module) {
        memcpy(out, module, strlen(module));
        written += strlen(module);
        out[written++] = separator_char;
    }

    if (domain) {
        print_dec(&out[written], (size_t) *domain, DOMAIN_CHARS);
        written += DOMAIN_CHARS;
    }

    if (key) {
        out[written++] = separator_char;
        print_dec(&out[written], *key, KEY_CHARS);
        written += KEY_CHARS;
    }
    out[written] = '\0';
}

// ///////////////////////////////////////////////////////////////////////////////////////////////

void HAPPlatformKeyValueStoreCreate(
        HAPPlatformKeyValueStoreRef keyValueStore,
        const HAPPlatformKeyValueStoreOptions* options) {
    int err;
    HAPPrecondition(keyValueStore);
    HAPPrecondition(options);
    HAPPrecondition(!((uintptr_t) options->bytes % SIZE_OF_WORD));
    HAPPrecondition(options->maxBytes >= 2);

    keyValueStore->_ = *options;
    keyValueStore->busy = true;
    keyValueStore->initialized = false;
    local_context.initialized = false;
    keyValueStore->peakNumBytes = 0;
    settings_load_args.is_loading = 0;
    settings_load_args.is_enumerating = 0;
    settings_load_args.record_found = 0;

    local_context.IOBuffor = keyValueStore->_.bytes;
    local_context.IOBufforCapacity = keyValueStore->_.maxBytes;
    k_mutex_init(&local_context.settings_load_subtree_mutex);

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %lu (Code may use too much flash).", (unsigned long) err);
        HAPFatalError();
    }

    // Register event handler with FDS.
    err = settings_register(&HAPPlatform_settings);
    if (err) {
        LOG_ERR("settings_register failed: %lu", (unsigned long) err);
        HAPFatalError();
    }

    keyValueStore->initialized = true;
    local_context.initialized = true;

    err = settings_load();
    if (err) {
        LOG_ERR("Cannot load settings");
        HAPFatalError();
    }
    keyValueStore->busy = false;
}

HAP_RESULT_USE_CHECK
bool HAPPlatformKeyValueStoreIsBusy(HAPPlatformKeyValueStoreRef keyValueStore) {
    return keyValueStore->busy;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStoreGet(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain,
        HAPPlatformKeyValueStoreKey key,
        void* _Nullable bytes,
        size_t maxBytes,
        size_t* _Nullable numBytes,
        bool* found) HAP_DIAGNOSE_ERROR(!bytes && maxBytes, "empty buffer cannot have a length")
        HAP_DIAGNOSE_ERROR(
                (bytes && !numBytes) || (!bytes && numBytes),
                "length is only available if a buffer is supplied") {
    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    HAPPrecondition(!maxBytes || bytes);
    HAPPrecondition((bytes == NULL) == (numBytes == NULL));
    HAPPrecondition(found);
    k_mutex_lock(&local_context.settings_load_subtree_mutex, K_FOREVER);
    HAPPrecondition(!settings_load_args.is_enumerating);
    HAPPrecondition(!settings_load_args.is_loading);

    keyValueStore->busy = true;
    settings_load_args.is_loading = true;
    settings_load_args.buf = (void*) local_context.IOBuffor;
    settings_load_args.record_found = false;

    domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, &key);
    domainKeyId_query(
            settings_load_args.file_record_key, sizeof(settings_load_args.file_record_key), NULL, &domain, &key);

    int err = settings_load_subtree(local_context.domainKeyID);
    settings_load_args.is_loading = false;
    if (err) {
        LOG_ERR("Cannot load settings");
        keyValueStore->busy = false;
        *found = false;
        HAPFatalError();
    }

    *found = settings_load_args.record_found;
    k_mutex_unlock(&local_context.settings_load_subtree_mutex);
    if (!settings_load_args.record_found) {
        if (numBytes != NULL) {
            *numBytes = 0;
        }
        keyValueStore->busy = false;
        return kHAPError_None;
    }

    uint16_t numWords = (settings_load_args.file_record_size / SIZE_OF_WORD);

    // First byte contains number of padded bytes.
    if (!numWords) {
        LOG_INF("Corrupted file %02X.%02X contains no number of padded bytes.", domain, key);
        keyValueStore->busy = false;
        return kHAPError_Unknown;
    }

    uint8_t* read_data = (uint8_t*) settings_load_args.buf;

    uint8_t numPaddedBytes = read_data[0];
    if (numPaddedBytes >= SIZE_OF_WORD) {
        LOG_INF("Corrupted file %02X.%02X contains invalid padding length %u.", domain, key, numPaddedBytes);
        keyValueStore->busy = false;
        return kHAPError_Unknown;
    }

    if (bytes) {
        // Copy content.
        *numBytes = (size_t) numWords * SIZE_OF_WORD - PADDING_INFO_SIZE - numPaddedBytes;
        if (*numBytes > maxBytes) {
            *numBytes = maxBytes;
        }
        HAPRawBufferCopyBytes(bytes, &read_data[1], *numBytes);
    }
    keyValueStore->busy = false;
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStoreSet(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain,
        HAPPlatformKeyValueStoreKey key,
        const void* bytes,
        size_t numBytes) {

    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    HAPPrecondition(bytes);
    HAPPrecondition(numBytes <= UINT16_MAX / SIZE_OF_WORD);
    k_mutex_lock(&local_context.settings_load_subtree_mutex, K_FOREVER);
    keyValueStore->busy = true;

    HAPRawBufferCopyBytes(local_context.IOBuffor + PADDING_INFO_SIZE, bytes, numBytes);

    size_t numBytesWithPaddingInfo = numBytes + PADDING_INFO_SIZE;
    size_t numWords = SIZE_IN_WORDS(numBytesWithPaddingInfo);

    size_t numPaddedBytes = numWords * SIZE_OF_WORD - numBytesWithPaddingInfo;
    HAPRawBufferZero(local_context.IOBuffor + numBytesWithPaddingInfo, numPaddedBytes);
    local_context.IOBuffor[0] = numPaddedBytes;

    size_t bytes_with_padding = PADDING_INFO_SIZE + numBytes + numPaddedBytes;

    HAPAssert(bytes_with_padding % SIZE_OF_WORD == 0);

    domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, &key);

    int err = settings_save_one(local_context.domainKeyID, local_context.IOBuffor, bytes_with_padding);
    if (err) {
        LOG_INF("Failed to write a record");
        keyValueStore->busy = false;
        HAPFatalError();
    }

    if (keyValueStore->peakNumBytes > bytes_with_padding)
        keyValueStore->peakNumBytes = bytes_with_padding;
    int r = settings_save(); // todo: RG handle error ?
    HAPAssert(r == 0);
    keyValueStore->busy = false;
    k_mutex_unlock(&local_context.settings_load_subtree_mutex);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStoreOverrideAndSet(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain,
        HAPPlatformKeyValueStoreKey key,
        const void* bytes,
        size_t numBytes) {
    int err = 0;
    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    HAPPrecondition(bytes);

    keyValueStore->busy = true;
    // same API for update or new write
    err = HAPPlatformKeyValueStoreSet(keyValueStore, domain, key, bytes, numBytes);
    keyValueStore->busy = false;
    return err;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStoreRemove(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain,
        HAPPlatformKeyValueStoreKey key) {
    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    k_mutex_lock(&local_context.settings_load_subtree_mutex, K_FOREVER);
    keyValueStore->busy = true;

    domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, &key);

    // removing the exact matched key (not a subtree)
    settings_delete(local_context.domainKeyID);

    keyValueStore->busy = false;
    k_mutex_unlock(&local_context.settings_load_subtree_mutex);
    return kHAPError_None;
}

static HAPError call_enumerate_callbacks(
        uint8_t count,
        HAPPlatformKeyValueStoreDomain domain,
        uint8_t* keys,
        void* context,
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreEnumerateCallback callback) {
    uint8_t local_keys[kMaxDomainSubtreeDepth] = { 0 };
    memcpy(local_keys, keys, count);
    bool shouldContinue = true;
    int err = 0;

    LOG_INF("keyDepthCount %d", local_context.keyDepthCount);
    for (int i = 0; i < count && shouldContinue; i++) {
        LOG_INF("keys[%d] =  %x", i, local_keys[i]);
        err = callback(context, keyValueStore, domain, local_keys[i], &shouldContinue);
        if (err != kHAPError_None) {
            LOG_ERR("enumerate callback returned error (%d) on domain 0x%x key 0x%x", err, domain, local_keys[i]);
            return kHAPError_Unknown;
        }
    }
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStoreEnumerate(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain,
        HAPPlatformKeyValueStoreEnumerateCallback callback,
        void* _Nullable context) {
    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    HAPPrecondition(callback);
    k_mutex_lock(&local_context.settings_load_subtree_mutex, K_FOREVER);
    HAPPrecondition(!settings_load_args.is_enumerating);
    HAPPrecondition(!settings_load_args.is_loading);

    domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, NULL);
    domainKeyId_query(
            settings_load_args.file_record_key, sizeof(settings_load_args.file_record_key), NULL, &domain, NULL);
    settings_load_args.is_enumerating = true;
    settings_load_args.buf = NULL;
    settings_load_args.record_found = false;

    // reset the enumerating counter and found keys buffer
    local_context.keyDepthCount = 0;
    HAPRawBufferZero(local_context.keys, kMaxDomainSubtreeDepth);

    int err = settings_load_subtree(local_context.domainKeyID);
    settings_load_args.is_enumerating = false;
    k_mutex_unlock(&local_context.settings_load_subtree_mutex);
    if (err) {
        LOG_ERR("Cannot load settings");
        HAPFatalError();
    }

    return call_enumerate_callbacks(
            local_context.keyDepthCount, domain, local_context.keys, context, keyValueStore, callback);
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformKeyValueStorePurgeDomain(
        HAPPlatformKeyValueStoreRef keyValueStore,
        HAPPlatformKeyValueStoreDomain domain) {
    HAPPrecondition(keyValueStore);
    HAPPrecondition(keyValueStore->initialized);
    k_mutex_lock(&local_context.settings_load_subtree_mutex, K_FOREVER);
    HAPPrecondition(!settings_load_args.is_enumerating);
    HAPPrecondition(!settings_load_args.is_loading);

    keyValueStore->busy = true;
    memset(local_context.domainKeyID, 0, sizeof(local_context.domainKeyID));
    domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, NULL);

    LOG_INF("Remove subtree %s", local_context.domainKeyID);
    settings_save();

    // deleting the subtree, using direct loader feature

    struct direct_loader_param subtree_keys;
    memset(&subtree_keys, 0, sizeof(struct direct_loader_param));

    int err = settings_load_subtree_direct(local_context.domainKeyID, direct_loader, (void*) &subtree_keys);
    if (err) {
        LOG_INF("Failed to iterate through domain records");
        HAPFatalError();
    } else {
        // records deleted successfully, now delete the file itself
        LOG_INF("subtree iterate successfull: found %d elements\n status = %d", subtree_keys.id, subtree_keys.status);
        for (uint8_t i = 0; i < subtree_keys.id; i++) {
            memset(local_context.domainKeyID, 0, sizeof(local_context.domainKeyID));
            domainKeyId_query(
                    local_context.domainKeyID,
                    sizeof(local_context.domainKeyID),
                    MODULE_NAME,
                    &domain,
                    &subtree_keys.id_repo[i]);
            LOG_INF("Delete %s", local_context.domainKeyID);
            err = settings_delete(local_context.domainKeyID);
            if (err) {
                LOG_ERR("Delete %s failed with err %d", local_context.domainKeyID, err);
                HAPFatalError();
            }
        }
        if (subtree_keys.status == NO_SPACE_FOR_ID) {
            // some keys from the domain, were already removed, so run the same function again to remove the rest of
            // them
            LOG_INF("Purge buffer for subtree element is full, continue deleting keys");
            HAPError e = HAPPlatformKeyValueStorePurgeDomain(keyValueStore, domain);
            k_mutex_unlock(&local_context.settings_load_subtree_mutex);
            return e;
        }

        LOG_INF("records deleted successfully, now delete the file itself");
        memset(local_context.domainKeyID, 0, sizeof(local_context.domainKeyID));
        domainKeyId_query(local_context.domainKeyID, sizeof(local_context.domainKeyID), MODULE_NAME, &domain, NULL);
        err = settings_delete(local_context.domainKeyID);
        if (err) {
            LOG_INF("Failed to delete records in the file");
            HAPFatalError();
        }
    }

    keyValueStore->busy = false;
    k_mutex_unlock(&local_context.settings_load_subtree_mutex);
    return kHAPError_None;
}
