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

#include <zephyr/logging/log.h>

#include "HAPPlatformBLEPeripheralManager.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <sys/types.h>
#include "HAPPlatformBLE+Init.h"
#include "HAPPlatformBLEPeripheralManager+Init.h"
#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformMutex+Init.h"

#include "StatusLED.h"

#include <zephyr/bluetooth/buf.h>
// FIXME: This need to be after bluetooth/buf.h -- Why? we shouldn't have this kind of dependency!
#include "../../../subsys/bluetooth/host/conn_internal.h"

#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
#include "hap_bt_proxy.h"
#endif

LOG_MODULE_REGISTER(pal_peripheral_manager, CONFIG_PAL_PERIPHERAL_MANAGER_LOG_LEVEL);

/* All the attributes that are built using the ADK operate on blePeripheralManagerRef->bytes.
 * If the buffer passed by the attribute does not point to blePeripheralManagerRef->bytes, this would
 * mean that the Read/Write requests made is not on the service built inside the ADK. It is the request
 * on service that was built outside the ADK by the app or the host/controller or the drivers.
 * In which case, This request will not be handled by the ADK but the value requested is simple returned
 * without any checks.
 */
static HAPPlatformBLEPeripheralManager * blePeripheralManagerRef = NULL;
static HAPPlatformMutex ble_manager_mutex;

void HAPPlatformBLEPeripheralManagerCreate(
    HAPPlatformBLEPeripheralManager *blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerOptions *options)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(options);
    HAPPrecondition(options->uuid128);
    HAPPrecondition(options->keyValueStore);
    const HAPPlatformMutexOptions mutex_options = { .mutexType = kHAPPlatformMutexType_ErrorCheck };
    HAPPlatformMutexInit(&ble_manager_mutex, &mutex_options);

    HAPRawBufferZero(blePeripheralManager, sizeof (HAPPlatformBLEPeripheralManager));
    blePeripheralManager->keyValueStore = options->keyValueStore;
    blePeripheralManager->uuidBases = options->uuid128;
    blePeripheralManager->numUUIDBases = options->numUUIDBases;
    blePeripheralManager->numUsedUUIDBases = 0;
    blePeripheralManager->advertisingHandle = NULL; // FIXME: assign null to _Nonnull pointer?!
    blePeripheralManager->conn = NULL;

    // keep a pointer of blePeripheralManager locally for handling events
    blePeripheralManagerRef = blePeripheralManager;
}


void HAPPlatformBLEPeripheralManagerSetDelegate(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerDelegate *_Nullable delegate)
{
    HAPPrecondition(blePeripheralManager);

    if (delegate) {
        blePeripheralManager->delegate = *delegate;
    } else {
        HAPRawBufferZero(&blePeripheralManager->delegate, sizeof blePeripheralManager->delegate);
    }
}


void HAPPlatformBLEPeripheralManagerSetDeviceName(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const char *deviceName)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(deviceName);

    // Set Bluetooth device name.
    size_t numDeviceNameBytes = HAPStringGetNumBytes(deviceName);
    HAPAssert(numDeviceNameBytes <= UINT16_MAX);
#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
    int e = hap_bt_set_name(deviceName);
#else
    int e = bt_set_name(deviceName);   
#endif
    if (e) {
        LOG_ERR("bt set name failed: 0x%02x.", e);
        HAPFatalError();
    }
}

/**
 * Compares the bases of two 128-bit UUIDs.
 *
 * @param      uuid                 UUID to compare.
 * @param      otherUUID            UUID to compare with.
 *
 * @return true                     If the bases of both UUIDs are the same.
 * @return false                    Otherwise.
 */
HAP_RESULT_USE_CHECK
static bool CompareBases(
    const uint8_t *uuid,
    const uint8_t *otherUUID)
{
    HAPPrecondition(uuid);
    HAPPrecondition(otherUUID);

    return
        (uuid[ 0] == otherUUID[ 0]) && (uuid[ 1] == otherUUID[ 1]) &&
        (uuid[ 2] == otherUUID[ 2]) && (uuid[ 3] == otherUUID[ 3]) &&
        (uuid[ 4] == otherUUID[ 4]) && (uuid[ 5] == otherUUID[ 5]) &&
        (uuid[ 6] == otherUUID[ 6]) && (uuid[ 7] == otherUUID[ 7]) &&
        (uuid[ 8] == otherUUID[ 8]) && (uuid[ 9] == otherUUID[ 9]) &&
        (uuid[10] == otherUUID[10]) && (uuid[11] == otherUUID[11]) &&
        (uuid[12] == otherUUID[12]) && (uuid[13] == otherUUID[13]) &&
        (uuid[14] == otherUUID[14]) && (uuid[15] == otherUUID[15]);
}


static void ResetGATTState(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager)
{
    HAPPrecondition(blePeripheralManager);
    HAPRawBufferZero(&blePeripheralManager->gatt, sizeof blePeripheralManager->gatt);
}

static ssize_t send_simple_write(
        struct bt_conn* conn,
        const struct bt_gatt_attr* attr,
        const void* buf,
        uint16_t len,
        uint16_t offset) {
    uint16_t attributeHandle = attr->handle;
    HAPAssert(len <= sizeof blePeripheralManagerRef->bytes);

    LOG_INF("(0x%04x) ATT Write Request.", (unsigned int) attributeHandle);

    if (blePeripheralManagerRef->gatt.operation) {
        LOG_ERR("Received Prepare Write Request while other operation is in progress.");
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

    if (offset != 0) {
        // offset is used for prepare write to combine few writes into one buffer.
        LOG_ERR("simple write should not have offset!");
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

    if (!blePeripheralManagerRef->delegate.handleWriteRequest) {
        LOG_ERR("No write request handler plugged in. Sending error response.");
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

    // Start preparing new write transaction.
    blePeripheralManagerRef->gatt.operation = kHAPPlatformBLEPeripheralManagerGATTOperation_Write;
    blePeripheralManagerRef->gatt.handle = attributeHandle;
    HAPRawBufferCopyBytes(&blePeripheralManagerRef->bytes[offset], buf, len);
    blePeripheralManagerRef->gatt.offset = 0;
    blePeripheralManagerRef->gatt.numBytes = len;

    HAPError err = blePeripheralManagerRef->delegate.handleWriteRequest(
            blePeripheralManagerRef,
            conn->handle,
            blePeripheralManagerRef->gatt.handle,
            blePeripheralManagerRef->bytes,
            blePeripheralManagerRef->gatt.numBytes,
            blePeripheralManagerRef->delegate.context);
    if (err) {
        // stop processing and return with error
        LOG_ERR("delagate.handleWriteRequest returned error %d", err);
        HAPAssert(err == kHAPError_InvalidState || err == kHAPError_InvalidData);

        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }
    if (blePeripheralManagerRef->gatt.operation != kHAPPlatformBLEPeripheralManagerGATTOperation_Write) {
        HAPAssert(
                blePeripheralManagerRef->gatt.operation == kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting);
        // a disconnect request was issues before this, all write request after that would fail. Return with error
        LOG_INF( "Requested disconnect while handling write request.");

        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }
    // Reset the gatt state to process the new Write transaction
    ResetGATTState(blePeripheralManagerRef);

    LOG_INF( "Sending Write Response.");
    uint8_t* value = attr->user_data;

    memcpy(value + offset, buf, len);
    return len;
}


static void MacOS_bug() {
    // TODO RG: Why would we gett operation Read on Write handler? check this bug!
    // Handle macOS Bluetooth bugs.
    if (blePeripheralManagerRef->secondClientMTU &&
        blePeripheralManagerRef->gatt.operation == kHAPPlatformBLEPeripheralManagerGATTOperation_Read &&
        blePeripheralManagerRef->gatt.offset == blePeripheralManagerRef->gatt.numBytes) {
        LOG_WRN("[macOS 10.13.2 Bluetooth Bugs] Expected spurious ATT Read Blob Request not received.");
        ResetGATTState(blePeripheralManagerRef);
    }
}

static ssize_t GattsAuthorizeWrite(
        struct bt_conn* conn,
        const struct bt_gatt_attr* attr,
        const void* buf,
        uint16_t len,
        uint16_t offset,
        uint8_t flags) {

    if (HAPPlatformMutexLock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not lock mutex");
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }
    MacOS_bug();

    ssize_t ret = BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    LOG_DBG("GATT write:\nlen: %d\noffset: %d\nflags: %d", len, offset, flags);
    switch (flags) {
        case 0: {
            LOG_DBG("flag 0");
            ret = send_simple_write(conn, attr, buf, len, offset);
            break;
        }
        case BT_GATT_WRITE_FLAG_CMD: {
            LOG_DBG("command");
            ret = send_simple_write(conn, attr, buf, len, offset);
            break;
        }
        case BT_GATT_WRITE_FLAG_EXECUTE: {
            LOG_DBG("execute");
            ret = send_simple_write(conn, attr, buf, len, offset);
            break;
        }
        case BT_GATT_WRITE_FLAG_PREPARE: {
            LOG_DBG("Prepare GATT write - do nothing");
            ret = 0; // do nothing
            break;
        }

        default: {
            LOG_ERR("Invalid, or multiple flags = %d", flags);
            ret = BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
        }
    }
    if (HAPPlatformMutexUnlock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not release mutex");
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

    if (ret < 0) {
        LOG_ERR("Error occurred - requesting disconnect.");
        HAPPlatformBLEPeripheralManagerCancelCentralConnection(blePeripheralManagerRef, conn->handle);
    }

    return ret;
}

static ssize_t GattsAuthorizeRead(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{
    if (HAPPlatformMutexLock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not lock mutex");
        return 0;
    }
    LOG_DBG("GattsAuthorizeRead conn %p, attr %p, offset %d, len %d", conn, attr, offset, len);

    HAPPrecondition(blePeripheralManagerRef);
    HAPPrecondition(conn);
    HAPPrecondition(blePeripheralManagerRef->conn == conn);

    HAPError err;
    size_t ret =  BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);

    MacOS_bug();

    // Process request.
    bool ok = true;
    if (!offset) {
        // Read Request.
        // See Bluetooth Core Specification Version 5
        // Vol 3 Part F Section 3.4.4.3 Read Request
        LOG_INF( "(0x%04x) ATT Read Request.", (unsigned int) attr->handle);

        if (blePeripheralManagerRef->gatt.operation) {
            LOG_INF("Received Read Request while other operation is in progress.Current GATT operation = %d",
                   blePeripheralManagerRef->gatt.operation);
            ok = false;
            ret =  BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
        } else {
            // Start new read transaction.
            if (blePeripheralManagerRef->delegate.handleReadRequest) {
                // handleReadRequest is valid, let the core platform know about this request
                size_t len;
                LOG_INF("conn = %p   ,  attr->handle = 0x%x.", conn, attr->handle);
                err = blePeripheralManagerRef->delegate.handleReadRequest(blePeripheralManagerRef,
                    conn->handle,  /* expects handle and not pointer to connection */
                    attr->handle,
                    blePeripheralManagerRef->bytes,
                    sizeof blePeripheralManagerRef->bytes,
                    &len,
                    blePeripheralManagerRef->delegate.context);
                if (err) {
                    // Error for core platform, stop processing and return with error
                    HAPAssert( err == kHAPError_InvalidState || err == kHAPError_OutOfResources);
                    ok = false;
                    ret =  BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
                } else if (blePeripheralManagerRef->gatt.operation) {
                    // Cannot have another request after a disconnect is in processing
                    HAPAssert(blePeripheralManagerRef->gatt.operation == kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting);
                    LOG_ERR("Requested disconnect while handling read request.");
                    ok = false;
                    ret =  BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
                } else {
                    // adjust the read pointers in the buffer space reserved
                    blePeripheralManagerRef->gatt.operation = kHAPPlatformBLEPeripheralManagerGATTOperation_Read;
                    blePeripheralManagerRef->gatt.handle = attr->handle;
                    blePeripheralManagerRef->gatt.offset = 0;
                    HAPAssert(len <= sizeof blePeripheralManagerRef->bytes);
                    blePeripheralManagerRef->gatt.numBytes = (uint16_t) len;
                }
            } else {
                LOG_ERR("No read request handler plugged in. Sending error response.");
                ok = false;
                ret =  BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
            }
        }
    } else {
        // Read Blob Request.
        // See Bluetooth Core Specification Version 5
        // Vol 3 Part F Section 3.4.4.5 Read Blob Request
        LOG_INF( "(0x%04x) ATT Read Blob Request.", (unsigned int) attr->handle);
        if (blePeripheralManagerRef->gatt.operation != kHAPPlatformBLEPeripheralManagerGATTOperation_Read) {
            LOG_ERR("Received Read Blob Request without prior Read Request.");
            ok = false;
            ret =  BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);

        } else if (blePeripheralManagerRef->gatt.handle != attr->handle) {
            LOG_ERR("Received Read Blob Request for a different characteristic than prior Read Request.");
            ok = false;
            ret =  BT_GATT_ERR(BT_ATT_ERR_INVALID_HANDLE);
        } else if (blePeripheralManagerRef->gatt.offset != offset) {
            LOG_ERR("Received Read Blob Request with non-sequential offset (expected: %u, actual: %u).",
                    blePeripheralManagerRef->gatt.offset,
                    offset);
            ok = false;
            ret =  BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
        }
    }

    // Send response.
    if (!ok) {
        // An error was returned in the handling above, not sending any response
        if (blePeripheralManagerRef->gatt.operation ==
            kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting)
        {
            LOG_INF("Not sending any response as disconnect has been requested.");
        } else {
            // Certain HAT / iOS versions do not cope well with GATT errors and get stuck.
            LOG_ERR("Error occurred - requesting disconnect.");
            HAPPlatformBLEPeripheralManagerCancelCentralConnection(blePeripheralManagerRef,
                conn->handle);
        }
    } else {
        // No errors upto this, send response to the peer with the data
        const uint8_t *value = attr->user_data;
        HAPAssert(blePeripheralManagerRef->bytes == value);

        // Update state.
        HAPAssert(blePeripheralManagerRef->mtu >= 1);
        uint16_t numSent = (uint16_t) (blePeripheralManagerRef->mtu - 1);
        if (blePeripheralManagerRef->gatt.numBytes - blePeripheralManagerRef->gatt.offset < numSent) {
            numSent = blePeripheralManagerRef->gatt.numBytes - blePeripheralManagerRef->gatt.offset;
        }

        ret = bt_gatt_attr_read(conn, attr, buf,
                    /* len */       numSent,
                    /* offset */    blePeripheralManagerRef->gatt.offset,
                    /* value */     value,
                    /* value_len */ sizeof blePeripheralManagerRef->bytes);


        if (ret >= 0) {
            blePeripheralManagerRef->gatt.offset += numSent;
            if (blePeripheralManagerRef->gatt.offset == blePeripheralManagerRef->gatt.numBytes) {
                if (numSent == blePeripheralManagerRef->mtu - 1) {
                    LOG_INF( "Expecting additional ATT Read Blob Request to resolve length ambiguity.");
                } else if (blePeripheralManagerRef->secondClientMTU && numSent >= blePeripheralManagerRef->secondClientMTU - 1) {
                    // Handle macOS Bluetooth bugs.
                    LOG_INF("[macOS 10.13.2 Bluetooth Bugs] Expecting spurious ATT Read Blob Request.");
                } else {
                    ResetGATTState(blePeripheralManagerRef);
                }
            }
        }
        else {
            LOG_ERR("bt_gatt_attr_read(0x0x%04x) failed: 0x%02x.", (unsigned int) conn, (unsigned int) ret);
            HAPFatalError();
        }
    }

    if (HAPPlatformMutexUnlock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not release mutex");
        return 0;
    }

    return ret;
}


/**
 * Gets a Nordic ble_uuid_t from a UUID. UUID types are automatically registered.
 *
 * @param      blePeripheralManager BLE peripheral manager.
 * @param      uuid                 128-bit UUID to convert.
 *
 * @return kHAPError_None           If successful.
 * @return kHAPError_OutOfResources If no more bases may be registered.
 */
HAP_RESULT_USE_CHECK
HAPError GetBLEUUID(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerUUID *uuid,
    size_t *baseIndex)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(uuid);


    // add the below UUID into known UUID bases
    static bool first_run = true;

    if (first_run) {
        HAPPrecondition(blePeripheralManager->numUsedUUIDBases == 0);

        // Check for Bluetooth Base UUID (0000xxxx-0000-1000-8000-00805F9B34FB).
        // See Bluetooth Core Specification Version 5
        // Vol 3 Part B Section 2.5.1 UUID
        uint8_t bluetoothBaseUUID[] = {
            0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        struct bt_uuid_128 *base = &blePeripheralManager->uuidBases[blePeripheralManager->numUsedUUIDBases];
        base->uuid.type =  BT_UUID_TYPE_128;
        HAPRawBufferCopyBytes(base->val, bluetoothBaseUUID, sizeof(bluetoothBaseUUID));

        blePeripheralManager->numUsedUUIDBases++;

        first_run = false;
    }

    // Find matching UUID base.
    for (size_t i = 0; i < blePeripheralManager->numUsedUUIDBases; i++) {
        const struct bt_uuid_128 *base = &blePeripheralManager->uuidBases[i];

        if (CompareBases(uuid->bytes, base->val)) {
            // This UUID has already been used (by already added service or characteristic or descriptor)
            *baseIndex = i;
            return kHAPError_None;
        }
    }

    // UUID base not found. Check for space to allocate a new one.
    if (blePeripheralManager->numUsedUUIDBases >= blePeripheralManager->numUUIDBases) {
        LOG_INF("Not enough space to allocate UUID base. Please increase the number of UUID bases.");
        return kHAPError_OutOfResources;
    }

    struct bt_uuid_128 *base = &blePeripheralManager->uuidBases[blePeripheralManager->numUsedUUIDBases];
    base->uuid.type =  BT_UUID_TYPE_128;
    HAPRawBufferCopyBytes(base->val, uuid->bytes, sizeof(base->val));

    *baseIndex = (size_t)blePeripheralManager->numUsedUUIDBases;

    // Registration succeeded.
    blePeripheralManager->numUsedUUIDBases++;

    return kHAPError_None;
}

/**
 * The constant below defines the maximum attribute depth of services that can be added (including service itself)
 */
#define kMaxServicesCount       CONFIG_HAP_BT_SERVICES_MAX_COUNT
#define kServiceAttrMaxDepth    CONFIG_HAP_BT_SERVICE_ATTR_MAX_DEPTH

static bool isServicePopulating = false;
static bool servicesPublished = false;

// Services built without using MACRO helpers as they were not designed for dynamically populating service
static struct bt_gatt_attr attrs[kServiceAttrMaxDepth]           = {{0}};
static struct bt_gatt_chrc characteristics[kServiceAttrMaxDepth] = {{0}};
static struct _bt_gatt_ccc ccc_gatt[kMaxServicesCount]           = {0};
static struct bt_gatt_service HAPServices[kMaxServicesCount]     = {{0}};

static uint16_t ServiceIndex = 0;
static uint16_t serviceStartAttributeIndex = 0;
static uint16_t attr_count = 0;
static uint16_t chars_count = 0;
static uint16_t ccc_count = 0;

/* The procedures are started from the lib giving the attribute handle, the below diff
 * help us calculate the actual pointer of the attribute with that handle.
 */
static uint16_t attrIndexHandlDiff = 0;

/* The valueHandle and cccDescriptorHandle that needs to be returned in AddXX function
 * are not ready before actually publishing the service. In this module we build a service
 * plus its attributes and publish all of them at once. So after publishService is called,
 * we need to update the *valueHandle and *cccDescriptorHandle with actual attribute handle given
 * by the BLE LinkLayer.
 * This is OK, since the pointer given here is from static array member which remains valid throughout device up time.
 */
static uint16_t update_handle_count = 0;
struct update_handles
{
  HAPPlatformBLEPeripheralManagerAttributeHandle *valueHandle;
  uint16_t valueAttrIndex;
} update_attr_val_handles[kServiceAttrMaxDepth];

HAP_RESULT_USE_CHECK
HAPError PublishService()
{
    // verify that this is a new service, so we can publish the last populated service
    HAPPrecondition(attr_count - serviceStartAttributeIndex);
    if (ServiceIndex >= (kMaxServicesCount - 1)) {
        LOG_ERR("Maximum services reached as defined in kMaxServicesCount");
        HAPFatalError();
        return kHAPError_OutOfResources;
    }

    // Since this request is for new service, publish the populated service
    struct bt_gatt_service *service = &HAPServices[ServiceIndex++];

    HAPRawBufferZero(service, sizeof(struct bt_gatt_service));
    service->attrs = &attrs[serviceStartAttributeIndex];
    service->attr_count = (attr_count - serviceStartAttributeIndex);

    int e = bt_gatt_service_register(service);
    if (e) {
        LOG_ERR("failed to register service, bt_gatt_service_register failed: 0x%02x.", (int) e);
        HAPFatalError();
        return kHAPError_OutOfResources;
    }

    // update serviceStartAttributeIndex for populating new service
    serviceStartAttributeIndex  = attr_count;
    isServicePopulating = false;

    /* Publish successful, now the CorePlatform should be updated with correct attr[X]->handle value */
    for (int i = 0; i < update_handle_count; i++) {
        if (update_attr_val_handles[i].valueHandle) {
            *(update_attr_val_handles[i].valueHandle) = attrs[update_attr_val_handles[i].valueAttrIndex].handle;
        }
    }

    // We assume here that all services are built through the lib using HAP, which are done all at once
    attrIndexHandlDiff = attrs[0].handle;

    /* Updated the CorePlatform with proper handle values */
    HAPRawBufferZero(update_attr_val_handles, sizeof update_attr_val_handles);
    update_handle_count = 0;

    return kHAPError_None;
}

static void GattsCCCAttributeChangeCallback(const struct bt_gatt_attr *attr,
				 uint16_t value)
{
    // no processing needed for now
}

/**
 * If we were in a process of locally populating a service and its underlying characteristics,
 * then we publish them before starting to populate a new service.
 */
HAP_RESULT_USE_CHECK
HAPError HAPPlatformBLEPeripheralManagerAddService(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerUUID *type,
    bool isPrimary)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(type);
    HAPPrecondition(!servicesPublished);
    HAPError err;
    size_t baseIndex = 0;

    if (isServicePopulating) {
        // new service add request, publish the last populated service
        err = PublishService();

        if (err) {
            LOG_ERR("HAPPlatformBLEPeripheralManagerPublishServices failed: 0x%02x.", (unsigned int) err);
            HAPFatalError();
            return err;
        }
    }

    if (attr_count >= (kServiceAttrMaxDepth - 1)) {
        LOG_ERR("Maximum attributes count reached: HAPPlatformBLEPeripheralManagerAddService");
        HAPFatalError();
        return kHAPError_OutOfResources;
    }

    // type will be copied into new blePeripheralManager uuidBases
    err = GetBLEUUID(blePeripheralManager, type, &baseIndex);

   if (err) {
        HAPAssert(err == kHAPError_OutOfResources);
        return err;
    }

    // creating a static space for holding these primary and secondary UUID markers
    static const struct bt_uuid_16 gattPrimaryUUID = BT_UUID_INIT_16(0x2800);
    static const struct bt_uuid_16 gattSecondaryUUID = BT_UUID_INIT_16(0x2801);

    // Populate the attribute locally
    attrs[attr_count].perm      = BT_GATT_PERM_READ;
    attrs[attr_count].read      = bt_gatt_attr_read_service;
    attrs[attr_count].write     = NULL;
    attrs[attr_count].uuid      = (struct bt_uuid *) ((isPrimary) ?  &gattPrimaryUUID: &gattSecondaryUUID);
    attrs[attr_count].user_data = (void *)&blePeripheralManager->uuidBases[baseIndex];

    attr_count++;

    isServicePopulating = true;

    return kHAPError_None;
}


HAP_RESULT_USE_CHECK
HAPError HAPPlatformBLEPeripheralManagerAddCharacteristic(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerUUID *type,
    HAPPlatformBLEPeripheralManagerCharacteristicProperties properties,
    const void* _Nullable constBytes,
    size_t constNumBytes,
    HAPPlatformBLEPeripheralManagerAttributeHandle *valueHandle,
    HAPPlatformBLEPeripheralManagerAttributeHandle *_Nullable cccDescriptorHandle)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(type);
    HAPPrecondition(valueHandle);
    HAPPrecondition(isServicePopulating);
    HAPPrecondition(attr_count);

    size_t baseIndex = 0;

    if (properties.notify || properties.indicate) {
        HAPPrecondition(cccDescriptorHandle);
    } else {
        HAPPrecondition(!cccDescriptorHandle);
    }
    HAPPrecondition(!servicesPublished);

    HAPError err;

    if (attr_count >= (kServiceAttrMaxDepth - 1)) {
        LOG_ERR("Maximum attributes count reached. Cannot add the characteristic to this service");
        HAPFatalError();
        return kHAPError_OutOfResources;
    }

    err = GetBLEUUID(blePeripheralManager, type, &baseIndex);
    if (err) {
        HAPAssert(err == kHAPError_OutOfResources);
        return err;
    }

    // populate characteristic.
    uint8_t charProps           = (
                                  (properties.read ? BT_GATT_CHRC_READ : 0) |
                                  (properties.writeWithoutResponse ? BT_GATT_CHRC_WRITE_WITHOUT_RESP : 0) |
                                  (properties.write ? BT_GATT_CHRC_WRITE : 0) |
                                  (properties.notify ? BT_GATT_CHRC_NOTIFY : 0) |
                                  (properties.indicate ? BT_GATT_CHRC_INDICATE : 0)
                                  );

    // Characteristic have two attributes one of which needs memory for struct bt_gatt_chrc
    static struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0x2803);

    attrs[attr_count].perm                    = BT_GATT_PERM_READ;
    attrs[attr_count].read                    = bt_gatt_attr_read_chrc;
    attrs[attr_count].write                   = NULL;
    attrs[attr_count].uuid                    = (struct bt_uuid *)&char_uuid;
    characteristics[chars_count].properties   = charProps;
    characteristics[chars_count].uuid         = (struct bt_uuid *)&blePeripheralManager->uuidBases[baseIndex];
    characteristics[chars_count].value_handle = 0U;
    attrs[attr_count].user_data               = &characteristics[chars_count];

    attr_count++;
    chars_count++;

    attrs[attr_count].perm      = ((properties.read ? BT_GATT_PERM_READ : 0) | ((properties.writeWithoutResponse || properties.write) ? BT_GATT_PERM_WRITE : 0) | BT_GATT_PERM_PREPARE_WRITE);
    attrs[attr_count].read      = GattsAuthorizeRead;
    attrs[attr_count].write     = GattsAuthorizeWrite;

    attrs[attr_count].uuid      = (struct bt_uuid *)&blePeripheralManager->uuidBases[baseIndex];
    attrs[attr_count].user_data = (void *)blePeripheralManager->bytes;

    update_attr_val_handles[update_handle_count].valueHandle    = valueHandle;
    update_attr_val_handles[update_handle_count].valueAttrIndex = attr_count;

    update_handle_count++;
    attr_count++;

    // Add CCCD if notifcation or indication is enabled
    if (properties.notify || properties.indicate) {
        // if notification or indication is enabled then we need to construct CCC attribute
        if (attr_count >= (kServiceAttrMaxDepth)) {
            LOG_ERR("Maximum attributes count reached. Cannot add the characteristic to this service");
            HAPFatalError();
            return kHAPError_OutOfResources;
        }

        if (ccc_count == (kServiceAttrMaxDepth/2)) {
            LOG_ERR("Maximum CCCD depth reached: HAPPlatformBLEPeripheralManagerAddCharacteristic");
            HAPFatalError();
            return kHAPError_OutOfResources;
        }

        static const struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(0x2902);

        attrs[attr_count].uuid      = (struct bt_uuid *)&ccc_uuid;

        attrs[attr_count].perm      = BT_GATT_PERM_READ | BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE;
        attrs[attr_count].read      = bt_gatt_attr_read_ccc;
        attrs[attr_count].write     = GattsAuthorizeWrite;

        HAPRawBufferZero(ccc_gatt[ccc_count].cfg, (sizeof(struct bt_gatt_ccc_cfg) * BT_GATT_CCC_MAX));
        ccc_gatt[ccc_count].cfg_changed  = GattsCCCAttributeChangeCallback;
        ccc_gatt[ccc_count].cfg_write    = NULL;
        ccc_gatt[ccc_count].cfg_match    = NULL;

        attrs[attr_count].user_data = &ccc_gatt[ccc_count];

        update_attr_val_handles[update_handle_count].valueHandle    = cccDescriptorHandle;
        update_attr_val_handles[update_handle_count].valueAttrIndex = attr_count;

        update_handle_count++;
        attr_count++;           // CCC have one attribute
        chars_count++;          // used one characteristic struct as user_data
        ccc_count++;
    }

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformBLEPeripheralManagerAddDescriptor(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    const HAPPlatformBLEPeripheralManagerUUID *type,
    HAPPlatformBLEPeripheralManagerDescriptorProperties properties,
    const void* _Nullable constBytes,
    size_t constNumBytes,
    HAPPlatformBLEPeripheralManagerAttributeHandle *descriptorHandle)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(type);
    HAPPrecondition(descriptorHandle);
    HAPPrecondition(!servicesPublished);
    HAPPrecondition(isServicePopulating);
    HAPPrecondition(attr_count);

    size_t baseIndex = 0;
    HAPError err;

    if (attr_count >= kServiceAttrMaxDepth) {
        LOG_ERR("Maximum attributes count reached: HAPPlatformBLEPeripheralManagerAddDescriptor");
        return kHAPError_OutOfResources;
    }


    // Convert type to native UUID.
    err = GetBLEUUID(blePeripheralManager, type, &baseIndex);
    if (err) {
        HAPAssert(err == kHAPError_OutOfResources);
        return err;
    }

    // Populate descriptor.
    attrs[attr_count].perm      = BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_AUTHEN | BT_GATT_PERM_PREPARE_WRITE;
    attrs[attr_count].read      = GattsAuthorizeRead;
    attrs[attr_count].write     = GattsAuthorizeWrite;
    attrs[attr_count].uuid      = (struct bt_uuid *)&blePeripheralManager->uuidBases[baseIndex];
    attrs[attr_count].user_data = (void *)blePeripheralManager->bytes;

    update_attr_val_handles[update_handle_count].valueHandle    = descriptorHandle;
    update_attr_val_handles[update_handle_count].valueAttrIndex = attr_count;

    update_handle_count++;
    attr_count++;

    return kHAPError_None;
}

void HAPPlatformBLEPeripheralManagerPublishServices(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(!servicesPublished);

    HAPError err;

    if (isServicePopulating) {
        // publish the last populated service
        err = PublishService();

        if (err) {
            LOG_ERR("HAPPlatformBLEPeripheralManagerPublishServices failed: 0x%02x.", (unsigned int) err);
            HAPFatalError();
            return;
        }
    }

    servicesPublished = true;
}

void HAPPlatformBLEPeripheralManagerRemoveAllServices(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager)
{
    HAPPrecondition(blePeripheralManager);

    int ret = 0;

    if (servicesPublished) {
        for (int i = 0; i < ServiceIndex; i++) {
            ret = bt_gatt_service_unregister(&HAPServices[i]);
            HAPAssert(ret == 0);
        }

        ServiceIndex = 0;
        serviceStartAttributeIndex = 0;
        attr_count = 0;
        chars_count = 0;
        ccc_count = 0;
        servicesPublished = false;
    }
}

// Pure ADV
typedef struct
{
  uint8_t     *p_data;  /**< Pointer to the data buffer provided to/from the application. */
  uint16_t     len;     /**< Length of the data buffer, in bytes. */
} ble_data_t;


// Set the max adv data length to 31
#define ADV_SET_DATA_SIZE_MAX  31
#define MAX_ADV_TYPE_ELEMENTS  10
static uint8_t advData[ADV_SET_DATA_SIZE_MAX];
static uint8_t scanRspData[ADV_SET_DATA_SIZE_MAX];
struct bt_data ad[MAX_ADV_TYPE_ELEMENTS];
struct bt_data sd[MAX_ADV_TYPE_ELEMENTS];

static int HAPDecodeAdvData(uint8_t * p_adv_data, uint16_t size, struct bt_data *p_decoded_data, uint8_t *elements)
{
    HAPPrecondition(p_adv_data);
    uint16_t pos = 0;
    uint8_t  num_elements    = 0;

    if (size > ADV_SET_DATA_SIZE_MAX)
    {
        return -EINVAL;
    }

    while (pos < size) {
        /* if the length is 0, then break out of the loop */
        if (p_adv_data[pos] == 0)
        {
          while (pos < size) {
            if (p_adv_data[pos] != 0x00)
            {
              return -EINVAL;
            }
            pos++;
          }
          break;
        }

        p_decoded_data[num_elements].type           =  p_adv_data[pos+1];

        // The encoded data length must only be the length of data (not including type)
        p_decoded_data[num_elements].data_len       =  (p_adv_data[pos] - 1);
        p_decoded_data[num_elements].data           =  &p_adv_data[pos+2];

        pos += p_adv_data[pos]+1;
        if (pos > size)
        {
          return -EINVAL;
        }

        num_elements++;
    }

    *elements = num_elements;
    return 0;
}


void HAPPlatformBLEPeripheralManagerStartAdvertising(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    HAPBLEAdvertisingInterval advertisingInterval,
    const void *advertisingBytes,
    size_t numAdvertisingBytes,
    const void *_Nullable scanResponseBytes,
    size_t numScanResponseBytes)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(advertisingInterval);
    HAPPrecondition(advertisingBytes);
    HAPPrecondition(numAdvertisingBytes <= UINT16_MAX);
    HAPPrecondition(!numScanResponseBytes || scanResponseBytes);
    HAPPrecondition(numScanResponseBytes <= UINT16_MAX);


    uint8_t ad_elements = 0;
    uint8_t sd_elements = 0;

    // stop advertisiing
#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
    hap_bt_adv_stop();
#else
    bt_le_adv_stop();
#endif

    HAPRawBufferZero(ad, ADV_SET_DATA_SIZE_MAX);
    HAPRawBufferZero(advData, sizeof advData);
    HAPAssert(numAdvertisingBytes <= sizeof advData);
    HAPRawBufferCopyBytes(advData, advertisingBytes, numAdvertisingBytes);

    int e = HAPDecodeAdvData(advData, numAdvertisingBytes, ad, &ad_elements);
    if (e) {
        LOG_ERR("Invalid Advertising data in HAPPlatformBLEPeripheralManagerStartAdvertising");
        HAPFatalError();
    }

    HAPRawBufferZero(sd, ADV_SET_DATA_SIZE_MAX);
    HAPRawBufferZero(scanRspData, sizeof scanRspData);
    if (scanResponseBytes) {
        HAPAssert(numScanResponseBytes <= sizeof scanRspData);
        HAPRawBufferCopyBytes(scanRspData, scanResponseBytes, numScanResponseBytes);
        e = HAPDecodeAdvData(scanRspData, numScanResponseBytes, sd, &sd_elements);
        if (e) {
            LOG_ERR("Invalid scan response data in HAPPlatformBLEPeripheralManagerStartAdvertising");
            HAPFatalError();
        }
    }

    const struct bt_le_adv_param adv_param =
            BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE, advertisingInterval, advertisingInterval, NULL);

    // first check if we can update advertising data, if not start advertising
#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
    e = hap_bt_adv_start(
            &adv_param, (const struct bt_data*) &ad, ad_elements, (const struct bt_data*) &sd, sd_elements);
#else
    e = bt_le_adv_start(&adv_param, (const struct bt_data*) &ad, ad_elements, (const struct bt_data*) &sd, sd_elements);
#endif

    if (e) {
        LOG_ERR("bt adv start failed: 0x%02x.", (unsigned int) e);
        // HAPFatalError();
    }
    else {
        LOG_INF("updated Advertising data");
        bleLedOnBLEAdvertising();
    }
}


void HAPPlatformBLEPeripheralManagerStopAdvertising(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager)
{
    HAPPrecondition(blePeripheralManager);

    // Stop advertising.
#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
    hap_bt_adv_stop();
#else
    bt_le_adv_stop();
#endif
    bleLedOnBLEDisabled();
}


void HAPPlatformBLEPeripheralManagerCancelCentralConnection(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    HAPPlatformBLEPeripheralManagerConnectionHandle connHandle)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(blePeripheralManager->conn);
    HAPPrecondition(blePeripheralManager->conn->handle == connHandle);

    struct bt_conn *conn = blePeripheralManager->conn;

    if (blePeripheralManager->gatt.operation == kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting) {
        return;
    }

    // Disconnect from central.
    ResetGATTState(blePeripheralManager);
    blePeripheralManager->gatt.operation = kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting;

    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void GattsIndicateValueCallback(struct bt_conn *conn, struct bt_gatt_indicate_params *params,
                        uint8_t err)
{
    ARG_UNUSED(params);

    HAPPrecondition(blePeripheralManagerRef);
    HAPAssert(blePeripheralManagerRef->conn == conn);
    LOG_INF("Indication %s", err != 0U ? "fail" : "success");

    blePeripheralManagerRef->indicationInProgress = false;
    if (blePeripheralManagerRef->delegate.handleReadyToUpdateSubscribers) {
        blePeripheralManagerRef->delegate.handleReadyToUpdateSubscribers(blePeripheralManagerRef,
            conn->handle,
            blePeripheralManagerRef->delegate.context);
    } else {
        LOG_INF("No ready to update subscribers handler plugged in. Ignoring.");
    }
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformBLEPeripheralManagerSendHandleValueIndication(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    HAPPlatformBLEPeripheralManagerConnectionHandle connectionHandle,
    HAPPlatformBLEPeripheralManagerAttributeHandle valueHandle,
    const void *_Nullable bytes,
    size_t numBytes)
{
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(blePeripheralManager->conn);
    HAPPrecondition(blePeripheralManager->conn->handle == connectionHandle);
    HAPPrecondition(!numBytes || bytes);

    static struct bt_gatt_indicate_params ind_params;

    struct bt_conn *conn = blePeripheralManager->conn;
    if (blePeripheralManager->indicationInProgress) {
        LOG_INF("An indication is already in progress.");
        return kHAPError_InvalidState;
    }
    if (blePeripheralManager->gatt.operation == kHAPPlatformBLEPeripheralManagerGATTOperation_Disconnecting) {
        LOG_INF("Not sending indication as the connection is being terminated.");
        return kHAPError_InvalidState;
    }

    // Handle Value Indication.
    // See Bluetooth Core Specification Version 5
    // Vol 3 Part F Section 3.4.7.2 Handle Value Indication
    uint16_t n;
    HAPAssert(blePeripheralManager->mtu >= 3);
    if (numBytes > UINT16_MAX || numBytes > blePeripheralManager->mtu - 3) {
        LOG_INF("Event value is too large.");
        return kHAPError_OutOfResources;
    }
    n = (uint16_t) numBytes;
    if (n) {
        // Sending an indication with a value overrides the GATT buffer.
        // Indications can occur concurrently with a central-initiated GATT transaction.
        LOG_INF("Indications with a non-empty value are not supported at this time.");
        return kHAPError_OutOfResources;
    }

    // Calculate the attrIndex value from the given valueHandle
    uint16_t attrIndex = (valueHandle - attrIndexHandlDiff);
    static uint32_t no_data = 0;

    ind_params.attr = &attrs[attrIndex];
    ind_params.func = GattsIndicateValueCallback;
    ind_params.data = bytes ? bytes : &no_data;
    ind_params.len  = bytes ? n : 0;

    int e = bt_gatt_indicate(conn, &ind_params);
    if (e) {
        LOG_ERR("bt_gatt_indicate(0x%04x) failed: 0x%02x.", (unsigned int) conn, (unsigned int) e);
        HAPFatalError();
    }
    blePeripheralManager->indicationInProgress = true;

    return kHAPError_None;
}


#define BLE_L2CAP_MTU_MIN		23
static struct bt_gatt_exchange_params exchange_params;

static void GattsExchangeMTUParams(struct bt_conn *conn, uint8_t err,
                          struct bt_gatt_exchange_params *params)
{

    if (HAPPlatformMutexLock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not lock mutex");
        return;
    }

    uint16_t mtu = bt_gatt_get_mtu(conn);
    HAPAssert(blePeripheralManagerRef != NULL);
    LOG_INF( "MTU exchange %s", err == 0 ? "successful" : "failed");

    if (!err) {
        if (sizeof blePeripheralManagerRef->bytes < mtu) {
            mtu = sizeof blePeripheralManagerRef->bytes;
        }

        if (blePeripheralManagerRef->mtu != BLE_L2CAP_MTU_MIN &&
            blePeripheralManagerRef->mtu != mtu)
        {
            // macOS 10.13.2 requests additional MTU update with different value when using Bluetooth headphones...
            LOG_INF("[macOS 10.13.2 Bluetooth Bugs] Staying on previous MTU %u instead of new MTU %u.",
                    blePeripheralManagerRef->mtu,
                    mtu);
            blePeripheralManagerRef->secondClientMTU = mtu;
        } else {
            blePeripheralManagerRef->mtu = mtu;
            LOG_INF("New MTU: %u.", blePeripheralManagerRef->mtu);
        }
        blePeripheralManagerRef->clientMTU = 0;
    }

    if (HAPPlatformMutexUnlock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not release mutex");
        return;
    }
}

void HAPPlatformBLEPeripheralManagerHandleBLEEvent(
    HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
    struct bt_conn *conn,
    const bt_platform_events event)
{
    // This function is called from the "BT RX" task context to handle various events.
    // Therefore it must acquire a mutex.
    if (HAPPlatformMutexLock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("cannot lock mutex");
        return;
    }

    HAPPrecondition(blePeripheralManager);

    /* Log event: */ {
        #define AssignEventName(id) case id: eventName = #id; break
        const char *eventName;
        switch (event) {
            AssignEventName(BLE_PLATFORM_CONNECTED);
            AssignEventName(BLE_PLATFORM_DISCONNECTED);
            AssignEventName(BLE_PLATFORM_CONN_PARAM_UPDATE_REQUEST);
            AssignEventName(BLE_PLATFORM_CONN_PARAM_UPDATE);
            default: eventName = "Unknown"; break;
        }
        #undef AssignEventName
        LOG_INF( "BLE Event: %s (0x%04X).", eventName, event);
    }

    static HAPPlatformBLEPeripheralManagerConnectionHandle connection_hk_handle = 0;
    switch (event) {
        case BLE_PLATFORM_CONNECTED: {
            HAPAssert(blePeripheralManager->conn == NULL);

            blePeripheralManager->conn = bt_conn_ref(conn);
            HAPAssert(blePeripheralManager->conn != NULL);

            blePeripheralManager->mtu = BLE_L2CAP_MTU_MIN;
            blePeripheralManager->secondClientMTU = 0;
            blePeripheralManager->clientMTU = 0;

            LOG_INF("Connection interval = %dms, min_interval =%dms, max interval = %dms",
                    conn->le.interval,
                    conn->le.interval_min,
                    conn->le.interval_max);

            if (blePeripheralManager->delegate.handleConnectedCentral) {
                LOG_INF("conn = %p  ", conn);
                blePeripheralManager->delegate.handleConnectedCentral(blePeripheralManager,
                    conn->handle,  /* needed handle here, which is used for validity at RW Auth events */
                    blePeripheralManager->delegate.context);
            } else {
                LOG_ERR("No connected handler plugged in.");
                HAPFatalError();
            }

            exchange_params.func = GattsExchangeMTUParams;
            int e = bt_gatt_exchange_mtu(conn, &exchange_params);
            if (e) {
                LOG_ERR("MTU exchange failed (err %d)", e);
            } else {
                LOG_INF("MTU exchange pending");
            }
            if (bt_hci_get_conn_handle(conn, &connection_hk_handle) != 0) {
                LOG_INF("Can not read connection handle");
            }

        } break;
        case BLE_PLATFORM_DISCONNECTED: {
            HAPAssert(blePeripheralManager->conn == conn);
            LOG_INF("Disconnected conn = %p  ", conn);
            bt_conn_unref(blePeripheralManager->conn);
            blePeripheralManager->conn = NULL;
            blePeripheralManager->mtu = 0;
            blePeripheralManager->secondClientMTU = 0;
            blePeripheralManager->clientMTU = 0;
            HAPRawBufferZero(blePeripheralManager->bytes, sizeof blePeripheralManager->bytes);
            ResetGATTState(blePeripheralManager);
            blePeripheralManager->indicationInProgress = false;

            if (blePeripheralManager->delegate.handleDisconnectedCentral) {
                blePeripheralManager->delegate.handleDisconnectedCentral(
                        blePeripheralManager, connection_hk_handle, blePeripheralManager->delegate.context);
            } else {
                LOG_INF("No disconnected handler plugged in. Ignoring.");
            }
        } break;

        case BLE_PLATFORM_CONN_PARAM_UPDATE:
        case BLE_PLATFORM_CONN_PARAM_UPDATE_REQUEST: {
        } break;

        default: {
            LOG_INF("Did not handle BLE event!");
        } break;
    }

    if (HAPPlatformMutexUnlock(&ble_manager_mutex) != kHAPError_None) {
        LOG_ERR("Can not release mutex");
        return;
    }
}

bool HAPPlatformBLEPeripheralManagerAllowsServiceRefresh(
        HAPPlatformBLEPeripheralManagerRef _Nonnull blePeripheralManager) {
    HAPPrecondition(blePeripheralManager);
    LOG_INF("HAPPlatformBLEPeripheralManagerAllowsServiceRefresh not implemented");

    return true;
}

void HAPPlatformBLEPeripheralManagerSetDeviceAddress(
        HAPPlatformBLEPeripheralManagerRef blePeripheralManager,
        const HAPPlatformBLEPeripheralManagerDeviceAddress* deviceAddress) {
    HAPPrecondition(blePeripheralManager);
    HAPPrecondition(deviceAddress);

    HAPAssert(sizeof deviceAddress->bytes == 6);
    LOG_INF("Setting Bluetooth address to %02X:%02X:%02X:%02X:%02X:%02X",
            deviceAddress->bytes[0],
            deviceAddress->bytes[1],
            deviceAddress->bytes[2],
            deviceAddress->bytes[3],
            deviceAddress->bytes[4],
            deviceAddress->bytes[5]);

    if(!HAPPlatformBLEStackIsEnabled())
    {
        bt_addr_le_t addr;
        HAPAssert(sizeof deviceAddress->bytes == sizeof addr.a.val);
        addr.type = BT_ADDR_LE_RANDOM;
        memcpy(addr.a.val, deviceAddress->bytes, sizeof deviceAddress->bytes);

#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
        int e = hap_bt_id_create(&addr, NULL);
#else
        int e = bt_id_create(&addr, NULL);   
#endif
        if (e) {
            LOG_ERR("Creating new ID failed: %d", e);
            HAPFatalError();
        }
    }
}

void HAPPlatformBLEPeripheralManagerClear(HAPPlatformBLEPeripheralManager* blePeripheralManager) {
    if (blePeripheralManager)
        blePeripheralManager->numUsedUUIDBases = 0;
}
