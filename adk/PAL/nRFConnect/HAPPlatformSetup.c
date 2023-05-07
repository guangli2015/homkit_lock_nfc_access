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
// Copyright (C) 2020 Apple Inc. All Rights Reserved.

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pal_platform_setup, CONFIG_PAL_PLATFORM_SETUP_LOG_LEVEL);

#include "HAP.h"
#include "HAP+API.h"
#include "HAPBase.h"
#include "HAPPlatformKeyValueStore+Init.h"

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
#include <openthread/instance.h>
#include <openthread/thread.h>
#include "HAPPlatformServiceDiscovery+Init.h"
#include "HAPPlatformThreadCoAPManager+Init.h"
#include "HAPPlatformThreadUtils+Init.h"
#endif
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include "HAPPlatformBLEPeripheralManager+Init.h"
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_NFC)
#include "HAPPlatformAccessorySetupNFC+Init.h"
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)

/**
 * CoAP manager
 */
static HAPPlatformThreadCoAPManager* setupCoapManager;

#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)

/**
 * BLE peripheral manager reference
 */
static HAPPlatformBLEPeripheralManager* setupBlePeripheralManager;

#endif // HAP_FEATURE_BLE

void HAPPlatformSetupDrivers(void) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
    // Perform platform specific initialization.
    HAPPlatformThreadInit();
#endif // HAP_FEATURE_THREAD
}

bool HAPPlatformSupportsBLE(void) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
    return true;
#else
    return false;
#endif
}

bool HAPPlatformSupportsThread(void) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
    return true;
#else
    return false;
#endif
}

void HAPPlatformSetupInitBLE(
        HAPPlatformBLEPeripheralManager* blePeripheralManager,
        HAPPlatformKeyValueStoreRef keyValueStore) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
    // Create BLE Peripheral Manager

    static HAPPlatformBLEPeripheralManagerOptions blePMOptions = { 0 };
    blePMOptions.keyValueStore = keyValueStore;
    static struct bt_uuid_128 uuidBases[CONFIG_HAP_BT_VS_UUID_COUNT];
    blePMOptions.uuid128 = uuidBases;
    blePMOptions.numUUIDBases = HAPArrayCount(uuidBases);

    HAPPlatformBLEPeripheralManagerCreate(blePeripheralManager, &blePMOptions);

    // Set up peripheral manager refernce used by the handler
    setupBlePeripheralManager = blePeripheralManager;
#else
    HAPAssertionFailure();
#endif
}

void HAPPlatformSetupDeinitBLE(void) {
    // Do nothing
}

void HAPPlatformSetupInitKeyValueStore(HAPPlatformKeyValueStoreRef keyValueStoreRef) {
    // Create key-value store with platform specific options
    HAP_ALIGNAS(4)
    static uint8_t keyValueStoreBytes[2048];

    HAPPlatformKeyValueStoreCreate(
            keyValueStoreRef,
            &(const HAPPlatformKeyValueStoreOptions) {
                    .baseFDSFileID = 0xBF00,
                    .baseFDSRecordID = 0xBF00,
                    .bytes = keyValueStoreBytes,
                    .maxBytes = sizeof keyValueStoreBytes,
            });
}

#if HAP_FEATURE_ENABLED(HAP_FEATURE_NFC)
void HAPPlatformSetupInitAccessorySetupNFC(HAPPlatformAccessorySetupNFCRef setupNFC) {
    HAPPlatformAccessorySetupNFCCreate(setupNFC);
}
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
void HAPPlatformSetupInitMFiHWAuth(HAPPlatformMFiHWAuthRef mfiHWAuth) {
    // Do nothing
}
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
void HAPPlatformSetupInitThread(
        HAPAccessoryServer* accessoryServer,
        HAPPlatformThreadCoAPManager* coapManager,
        HAPPlatformThreadCoAPManagerResource* resources,
        size_t numResources,
        HAPPlatformThreadCoAPRequest* coapRequests,
        size_t numRequests,
        HAPPlatformServiceDiscovery* serviceDiscovery) {
    HAPPrecondition(coapManager);
    HAPPrecondition(resources);
    HAPPrecondition(serviceDiscovery);

    setupCoapManager = coapManager;

    HAPPlatformThreadCoAPManagerCreate(
            coapManager,
            &(const HAPPlatformThreadCoAPManagerOptions) { .port = OT_DEFAULT_COAP_PORT,
                                                           .resources = resources,
                                                           .numResources = numResources,
                                                           .requests = coapRequests,
                                                           .numRequests = numRequests });
    HAPPlatformServiceDiscoveryCreate(serviceDiscovery, &(const HAPPlatformServiceDiscoveryOptions) {});
}
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
HAPError HAPPlatformSetupHandleOpenThreadVersionRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPStringCharacteristicReadRequest* request,
        char* value,
        size_t maxValueBytes,
        void* _Nullable context HAP_UNUSED) {
    const char* version = otGetVersionString();
    size_t numBytes = HAPStringGetNumBytes(version);
    if (numBytes >= maxValueBytes) {
        LOG_INF("Not enough space available to send version string.");
        return kHAPError_OutOfResources;
    }
    HAPRawBufferCopyBytes(value, version, numBytes);
    value[numBytes] = '\0';
    return kHAPError_None;
}
#endif
