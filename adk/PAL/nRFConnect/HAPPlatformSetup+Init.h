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

#ifndef HAP_PLATFORM_SETUP_INIT_H
#define HAP_PLATFORM_SETUP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "HAP.h"
#include "HAPPlatformThreadCoAPManager+Init.h"

#include "HAPPlatformAccessorySetupNFC+Init.h"
#include "HAPPlatformBLEPeripheralManager+Init.h"
#include "HAPPlatformKeyValueStore.h"
#include "HAPPlatformServiceDiscovery+Init.h"
#include "HAPPlatformThreadCoAPManager+Init.h"

#if __has_feature(nullability)
#pragma clang assume_nonnull begin
#endif

/**
 * Initialize platform drivers which should be initiailized only once till reset.
 */
void HAPPlatformSetupDrivers(void);

/**
 * Checks whether BLE is supported by the platform
 */
bool HAPPlatformSupportsBLE(void);

/**
 * Initialize BLE peripheral manager and platform specific BLE protocol driver
 *
 * @param blePeripheralManager  pointer to a BLE Peripheral Manager to initialize
 * @param keyValueStore         key value store to use by BLE Peripheral Manager
 */
void HAPPlatformSetupInitBLE(
        HAPPlatformBLEPeripheralManager* blePeripheralManager,
        HAPPlatformKeyValueStoreRef keyValueStore);

/**
 * De-initialize BLE drivers
 */
void HAPPlatformSetupDeinitBLE(void);

/**
 * Create key value store with board specific parameters
 *
 * @param keyValueStoreRef   key value store reference to create the store with
 */
void HAPPlatformSetupInitKeyValueStore(HAPPlatformKeyValueStoreRef keyValueStoreRef);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_NFC)
/**
 * Create Accessory Setup NFC tag with board specific parameters
 *
 * @param setupNFC   accessory setup NFC reference
 */
void HAPPlatformSetupInitAccessorySetupNFC(HAPPlatformAccessorySetupNFCRef setupNFC);
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
/**
 * Initialize Apple Authentication Coprocessor provider with platform specific parameters.
 *
 * @param mfiHWAuth  MFiHWAuth reference to initialize
 */
void HAPPlatformSetupInitMFiHWAuth(HAPPlatformMFiHWAuthRef mfiHWAuth);
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)
/**
 * Initialize Thread platform, Thread CoAP manager and service discovery with board specific
 * parameters.
 *
 * @param accessoryServer          pointer to accessory server required for initializing Thread platform
 * @param coapManager              pointer to platform specific CoAP Manager instance to initialize
 * @param coapResources            CoAP Manager resource array
 * @param numResources             number of CoAP Manager resources
 * @param coapRequests             CoAP request array.
 * @param numRequests              size of CoAP request array.
 * @param serviceDiscovery         pointer to platform speciifc Servie Discovery instance to initialize
 */
void HAPPlatformSetupInitThread(
        HAPAccessoryServer* accessoryServer,
        HAPPlatformThreadCoAPManager* coapManager,
        HAPPlatformThreadCoAPManagerResource* resources,
        size_t numResources,
        HAPPlatformThreadCoAPRequest* coapRequests,
        size_t numRequests,
        HAPPlatformServiceDiscovery* serviceDiscovery);

/**
 * Handles OpenThreadVersion characteristic read
 */
HAPError HAPPlatformSetupHandleOpenThreadVersionRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPStringCharacteristicReadRequest* request,
        char* value,
        size_t maxValueBytes,
        void* _Nullable context HAP_UNUSED);
#endif

#if __has_feature(nullability)
#pragma clang assume_nonnull end
#endif

#ifdef __cplusplus
}
#endif

#endif
