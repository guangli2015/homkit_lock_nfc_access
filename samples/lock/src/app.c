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
// Copyright (C) 2015-2021 Apple Inc. All Rights Reserved.

// An example that implements the lock HomeKit profile. It can serve as a basic implementation for
// any platform. The accessory logic implementation is reduced to internal state updates and log output.
// The example covers the Lock Mechanism service and the linked Lock Management service.
//
// To enable user interaction following POSIX signals or Buttons are used:
// - Button 1 or signal `SIGUSR1` to clear pairings.
// - Button 2 or signal `SIGUSR2` to trigger a factory reset.
// - Button 3 or Signal `SIGTERM` to trigger pairing mode.
// - Button 4 or Signal `SIGQUIT` to toggle the lock target state between secured and unsecured.
//
// The code consists of multiple parts:
//
//   1. The definition of the accessory configuration and its internal state.
//
//   2. Helper functions to load and save the state of the accessory.
//
//   3. The definitions for the HomeKit attribute database.
//
//   4. The callbacks that implement the actual behavior of the accessory, in this
//      case here they merely access the global accessory state variable and write
//      to the log to make the behavior easily observable.
//
//   5. The initialization of the accessory state.
//
//   6. Callbacks that notify the server in case their associated value has changed.
 
#include <string.h>

#include "HAP+API.h"
#include "HAP.h"

#include "AccessoryInformationServiceDB.h"
#include "App.h"
#include "AppBase.h"
#include "AppLED.h"
#if (HAP_TESTING == 1)
#include "AppUserInput.h"
#endif
#include "ApplicationFeatures.h"
#include "DB.h"
#if (HAVE_ACCESS_CODE == 1)
#include "AccessCodeHelper.h"
#endif
#if (HAP_APP_USES_HDS == 1)
#include "DataStreamTransportManagementServiceDB.h"
#endif
#if (HAVE_FIRMWARE_UPDATE == 1)
#include "FirmwareUpdate.h"
#include "FirmwareUpdateServiceDB.h"
#endif
#if (HAVE_THREAD == 1)
#include "ThreadManagementServiceDB.h"
#endif
#if (HAVE_ACCESSORY_REACHABILITY == 1)
#include "AccessoryRuntimeInformationServiceDB.h"
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
#include "DiagnosticsService.h"
#include "DiagnosticsServiceDB.h"
#include "HAPDiagnostics.h"
#endif

#include "PowerManagment.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Key used in the key value store to store the configuration state.
 *
 * Purged: On factory reset.
 */
#define kAppKeyValueStoreKey_Configuration_State ((HAPPlatformKeyValueStoreKey) 0x00)

#if (HAVE_ACCESS_CODE == 1)
/**
 * Key store domain dedicated to access code list
 */
#define kHAPPlatformAccessCodeKeyStoreDomain ((HAPPlatformKeyValueStoreDomain) 0x10)

/**
 * Number of keypad attempts allowed in a ten minute window
 */
#define kAppAttemptsAllowedInTenMinutes 10

/**
 * Number of keypad attempts allowed in an hour window
 */
#define kAppAttemptsAllowedInHour 20

/**
 * 10 minutes in milliseconds.
 */
#define kAppMillisecondsPerTenMinutes ((HAPTime)(10 * HAPMinute))
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if (HAVE_NFC_ACCESS == 1)
/**
 * NFC Access write response TLV storage buffer size in bytes
 *   - The following multiplied by max number of suspended device credential keys supported:
 *     - Number of bytes for Identifier field
 *     - Number of bytes for Issuer Key Identifier field
 *     - Number of bytes for Status Code field
 *     - 2 bytes for type/length of Device Credential Key Response
 *     - 6 bytes for type/length of each field in Device Credential Key Response
 *   - 38 bytes for max separators 0000 between each Device Credential Key Response
 */
#define kAppNfcAccessResponseBufferBytes (20 * (8 + 8 + sizeof(uint8_t) + 2 + 6) + 38)

/**
 * Key store domain dedicated to NFC Access lists
 */
#define kHAPPlatformNfcAccessKeyStoreDomain ((HAPPlatformKeyValueStoreDomain) 0x11)

/**
 * The salt value used with a key value to create a hash for the Identifier field
 */
#define kHAPPlatformNfcAccessKeyIdentifierSalt "key-identifier"
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if (HAVE_LOCK_ENC == 1)
#define CONTEXT_TLV_MAX_SIZE          64
#define STATE_CHANGE_SOURCE_KEYPAD    1
#define STATE_CHANGE_SOURCE_NFC       2
#define STATE_CHANGE_SOURCE_HAP       3
#define CONTEXT_TIMESTAMP_TICK_STRIDE 100 // Number of milliseconds per tick
#define CONTEXT_IDENTIFIER_SIZE       4

static char contextDataTLVBuffer[CONTEXT_TLV_MAX_SIZE];
static HAPTLVWriter contextTLVWriter;
#endif

typedef struct {
    AppLEDIdentifier lockStateLedPin;
#if (HAVE_ACCESS_CODE == 1)
    AppLEDIdentifier activeLedPin;
#endif
    AppLEDIdentifier identifyPin;
} Device;

/**
 * Global accessory configuration.
 */
typedef struct {
    struct {
        HAPCharacteristicValue_LockCurrentState currentState;
        HAPCharacteristicValue_LockTargetState targetState;
        HAPCharacteristicValue_StatusLowBattery statusLowBattery;
#if (HAVE_LOCK_ENC == 1)
        // Data related to event notification context information
        struct {
            uint64_t bootTime;
            uint64_t lastStateChangeTime;
            bool isContextPresent;
            uint8_t contextIdentifier[CONTEXT_IDENTIFIER_SIZE];
            uint8_t source;
            uint32_t contextTimestamp; // In 'ticks' per event notification context spec
        } contextData;
#endif
#if (HAVE_ACCESS_CODE == 1)
        HAPCharacteristicValue_Active active;

        struct {
            uint8_t numAttempts;
            HAPTime timestamps[kAppAttemptsAllowedInTenMinutes];
        } keypadAttemptsInLastTenMinutes;

        struct {
            uint8_t numAttempts;
            HAPTime timestamps[kAppAttemptsAllowedInHour];
        } keypadAttemptsInLastHour;
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
        uint32_t diagnosticsSelectedModeState;
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
    } state;
    Device device;
    HAPPlatformTimerRef identifyTimer;
    bool jammed;
    HAPAccessoryServer* server;
    HAPPlatformKeyValueStoreRef keyValueStore;
    bool useAAD;
    HAPPlatformTimerRef keypadTimer;

} AccessoryConfiguration;

#if (HAVE_ACCESS_CODE == 1)
/**
 * Access code lock state change info
 */
typedef struct {
    uint32_t accessCodeIdentifier;
    bool locked;
} AccessCodeLockStateChangeInfo;
#endif

static AccessoryConfiguration accessoryConfiguration;
static HAPAccessory accessory;

#if (HAVE_ACCESSORY_REACHABILITY == 1)
static HAPAccessoryReachabilityConfiguration accessoryReachabilityConfig = {
    .sleepIntervalInMs = kAccessorySleepInterval,
};
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if (HAP_APP_USES_HDS == 1)
#if (HAP_APP_USES_HDS_STREAM == 1)
static HAPStreamDataStreamProtocol streamDataStreamProtocol = {
    .base = &kHAPStreamDataStreamProtocol_Base,
    .numDataStreams = kApp_NumDataStreams,
    .applicationProtocols = (HAPStreamApplicationProtocol* const[]) { &streamProtocolUARP, NULL }
};
#endif // HAP_APP_USES_HDS_STREAM

#if (HAVE_DIAGNOSTICS_SERVICE == 1)
static HAPDataSendStreamProtocolStreamAvailableCallbacks dataSendDataStreamProtocolAvailableCallbacks[] = {
    { .type = kHAPDataSendDataStreamProtocolType_Diagnostics_Snapshot,
      .handleStreamAvailable = HAPDiagnosticsHandleDataSendStreamAvailable }
};
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)

static HAPDataSendDataStreamProtocolContext dataSendDataStreamProtocolContexts[kApp_NumDataStreams];
static HAPDataSendDataStreamProtocolListener dataSendDataStreamProtocolListeners[kApp_NumDataStreams];
static HAPDataSendDataStreamProtocol
        dataSendDataStreamProtocol = { .base = &kHAPDataSendDataStreamProtocol_Base,
                                       .storage = { .numDataStreams = kApp_NumDataStreams,
                                                    .protocolContexts = dataSendDataStreamProtocolContexts,
                                                    .listeners = dataSendDataStreamProtocolListeners },
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
                                       .callbacks = {
                                               .numStreamAvailableCallbacks = 1,
                                               .streamAvailableCallbacks = dataSendDataStreamProtocolAvailableCallbacks,
                                       },
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
};

static HAPDataStreamDescriptor dataStreamDescriptors[kApp_NumDataStreams];
static HAPDataStreamDispatcher dataStreamDispatcher;
const HAPDataStreamDispatcherStorage dataStreamDispatcherStorage = {
    .numDataStreams = kApp_NumDataStreams,
    .dataStreamDescriptors = dataStreamDescriptors,
    .dataStreamProtocols =
            (HAPDataStreamProtocol* const[]) {
                    &dataSendDataStreamProtocol,
#if (HAP_APP_USES_HDS_STREAM == 1)
                    &streamDataStreamProtocol,
#endif
                    NULL,
            },
};
#endif // HAP_APP_USES_HDS

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------------------------------------------------

/**
 * Initialize a default accessory state that validates with the device configuration.
 */
static void SetupDefaultAccessoryState(void) {
    HAPRawBufferZero(&accessoryConfiguration.state, sizeof accessoryConfiguration.state);

#if (HAVE_ACCESS_CODE == 1)
    // Keypad should be enabled by default
    accessoryConfiguration.state.active = kHAPCharacteristicValue_Active_Active;
#endif

#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    accessoryConfiguration.state.diagnosticsSelectedModeState = kHAPCharacteristicValue_SelectedDiagnosticsModes_None;
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}

/**
 * Load the accessory state from persistent memory.
 */
static void LoadAccessoryState(void) {
    HAPPrecondition(accessoryConfiguration.keyValueStore);

    HAPError err;

    // Load persistent state if available
    bool found;
    size_t numBytes;

    err = HAPPlatformKeyValueStoreGet(
            accessoryConfiguration.keyValueStore,
            kAppKeyValueStoreDomain_Configuration,
            kAppKeyValueStoreKey_Configuration_State,
            &accessoryConfiguration.state,
            sizeof accessoryConfiguration.state,
            &numBytes,
            &found);

    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        HAPFatalError();
    }
    if (!found || numBytes != sizeof accessoryConfiguration.state) {
        if (found) {
            HAPLogError(&kHAPLog_Default, "Unexpected app state found in key-value store. Resetting to default.");
        }
        SetupDefaultAccessoryState();
    }
}

/**
 * Save the accessory state to persistent memory
 */
static void SaveAccessoryState(void) {
    HAPPrecondition(accessoryConfiguration.keyValueStore);

    HAPError err;
    err = HAPPlatformKeyValueStoreSet(
            accessoryConfiguration.keyValueStore,
            kAppKeyValueStoreDomain_Configuration,
            kAppKeyValueStoreKey_Configuration_State,
            &accessoryConfiguration.state,
            sizeof accessoryConfiguration.state);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
        HAPFatalError();
    }
}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Attempt to secure the lock.
 */
static HAPCharacteristicValue_LockCurrentState DeviceSecureLock(void) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    if (accessoryConfiguration.jammed) {
        return kHAPCharacteristicValue_LockCurrentState_Jammed;
    } else {
        return kHAPCharacteristicValue_LockCurrentState_Secured;
    }
}

/**
 * Attempt to unsecure the lock.
 */
static HAPCharacteristicValue_LockCurrentState DeviceUnsecureLock(void) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    if (accessoryConfiguration.jammed) {
        return kHAPCharacteristicValue_LockCurrentState_Jammed;
    } else {
        return kHAPCharacteristicValue_LockCurrentState_Unsecured;
    }
}

#if (HAP_TESTING == 1)
/**
 * Block the lock - next (un-)secure lock will result in jammed state.
 */
static void DeviceBlockLock(void) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    accessoryConfiguration.jammed = true;
}

/**
 * Unblock the lock - next (un-)secure lock will succeed.
 */
static void DeviceUnblockLock(void) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    accessoryConfiguration.jammed = false;
}
#endif

/**
 * Enable LED on the device
 */
static void DeviceEnableLED(AppLEDIdentifier pin) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    AppLEDSet(pin, true);
}

/**
 * Disable LED on the device
 */
static void DeviceDisableLED(AppLEDIdentifier pin) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    AppLEDSet(pin, false);
}

void SetLockTargetState(HAPCharacteristicValue_LockTargetState targetState) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    HAPCharacteristicValue_LockCurrentState newCurrentState = 0;

    switch (targetState) {
        case kHAPCharacteristicValue_LockTargetState_Secured:
            newCurrentState = DeviceSecureLock();
            DeviceEnableLED(accessoryConfiguration.device.lockStateLedPin);

            break;
        case kHAPCharacteristicValue_LockTargetState_Unsecured:
            newCurrentState = DeviceUnsecureLock();
            DeviceDisableLED(accessoryConfiguration.device.lockStateLedPin);
            break;
    }

    if (accessoryConfiguration.state.targetState != targetState ||
        accessoryConfiguration.state.currentState != newCurrentState) {
        if (accessoryConfiguration.state.targetState != targetState) {
            accessoryConfiguration.state.targetState = targetState;

#if (HAVE_ACCESS_CODE == 1)
            // Re-enable the keypad if it has been disabled
            if (accessoryConfiguration.state.active == kHAPCharacteristicValue_Active_Inactive) {
                accessoryConfiguration.state.active = kHAPCharacteristicValue_Active_Active;
                HAPAccessoryServerRaiseEvent(
                        accessoryConfiguration.server, &accessCodeActiveCharacteristic, &accessCodeService, &accessory);
            }

            // Clear any invalid attempts
            HAPRawBufferZero(
                    &accessoryConfiguration.state.keypadAttemptsInLastTenMinutes,
                    sizeof(accessoryConfiguration.state.keypadAttemptsInLastTenMinutes));
            HAPRawBufferZero(
                    &accessoryConfiguration.state.keypadAttemptsInLastHour,
                    sizeof(accessoryConfiguration.state.keypadAttemptsInLastHour));
#endif

            HAPAccessoryServerRaiseEvent(
                    accessoryConfiguration.server,
                    &lockMechanismLockTargetStateCharacteristic,
                    &lockMechanismService,
                    &accessory);
        }
        if (accessoryConfiguration.state.currentState != newCurrentState) {
            accessoryConfiguration.state.currentState = newCurrentState;

            HAPAccessoryServerRaiseEvent(
                    accessoryConfiguration.server,
                    &lockMechanismLockCurrentStateCharacteristic,
                    &lockMechanismService,
                    &accessory);
        }

        SaveAccessoryState();
    }
}

#if (HAP_TESTING == 1)
void SetLockCurrentState(HAPCharacteristicValue_LockCurrentState newCurrentState) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    if (accessoryConfiguration.state.currentState != newCurrentState) {
        accessoryConfiguration.state.currentState = newCurrentState;

        HAPAccessoryServerRaiseEvent(
                accessoryConfiguration.server,
                &lockMechanismLockCurrentStateCharacteristic,
                &lockMechanismService,
                &accessory);

        SaveAccessoryState();
    }
}

void ToggleLockState(void) {
    HAPLogInfo(&kHAPLog_Default, "%s (Simulating manual lock/unlock)", __func__);
#if (HAVE_LOCK_ENC == 1)
    accessoryConfiguration.state.contextData.isContextPresent = false;
#endif
    switch (accessoryConfiguration.state.targetState) {
        case kHAPCharacteristicValue_LockTargetState_Secured: {
            SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Unsecured);
            break;
        }
        case kHAPCharacteristicValue_LockTargetState_Unsecured: {
            SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Secured);
            break;
        }
    }
}

void ToggleLockJammed(void) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    if (accessoryConfiguration.jammed) {
        DeviceUnblockLock();
    } else {
        DeviceBlockLock();
    }
}

void SetStatusLowBatteryState(HAPCharacteristicValue_StatusLowBattery statusLowBattery) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    if (accessoryConfiguration.state.statusLowBattery != statusLowBattery) {
        accessoryConfiguration.state.statusLowBattery = statusLowBattery;

        HAPAccessoryServerRaiseEvent(
                accessoryConfiguration.server, &batteryStatusLowCharacteristic, &batteryService, &accessory);

        SaveAccessoryState();
    }
}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Global user input handler.
 *
 * Button and signal mapping to kAppUserInputIdentifier can be found at Applications/Common/AppUserInput.h
 */
static void HandleUserInputEventCallback(void* _Nullable context, size_t contextSize) {
    HAPPrecondition(context);
    HAPAssert(contextSize == sizeof(AppUserInputEvent));

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    AppUserInputEvent buttonEvent = *((AppUserInputEvent*) context);
    switch (buttonEvent.id) {
        case kAppUserInputIdentifier_4: { // SIGQUIT or Button 4
            ToggleLockState();
            break;
        }
        default: {
            break;
        }
    }
}

#endif

/**
 * Unconfigure platform specific IO.
 */
static void UnconfigureIO(void) {
    HAPLogDebug(&kHAPLog_Default, "%s", __func__);

    AppLEDDeinit();
}

/**
 * Configure platform specific IO.
 */
static void ConfigureIO(void) {
    HAPLogDebug(&kHAPLog_Default, "%s", __func__);

#if (HAP_TESTING == 1)
    // Setup user input event handler
    AppUserInputInit();
    AppUserInputRegisterCallback(HandleUserInputEventCallback);
#endif

    // Assign LEDs.
    accessoryConfiguration.device.lockStateLedPin = kAppLEDIdentifier_1;
    accessoryConfiguration.device.identifyPin = kAppLEDIdentifier_2;
#if (HAVE_ACCESS_CODE == 1)
    accessoryConfiguration.device.activeLedPin = kAppLEDIdentifier_2;
#endif

    // Initialize LED driver
    AppLEDInit();
}

//----------------------------------------------------------------------------------------------------------------------
static char firmware_version[kHAPFirmwareVersion_MaxLength] = CONFIG_MCUBOOT_IMAGE_VERSION;
static void fix_firmware_version_for_HAP(void) {
    char* end_of_valid_version = strstr(firmware_version, "+");
    if (end_of_valid_version != NULL) {
        // found build part in version string, remove it
        *end_of_valid_version = '\0';
    } else {
        // version is already valid
    }
}
/**
 * HomeKit accessory that provides the Lock service.
 */
static HAPAccessory accessory = { .aid = 1,
                                  .category = kHAPAccessoryCategory_Locks,
                                  .name = "Nordic Lock",
                                  .productData = "b3e691cc3020f889",
                                  .manufacturer = "Nordic Semiconductor",
                                  .model = "Lock1,1",
                                  .serialNumber = "000000001002",
                                  .firmwareVersion = firmware_version,
                                  .hardwareVersion = "1.0.0",
#if (HAVE_NFC_ACCESS == 1)
                                  .hardwareFinish = HARDWARE_FINISH_MATTE_BLACK,
#endif
                                  .services = (const HAPService* const[]) { &accessoryInformationService,
                                                                            &hapProtocolInformationService,
                                                                            &pairingService,
                                                                            &lockMechanismService,
#if (HAVE_ACCESS_CODE == 1)
                                                                            &accessCodeService,
#endif
#if (HAVE_NFC_ACCESS == 1)
                                                                            &nfcAccessService,
#endif
#if (HAVE_THREAD == 1)
                                                                            &threadManagementService,
#endif
#if (HAP_TESTING == 1)
                                                                            &debugService,
#endif
#if (HAVE_ACCESSORY_REACHABILITY == 1)
                                                                            &accessoryRuntimeInformationService,
#endif
#if (HAP_APP_USES_HDS == 1)
                                                                            &dataStreamTransportManagementService,
#endif
#if (HAVE_FIRMWARE_UPDATE == 1)
                                                                            &firmwareUpdateService,
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
                                                                            &diagnosticsService,
#endif
                                                                            &batteryService,
                                                                            NULL },
#if (HAP_APP_USES_HDS == 1)
                                  .dataStream.delegate = { .callbacks = &kHAPDataStreamDispatcher_DataStreamCallbacks,
                                                           .context = &dataStreamDispatcher },
#endif
#if (HAVE_ACCESSORY_REACHABILITY == 1)
                                  .reachabilityConfiguration = &accessoryReachabilityConfig,
#endif
                                  .callbacks = {
                                          .identify = IdentifyAccessory,
#if (HAVE_FIRMWARE_UPDATE == 1)
                                          .firmwareUpdate = { .getAccessoryState = FirmwareUpdateGetAccessoryState },
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
                                          .diagnosticsConfig = { .getDiagnosticsConfig =
                                                                         GetAccessoryDiagnosticsConfig },
#endif
                                  } };

//----------------------------------------------------------------------------------------------------------------------

#if (HAVE_LOCK_ENC == 1)
static void UpdateLocalTimestamps() {
    accessoryConfiguration.state.contextData.lastStateChangeTime = HAPPlatformClockGetCurrent();
    accessoryConfiguration.state.contextData.contextTimestamp = (uint32_t)(
            (accessoryConfiguration.state.contextData.lastStateChangeTime -
             accessoryConfiguration.state.contextData.bootTime) /
            CONTEXT_TIMESTAMP_TICK_STRIDE);
}

#if (HAVE_NFC_ACCESS == 1)
static void UpdateNfcIdentifier(uint8_t* issuerKeyIdentifier) {
    HAPRawBufferCopyBytes(
            &accessoryConfiguration.state.contextData.contextIdentifier, issuerKeyIdentifier, CONTEXT_IDENTIFIER_SIZE);
}
#endif

static void PrintContextData() {
    HAPLogInfo(&kHAPLog_Default, "==Current Context Data==");
    HAPLogInfo(
            &kHAPLog_Default,
            "Boot time: %llu",
            (unsigned long long) accessoryConfiguration.state.contextData.bootTime);
    HAPLogInfo(
            &kHAPLog_Default,
            "Last state change: %llu",
            (unsigned long long) accessoryConfiguration.state.contextData.lastStateChangeTime);
    HAPLogInfo(&kHAPLog_Default, "Is Context Present: %d", accessoryConfiguration.state.contextData.isContextPresent);
    HAPLogInfo(
            &kHAPLog_Default,
            "Context Identifier: 0x%02x%02x%02x%02x",
            accessoryConfiguration.state.contextData.contextIdentifier[0],
            accessoryConfiguration.state.contextData.contextIdentifier[1],
            accessoryConfiguration.state.contextData.contextIdentifier[2],
            accessoryConfiguration.state.contextData.contextIdentifier[3]);
    HAPLogInfo(&kHAPLog_Default, "Source: %u", accessoryConfiguration.state.contextData.source);
    HAPLogInfo(
            &kHAPLog_Default,
            "Timestamp: %llu",
            (unsigned long long) accessoryConfiguration.state.contextData.contextTimestamp);
}
#endif // (HAVE_LOCK_ENC == 1)

#if (HAVE_NFC_ACCESS == 1)
static void
        AddHapPairingLtpkAsIssuerKey(const HAPControllerPublicKey* publicKey, NfcAccessIssuerKeyCacheType cacheType) {
    HAPPrecondition(publicKey);

    HAPPlatformNfcAccessIssuerKey issuerKey = {
        // HAP Pairing keys are Ed25519
        .type = kHAPCharacteristicValue_NfcAccessControlPoint_KeyType_Ed25519,
        .key = (uint8_t*) publicKey->bytes,
        .keyNumBytes = sizeof publicKey->bytes,
    };

    NfcAccessStatusCode statusCode;
    HAPError err = HAPPlatformNfcAccessIssuerKeyAdd(&issuerKey, cacheType, &statusCode);
    if ((err != kHAPError_None) || (statusCode != NFC_ACCESS_STATUS_CODE_SUCCESS)) {
        HAPLogError(&kHAPLog_Default, "%s: Failed to add HAP pairing LTPK as issuer key", __func__);
    }
}

static void RemoveHapPairingLtpkAsIssuerKey(const HAPControllerPublicKey* publicKey) {
    // Generate the identifier from the key
    uint8_t identifier[NFC_ACCESS_KEY_IDENTIFIER_BYTES];
    HAPPlatformNfcAccessGenerateIdentifier(publicKey->bytes, sizeof publicKey->bytes, identifier);

    HAPPlatformNfcAccessIssuerKey issuerKey = {
        .identifier = identifier,
    };

    NfcAccessStatusCode statusCode;
    HAPError err = HAPPlatformNfcAccessIssuerKeyRemove(
            &issuerKey, NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_REMOVE, &statusCode);
    if ((err != kHAPError_None) || (statusCode != NFC_ACCESS_STATUS_CODE_SUCCESS)) {
        HAPLogError(&kHAPLog_Default, "%s: Failed to remove HAP pairing LTPK as issuer key", __func__);
    }
}

static void CachePairingEnumerateCallback(
        void* _Nullable context HAP_UNUSED,
        HAPPlatformKeyValueStoreRef keyValueStore HAP_UNUSED,
        const HAPControllerPairingIdentifier* pairingIdentifier HAP_UNUSED,
        const HAPControllerPublicKey* publicKey,
        bool isAdmin HAP_UNUSED,
        bool* shouldContinue) {
    HAPPrecondition(publicKey);
    HAPPrecondition(shouldContinue);

    AddHapPairingLtpkAsIssuerKey(publicKey, NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_READ);
    *shouldContinue = true;
}
#endif

/**
 * Control duration of Identify indication.
 */
static void IdentifyTimerExpired(HAPPlatformTimerRef timer, void* _Nullable context) {
    HAPLogDebug(&kHAPLog_Default, "%s", __func__);

    HAPPrecondition(!context);
    HAPPrecondition(timer == accessoryConfiguration.identifyTimer);

    accessoryConfiguration.identifyTimer = 0;

    AppLEDSet(accessoryConfiguration.device.identifyPin, false);
}

/**
 * Performs the Identify routine.
 */
static void DeviceIdentify(void) {
    HAPError err;

    if (accessoryConfiguration.identifyTimer) {
        HAPPlatformTimerDeregister(accessoryConfiguration.identifyTimer);
        accessoryConfiguration.identifyTimer = 0;
    }
    err = HAPPlatformTimerRegister(
            &accessoryConfiguration.identifyTimer,
            HAPPlatformClockGetCurrent() + 3 * HAPSecond,
            IdentifyTimerExpired,
            NULL);
    if (err) {
        HAPAssert(err == kHAPError_OutOfResources);
    }

    AppLEDSet(accessoryConfiguration.device.identifyPin, true);
}

HAP_RESULT_USE_CHECK
HAPError IdentifyAccessory(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPAccessoryIdentifyRequest* request HAP_UNUSED,
        void* _Nullable context HAP_UNUSED) {
    HAPLogDebug(&kHAPLog_Default, "%s", __func__);
    /* VENDOR-TODO: Trigger LED/sound notification to identify accessory */
    DeviceIdentify();
    return kHAPError_None;
}

/**
 * Handle read request to the 'Lock Current State' characteristic of the Lock Mechanism service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleLockMechanismLockCurrentStateRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

    *value = accessoryConfiguration.state.currentState;

    switch (*value) {
        case kHAPCharacteristicValue_LockCurrentState_Secured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockCurrentState_Secured");
            break;
        }
        case kHAPCharacteristicValue_LockCurrentState_Unsecured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockCurrentState_Unsecured");
            break;
        }
        case kHAPCharacteristicValue_LockCurrentState_Jammed: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockCurrentState_Jammed");
            break;
        }
        case kHAPCharacteristicValue_LockCurrentState_Unknown: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockCurrentState_Unknown");
            break;
        }
    }
    return kHAPError_None;
}

/**
 * Handle read request to the 'Lock Target State' characteristic of the Lock Mechanism service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleLockMechanismLockTargetStateRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    *value = accessoryConfiguration.state.targetState;
    switch (*value) {
        case kHAPCharacteristicValue_LockTargetState_Secured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockTargetState_Secured");
            break;
        }
        case kHAPCharacteristicValue_LockTargetState_Unsecured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockTargetState_Unsecured");
            break;
        }
    }
    return kHAPError_None;
}

/**
 * Handle write request to the 'Lock Target State' characteristic of the Lock Mechanism service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleLockMechanismLockTargetStateWrite(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicWriteRequest* request,
        uint8_t value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);

#if (HAVE_LOCK_ENC == 1)
    HAPError err;
    if (request->contextData.numBytes) {
        HAPTLV tlv;
        HAPTLVReader reader;
        HAPTLVReaderCreate(&reader, (void*) request->contextData.bytes, request->contextData.numBytes);
        bool found = false;

        // We expect exactly one TLV with:
        //   TYPE = CONTEXT_TLV_TYPE_IDENTIFIER
        //   LENGTH = CONTEXT_IDENTIFIER_SIZE
        err = HAPTLVReaderGetNext(&reader, &found, &tlv);
        if (err) {
            HAPLogError(&kHAPLog_Default, "Error while parsing context data for Lock.");
            return kHAPError_InvalidData;
        }
        if (!found || tlv.type != kHAPCharacteristicTLVType_LockContextIdentifier ||
            tlv.value.numBytes != CONTEXT_IDENTIFIER_SIZE) {
            HAPLogError(&kHAPLog_Default, "Invalid context data for Lock.");
            return kHAPError_InvalidData;
        }
        HAPRawBufferCopyBytes(
                accessoryConfiguration.state.contextData.contextIdentifier, tlv.value.bytes, tlv.value.numBytes);
        UpdateLocalTimestamps();
        accessoryConfiguration.state.contextData.source = STATE_CHANGE_SOURCE_HAP;
        accessoryConfiguration.state.contextData.isContextPresent = true;

        HAPLogInfo(&kHAPLog_Default, "New Context Data written");
        PrintContextData();

    } else {
        accessoryConfiguration.state.contextData.isContextPresent = false;
    }
#endif

    if (accessoryConfiguration.useAAD == true) {
        // Additional authorization data is controller-provided data that the accessory may use to validate that the
        // controller is authorized to perform a requested operation. The contents of the authorization data are
        // manufacturer specific.
        // See HomeKit Accessory Protocol Specification R17
        // Section 2.3.3.2 Additional Authorization Data
        HAPLogInfo(&kHAPLog_Default, "%s: Using Additional Authorization Data", __func__);
        if (request->authorizationData.numBytes) {
            HAPLogBufferDebug(
                    &kHAPLog_Default,
                    request->authorizationData.bytes,
                    request->authorizationData.numBytes,
                    "%s: Authorized - Additional Authorization Data provided: ",
                    __func__);
            // In this example the request is accepted if any authorization data was provided.
        } else {
            HAPLogError(&kHAPLog_Default, "%s: Not Authorized - Additional Authorization Data expected.", __func__);
            return kHAPError_NotAuthorized;
        }
    }

    HAPCharacteristicValue_LockTargetState targetState = (HAPCharacteristicValue_LockTargetState) value;
    switch (targetState) {
        case kHAPCharacteristicValue_LockTargetState_Secured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockTargetState_Secured");
            break;
        }
        case kHAPCharacteristicValue_LockTargetState_Unsecured: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "LockTargetState_Unsecured");
            break;
        }
    }

    SetLockTargetState(targetState);

    return kHAPError_None;
}

/**
 * Handle read request to the 'Battery Level' characteristic of the Battery service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleBatteryLevelRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    uint8_t batteryLevel = 100;
    // Query the battery sub-system and report battery level
    *value = batteryLevel;
    return kHAPError_None;
}

/**
 * Handle read request to the 'Charging State' characteristic of the Battery service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleBatteryChargingStateRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    // Query the battery sub-system and report battery charging state
    *value = kHAPCharacteristicValue_ChargingState_NotCharging;
    return kHAPError_None;
}

/**
 * Handle read request to the 'Status Low Battery' characteristic of the Battery service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleBatteryStatusLowRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    // Query the battery sub-system and report low battery status
    *value = accessoryConfiguration.state.statusLowBattery;
    switch (*value) {
        case kHAPCharacteristicValue_StatusLowBattery_Normal: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Normal");
            break;
        }
        case kHAPCharacteristicValue_StatusLowBattery_Low: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Low");
            break;
        }
    }
    return kHAPError_None;
}

#if (HAVE_ACCESS_CODE == 1)
HAP_RESULT_USE_CHECK
HAPError HandleAccessCodeActiveRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicReadRequest* request HAP_UNUSED,
        uint8_t* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.active;

    switch (*value) {
        case kHAPCharacteristicValue_Active_Active: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Active");
            break;
        }
        case kHAPCharacteristicValue_Active_Inactive: {
            HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Inactive");
            break;
        }
    }

    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleAccessCodeActiveWrite(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt8CharacteristicWriteRequest* request HAP_UNUSED,
        uint8_t value,
        void* _Nullable context HAP_UNUSED) {
    HAPCharacteristicValue_Active active = (HAPCharacteristicValue_Active) value;
    if (accessoryConfiguration.state.active != active) {
        accessoryConfiguration.state.active = active;

        switch (active) {
            case kHAPCharacteristicValue_Active_Active: {
                HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Active");
                DeviceEnableLED(accessoryConfiguration.device.activeLedPin);

                // Clear any invalid attempts
                HAPRawBufferZero(
                        &accessoryConfiguration.state.keypadAttemptsInLastTenMinutes,
                        sizeof(accessoryConfiguration.state.keypadAttemptsInLastTenMinutes));
                HAPRawBufferZero(
                        &accessoryConfiguration.state.keypadAttemptsInLastHour,
                        sizeof(accessoryConfiguration.state.keypadAttemptsInLastHour));
                break;
            }
            case kHAPCharacteristicValue_Active_Inactive: {
                HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, "Inactive");
                DeviceDisableLED(accessoryConfiguration.device.activeLedPin);
                break;
            }
        }

        SaveAccessoryState();

        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
    return kHAPError_None;
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * Callback to handle keypad disable timer expiration. Keypad can be re-enabled and notified.
 */
static void HandleKeypadDisableTimerCallback(HAPPlatformTimerRef timer HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
    accessoryConfiguration.keypadTimer = 0;
    accessoryConfiguration.state.active = kHAPCharacteristicValue_Active_Active;
    HAPAccessoryServerRaiseEvent(
            accessoryConfiguration.server, &accessCodeActiveCharacteristic, &accessCodeService, &accessory);
    SaveAccessoryState();
}

/**
 * Enable a timer to disable the keypad for a set amount of time.
 *
 * @param   disableTime   Amount of time to disable the keypad
 */
static void EnableKeypadDisableTimer(HAPTime keypadDisableTime) {
    // Cap the timer to an hour
    if (keypadDisableTime > HAPHour) {
        keypadDisableTime = HAPHour;
    }

    HAPLogInfo(&kHAPLog_Default, "%s: Set timer for %llu ms", __func__, (unsigned long long) keypadDisableTime);

    // Timer is already running
    if (accessoryConfiguration.keypadTimer) {
        return;
    }

    HAPError err = HAPPlatformTimerRegister(
            &accessoryConfiguration.keypadTimer,
            HAPPlatformClockGetCurrent() + keypadDisableTime,
            HandleKeypadDisableTimerCallback,
            accessoryConfiguration.server);
    if (err) {
        HAPAssert(err == kHAPError_OutOfResources);
        HAPFatalError();
    }
}

/**
 * Remove any failed keypad attempts that are outside of the sliding window
 *
 * @param   currentTime           The timestamp used to determine if a failed attempt is outside of the sliding window
 * @param   timestamps            The array of timestamps holding existing failed attempts
 * @param   window                The window to check against to determine whether a failed attempt should be removed
 * @param   numAttempts[in/out]   The number of attempts stored in the timestamps param
 */
static void RefreshKeypadAttempts(HAPTime currentTime, HAPTime* timestamps, HAPTime window, uint8_t* numAttempts) {
    HAPPrecondition(timestamps);
    HAPPrecondition(numAttempts);

    size_t newFirstIndex = 0;

    for (size_t i = 0; i < *numAttempts; i++) {
        HAPTime loggedTime = timestamps[i];
        HAPAssert(loggedTime <= currentTime);

        // The logged time is not within the sliding window so it can be removed
        if ((currentTime - loggedTime) > window) {
            newFirstIndex = i + 1;
        }
    }

    if (newFirstIndex > 0) {
        size_t remainderCount = *numAttempts - newFirstIndex;

        if (newFirstIndex == *numAttempts) {
            // All attempts are beyond the sliding window
            HAPRawBufferZero(timestamps, *numAttempts * sizeof(HAPTime));
            *numAttempts = 0;
        } else if (remainderCount > 0) {
            // Keep all attempts within the sliding window
            HAPRawBufferCopyBytes(&timestamps[0], &timestamps[newFirstIndex], remainderCount * sizeof(HAPTime));
            *numAttempts = remainderCount;
        }
    }
}

#if (HAP_TESTING == 1)
/**
 * Log a new failed keypad attempt for the last 10 minutes and last hour
 *
 * @param   currentTime   The timestamp used to to log the current failed attempt
 */
static void LogKeypadAttempt(HAPTime currentTime) {
    // If keypad is disabled, there is no need to log this failed attempt
    if (accessoryConfiguration.state.active == kHAPCharacteristicValue_Active_Inactive) {
        HAPLogInfo(&kHAPLog_Default, "%s: Keypad is disabled", __func__);
        return;
    }

    // Remove any obsolete attempts for the last 10 minutes
    RefreshKeypadAttempts(
            currentTime,
            accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.timestamps,
            kAppMillisecondsPerTenMinutes,
            &accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.numAttempts);

    // Log failed attempt for the last 10 minutes
    uint8_t numAttempts = accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.numAttempts;
    HAPAssert(numAttempts < kAppAttemptsAllowedInTenMinutes);
    accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.timestamps[numAttempts] = currentTime;
    accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.numAttempts++;

    // Remove any obsolete attempts for the last hour
    RefreshKeypadAttempts(
            currentTime,
            accessoryConfiguration.state.keypadAttemptsInLastHour.timestamps,
            HAPHour,
            &accessoryConfiguration.state.keypadAttemptsInLastHour.numAttempts);

    // Log failed attempt for the last hour
    numAttempts = accessoryConfiguration.state.keypadAttemptsInLastHour.numAttempts;
    HAPAssert(numAttempts < kAppAttemptsAllowedInHour);
    accessoryConfiguration.state.keypadAttemptsInLastHour.timestamps[numAttempts] = currentTime;
    accessoryConfiguration.state.keypadAttemptsInLastHour.numAttempts++;

    SaveAccessoryState();
}
#endif // (HAP_TESTING == 1)

/**
 * Set the status of the keypad based on current state of failed attempts
 *
 * @param   currentTime   The timestamp used to determine how long to disable the keypad for, if applicable
 */
static void SetKeypadStatus(HAPTime currentTime) {
    HAPTime keypadDisableTimeTenMinutes = 0;
    HAPTime keypadDisableTimeHour = 0;
    HAPTime keypadDisableTime = 0;

    // Remove any obsolete attempts for the last 10 minutes
    RefreshKeypadAttempts(
            currentTime,
            accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.timestamps,
            kAppMillisecondsPerTenMinutes,
            &accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.numAttempts);

    // Reached max failed attempts allowed in the last 10 minutes
    if (accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.numAttempts >= kAppAttemptsAllowedInTenMinutes) {
        keypadDisableTimeTenMinutes =
                kAppMillisecondsPerTenMinutes -
                (currentTime - accessoryConfiguration.state.keypadAttemptsInLastTenMinutes.timestamps[0]);
    }

    // Remove any obsolete attempts for the last hour
    RefreshKeypadAttempts(
            currentTime,
            accessoryConfiguration.state.keypadAttemptsInLastHour.timestamps,
            HAPHour,
            &accessoryConfiguration.state.keypadAttemptsInLastHour.numAttempts);

    // Reached max failed attempts allowed in the last hour
    if (accessoryConfiguration.state.keypadAttemptsInLastHour.numAttempts >= kAppAttemptsAllowedInHour) {
        keypadDisableTimeHour =
                HAPHour - (currentTime - accessoryConfiguration.state.keypadAttemptsInLastHour.timestamps[0]);
    }

    // Choose the longest time to disable
    keypadDisableTime =
            keypadDisableTimeHour > keypadDisableTimeTenMinutes ? keypadDisableTimeHour : keypadDisableTimeTenMinutes;

    if (keypadDisableTime > 0) {
        // Only disable keypad and notify if not already disabled
        if (accessoryConfiguration.state.active != kHAPCharacteristicValue_Active_Inactive) {
            accessoryConfiguration.state.active = kHAPCharacteristicValue_Active_Inactive;
            HAPAccessoryServerRaiseEvent(
                    accessoryConfiguration.server, &accessCodeActiveCharacteristic, &accessCodeService, &accessory);
            SaveAccessoryState();
        }

        // Disable the keypad, it can be re-enabled once timer expires
        EnableKeypadDisableTimer(keypadDisableTime);
    } else {
        // Keypad is disabled but should not be
        if (accessoryConfiguration.state.active == kHAPCharacteristicValue_Active_Inactive) {
            accessoryConfiguration.state.active = kHAPCharacteristicValue_Active_Active;
            HAPAccessoryServerRaiseEvent(
                    accessoryConfiguration.server, &accessCodeActiveCharacteristic, &accessCodeService, &accessory);
            SaveAccessoryState();
        }
    }
}
#endif // (HAVE_ACCESS_CODE == 1)

//----------------------------------------------------------------------------------------------------------------------

void AccessoryNotification(
        const HAPAccessory* accessory,
        const HAPService* service,
        const HAPCharacteristic* characteristic,
        void* ctx HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "Accessory Notification");

    HAPAccessoryServerRaiseEvent(accessoryConfiguration.server, characteristic, service, accessory);
}

void AppCreate(HAPAccessoryServer* server, HAPPlatformKeyValueStoreRef keyValueStore) {
    HAPPrecondition(server);
    HAPPrecondition(keyValueStore);

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    fix_firmware_version_for_HAP();

    HAPRawBufferZero(&accessoryConfiguration, sizeof accessoryConfiguration);
    accessoryConfiguration.server = server;
    accessoryConfiguration.keyValueStore = keyValueStore;

#if HAP_APP_USES_AAD
    accessoryConfiguration.useAAD = true;
#else
    accessoryConfiguration.useAAD = false;
#endif

    ConfigureIO();

    LoadAccessoryState();

    switch (accessoryConfiguration.state.targetState) {
        case kHAPCharacteristicValue_LockTargetState_Secured: {
            DeviceEnableLED(accessoryConfiguration.device.lockStateLedPin);
            break;
        }
        case kHAPCharacteristicValue_LockTargetState_Unsecured: {
            DeviceDisableLED(accessoryConfiguration.device.lockStateLedPin);
            break;
        }
    }

#if (HAVE_ACCESS_CODE == 1)
    // If keypad was previously disabled, continue the timer to determine when to re-enable
    HAPTime currentTime = HAPPlatformClockGetCurrent();
    SetKeypadStatus(currentTime);

    switch (accessoryConfiguration.state.active) {
        case kHAPCharacteristicValue_Active_Active: {
            DeviceEnableLED(accessoryConfiguration.device.activeLedPin);
            break;
        }
        case kHAPCharacteristicValue_Active_Inactive: {
            DeviceDisableLED(accessoryConfiguration.device.activeLedPin);
            break;
        }
    }
#endif

#if (HAP_APP_USES_HDS == 1)
    // Initialize HomeKit Data Stream dispatcher.
    HAPDataStreamDispatcherCreate(
            server,
            &dataStreamDispatcher,
            &(const HAPDataStreamDispatcherOptions) { .storage = &dataStreamDispatcherStorage });
#endif

#if (HAVE_FIRMWARE_UPDATE == 1)
    UARPInitialize(accessoryConfiguration.server, &accessory);

    FirmwareUpdateOptions fwupOptions = { 0 };
#if (HAP_TESTING == 1)
    fwupOptions.persistStaging = server->firmwareUpdate.persistStaging;
#endif
    FirmwareUpdateInitialize(
            accessoryConfiguration.server, &accessory, accessoryConfiguration.keyValueStore, &fwupOptions);
#endif // HAVE_FIRMWARE_UPDATE

    pm_qspi_nor_init();
    pm_qspi_nor_sleep();

#if (HAVE_NFC_ACCESS == 1)
    // For firmware updates where the previous version did not support NFC Access service, this is to ensure that all
    // HAP pairings LTPK are added to the issuer key list. Otherwise, this is to verify that previously added
    // HAP pairings LTPK have already been added to the issuer key list.
    HAPError err =
            HAPExportControllerPairings(accessoryConfiguration.keyValueStore, CachePairingEnumerateCallback, NULL);
    if (err) {
        HAPAssert(err == kHAPError_Unknown);
    }
#endif
}

void AppRelease(HAPAccessoryServer* _Nonnull server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
    UnconfigureIO();
}

void AppAccessoryServerStart(void) {
    HAPAccessoryServerStart(accessoryConfiguration.server, &accessory);
#if (HAVE_LOCK_ENC == 1)
    accessoryConfiguration.state.contextData.bootTime = HAPPlatformClockGetCurrent();
    accessoryConfiguration.state.contextData.isContextPresent = false;
#endif
#if (HAVE_FIRMWARE_UPDATE == 1)
    FirmwareUpdateStart(accessoryConfiguration.server, &accessory);
#endif // HAVE_FIRMWARE_UPDATE
}

//----------------------------------------------------------------------------------------------------------------------

void AppHandleAccessoryServerStateUpdate(HAPAccessoryServer* server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
}

void AppHandleFactoryReset(HAPAccessoryServer* server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
#if (HAVE_ACCESS_CODE == 1)
    // Purge access code key store domain
    HAPError accessCodeErr = HAPPlatformKeyValueStorePurgeDomain(
            accessoryConfiguration.keyValueStore, kHAPPlatformAccessCodeKeyStoreDomain);
    if (accessCodeErr) {
        HAPAssert(accessCodeErr == kHAPError_Unknown);
        HAPFatalError();
    }

    // Cache access code list and configuration state value
    accessCodeErr = AccessCodeRestart();
    if (accessCodeErr) {
        HAPFatalError();
    }
#endif

#if (HAVE_NFC_ACCESS == 1)
    HAPError nfcErr;
    nfcErr = HAPPlatformNfcAccessPurge();
    if (nfcErr) {
        HAPAssert(nfcErr == kHAPError_Unknown);
        HAPFatalError();
    }
#endif
}

void AppHandlePairingStateChange(
        HAPAccessoryServer* server HAP_UNUSED,
        HAPPairingStateChange state,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: state=%d", __func__, state);

    switch (state) {
        case kHAPPairingStateChange_Paired:
            break;
        case kHAPPairingStateChange_Unpaired: {
#if (HAVE_ACCESS_CODE == 1)
            // Last admin controller unpaired so purge everything
            HAPError accessCodeErr = HAPPlatformKeyValueStorePurgeDomain(
                    accessoryConfiguration.keyValueStore, kHAPPlatformAccessCodeKeyStoreDomain);
            if (accessCodeErr) {
                HAPAssert(accessCodeErr == kHAPError_Unknown);
                HAPFatalError();
            }

            // Cache access code list and configuration state value
            accessCodeErr = AccessCodeRestart();
            if (accessCodeErr) {
                HAPFatalError();
            }
#endif

#if (HAVE_NFC_ACCESS == 1)
            // Last admin controller unpaired so purge everything
            HAPError nfcErr = HAPPlatformNfcAccessPurge();
            if (nfcErr) {
                HAPAssert(nfcErr == kHAPError_Unknown);
                HAPFatalError();
            }
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
            DiagnosticsHandlePairingStateChange(
                    state, &accessoryConfiguration.state.diagnosticsSelectedModeState, &SaveAccessoryState);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
            break;
        }
    }
}

void AppHandleControllerPairingStateChange(
        HAPAccessoryServer* server HAP_UNUSED,
        HAPControllerPairingStateChange state,
        const HAPControllerPairingIdentifier* pairingIdentifier,
        const HAPControllerPublicKey* publicKey,
        void* _Nullable context HAP_UNUSED) {
    HAPPrecondition(pairingIdentifier);
    HAPPrecondition(publicKey);

    HAPLogInfo(&kHAPLog_Default, "%s: state=%d", __func__, state);

    switch (state) {
        case kHAPControllerPairingStateChange_Paired: {
#if (HAVE_NFC_ACCESS == 1)
            AddHapPairingLtpkAsIssuerKey(publicKey, NFC_ACCESS_ISSUER_KEY_CACHE_TYPE_HAP_PAIRING_ADD);
#endif
            break;
        }
        case kHAPControllerPairingStateChange_Unpaired: {
#if (HAVE_NFC_ACCESS == 1)
            RemoveHapPairingLtpkAsIssuerKey(publicKey);
#endif
            break;
        }
    }
}

const HAPAccessory* AppGetAccessoryInfo(void) {
    return &accessory;
}

const HAPAccessory* _Nonnull const* _Nullable AppGetBridgeAccessoryInfo(void) {
    return NULL;
}

#if (HAVE_ACCESS_CODE == 1)
/**
 * Handle configuration state changes by access code
 */
static void HandleConfigurationStateChangeByAccessCode(void) {
    HAPAccessoryServerRaiseEvent(
            accessoryConfiguration.server, &accessCodeConfigurationStateCharacteristic, &accessCodeService, &accessory);
}

#if (HAP_TESTING == 1)
/**
 * Handle lock state changes by access code
 */
static void HandleLockStateChangeByAccessCode(const AccessCodeLockStateChangeInfo* lockStateChangeInfo) {
    HAPPrecondition(lockStateChangeInfo);

    HAPLogInfo(&kHAPLog_Default, "%s: locked = %s", __func__, lockStateChangeInfo->locked ? "true" : "false");

#if (HAVE_LOCK_ENC == 1)
    accessoryConfiguration.state.contextData.source = STATE_CHANGE_SOURCE_KEYPAD;
    HAPRawBufferCopyBytes(
            accessoryConfiguration.state.contextData.contextIdentifier,
            &lockStateChangeInfo->accessCodeIdentifier,
            CONTEXT_IDENTIFIER_SIZE);
    UpdateLocalTimestamps();
    accessoryConfiguration.state.contextData.isContextPresent = true;
    PrintContextData();
#endif

    if (lockStateChangeInfo->locked) {
        SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Secured);
    } else {
        SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Unsecured);
    }
}

void ValidateAccessCode(char* accessCode) {
    HAPPrecondition(accessCode);

    if (accessoryConfiguration.state.active == kHAPCharacteristicValue_Active_Inactive) {
        HAPLogInfo(&kHAPLog_Default, "%s: Keypad is disabled", __func__);
        return;
    }

    HAPLogInfo(&kHAPLog_Default, "%s: Validating access code %s", __func__, accessCode);

    uint32_t accessCodeIdentifier;
    if (AccessCodeLookUp(accessCode, &accessCodeIdentifier)) {
        HAPLogInfo(&kHAPLog_Default, "%s: Access code is valid, toggling lock", __func__);

        AccessCodeLockStateChangeInfo lockStateChangeInfo = {
            .accessCodeIdentifier = accessCodeIdentifier,
        };

        switch (accessoryConfiguration.state.currentState) {
            case kHAPCharacteristicValue_LockCurrentState_Unsecured:
                lockStateChangeInfo.locked = true;
                break;
            case kHAPCharacteristicValue_LockCurrentState_Secured:
                lockStateChangeInfo.locked = false;
                break;
            default:
                lockStateChangeInfo.locked = true;
                break;
        }

        HandleLockStateChangeByAccessCode(&lockStateChangeInfo);
    } else {
        HAPLogInfo(&kHAPLog_Default, "%s: Access code is invalid", __func__);

        // Log the failed attempt
        HAPTime currentTime = HAPPlatformClockGetCurrent();
        LogKeypadAttempt(currentTime);

        // If needed, update the keypad status to disabled
        SetKeypadStatus(currentTime);
    }
}
#endif
#endif

#if (HAVE_NFC_ACCESS == 1)
/**
 * Handle configuration state changes by NFC
 */
static void HandleConfigurationStateChangeByNfc(void) {
    HAPAccessoryServerRaiseEvent(
            accessoryConfiguration.server, &nfcAccessConfigurationStateCharacteristic, &nfcAccessService, &accessory);
}

/**
 * Handle lock state changes by NFC
 */
static void HandleLockStateChangeByNfc(NfcLockStateChangeInfo lockStateChangeInfo) {
    HAPLogInfo(&kHAPLog_Default, "%s: locked = %s", __func__, lockStateChangeInfo.locked ? "true" : "false");
#if (HAVE_LOCK_ENC == 1)
    accessoryConfiguration.state.contextData.source = STATE_CHANGE_SOURCE_NFC;
    UpdateNfcIdentifier(lockStateChangeInfo.issuerKeyIdentifier);
    UpdateLocalTimestamps();
    accessoryConfiguration.state.contextData.isContextPresent = true;
    PrintContextData();
#endif
    if (lockStateChangeInfo.locked) {
        SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Secured);
    } else {
        SetLockTargetState(kHAPCharacteristicValue_LockTargetState_Unsecured);
    }
}

#endif

#if (HAVE_DIAGNOSTICS_SERVICE == 1)
/**
 * Handle write request to the 'Selected Diagnostics Mode' characteristic of the Diagnostics service.
 */
HAP_RESULT_USE_CHECK
HAPError HandleSelectedDiagnosticsModesWrite(
        HAPAccessoryServer* server,
        const HAPUInt32CharacteristicWriteRequest* request,
        uint32_t value,
        void* _Nullable context HAP_UNUSED) {
    return HandleSelectedDiagnosticsModesWriteHelper(
            server,
            request,
            value,
            context,
            &accessoryConfiguration.state.diagnosticsSelectedModeState,
            &SaveAccessoryState);
}
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)

void AppInitialize(
        HAPAccessoryServerOptions* hapAccessoryServerOptions HAP_UNUSED,
        HAPPlatform* hapPlatform HAP_UNUSED,
        HAPAccessoryServerCallbacks* hapAccessoryServerCallbacks) {
    hapAccessoryServerCallbacks->handleControllerPairingStateChange = AppHandleControllerPairingStateChange;

#if (HAVE_ACCESS_CODE == 1)
    HAPError hapErr = AccessCodeCreate(
            hapPlatform->keyValueStore,
            kHAPPlatformAccessCodeKeyStoreDomain,
            HandleConfigurationStateChangeByAccessCode,
            hapAccessoryServerOptions);
    HAPAssert(!hapErr);
#endif

#if (HAVE_NFC_ACCESS == 1)
    HAPError err = HAPPlatformNfcAccessCreate(
            hapPlatform->keyValueStore,
            kHAPPlatformNfcAccessKeyStoreDomain,
            HandleConfigurationStateChangeByNfc,
            HandleLockStateChangeByNfc);
    HAPAssert(!err);

    static uint8_t nfcAccessResponseBuffer[kAppNfcAccessResponseBufferBytes];
    static HAPNfcAccessResponseStorage nfcAccessResponseStorage = { .bytes = nfcAccessResponseBuffer,
                                                                    .maxBytes = sizeof nfcAccessResponseBuffer };

    hapAccessoryServerOptions->nfcAccess.responseStorage = &nfcAccessResponseStorage;
#endif

#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    InitializeDiagnostics(&accessoryConfiguration.state.diagnosticsSelectedModeState, &accessory, hapPlatform);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}

void AppDeinitialize(void) {
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    DeinitializeDiagnostics(&accessory);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}

#if (HAVE_LOCK_ENC == 1)
HAPError HandleLockReadContextData(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPCharacteristic* characteristic HAP_UNUSED,
        HAPTLVWriter* _Nullable* _Nullable outWriter,
        void* _Nullable context HAP_UNUSED) {
    HAPTLVWriter* writer = &contextTLVWriter;
    HAPError err = kHAPError_None;

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    PrintContextData();

    HAPRawBufferZero(contextDataTLVBuffer, CONTEXT_TLV_MAX_SIZE);
    HAPTLVWriterCreate(writer, contextDataTLVBuffer, CONTEXT_TLV_MAX_SIZE);
    if (accessoryConfiguration.state.contextData.isContextPresent) {
        HAPTLVWriterCreate(writer, contextDataTLVBuffer, CONTEXT_TLV_MAX_SIZE);

        err = HAPTLVWriterAppend(
                writer,
                &(const HAPTLV) { .type = kHAPCharacteristicTLVType_LockContextIdentifier,
                                  .value = { .bytes = &accessoryConfiguration.state.contextData.contextIdentifier,
                                             .numBytes = CONTEXT_IDENTIFIER_SIZE } });
        if (err != kHAPError_None) {
            return err;
        }

        err = HAPTLVWriterAppend(
                writer,
                &(const HAPTLV) { .type = kHAPCharacteristicTLVType_LockContextSource,
                                  .value = { .bytes = &accessoryConfiguration.state.contextData.source,
                                             .numBytes = sizeof(uint8_t) } });
        if (err != kHAPError_None) {
            return err;
        }

        err = HAPTLVWriterAppend(
                writer,
                &(const HAPTLV) { .type = kHAPCharacteristicTLVType_LockContextTimestamp,
                                  .value = { .bytes = &accessoryConfiguration.state.contextData.contextTimestamp,
                                             .numBytes = sizeof(uint32_t) } });
        if (err != kHAPError_None) {
            return err;
        }
    }
    *outWriter = writer;
    return err;
}

HAPError HandleLockReadContextDataCompact(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPCharacteristic* characteristic HAP_UNUSED,
        uint8_t* buffer,
        size_t bufferSize,
        void* _Nullable context HAP_UNUSED) {

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    PrintContextData();
    // TOTAL SIZE = 6
    // 1 byte to indicate context is present,
    // 4 bytes for ctx identifier,
    // 1 byte for source
    if (bufferSize >= 6) {
        if (accessoryConfiguration.state.contextData.isContextPresent == true) {
            int i = 0;
            buffer[i++] = (uint8_t) 1;
            for (int j = 0; j < CONTEXT_IDENTIFIER_SIZE; j++) {
                buffer[i++] = accessoryConfiguration.state.contextData.contextIdentifier[j];
            }
            buffer[i++] = accessoryConfiguration.state.contextData.source;
        } else {
            HAPRawBufferZero(buffer, bufferSize);
        }
    }
    return kHAPError_None;
}
#endif // (HAVE_LOCK_ENC == 1)
