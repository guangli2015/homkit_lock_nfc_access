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

// The most basic HomeKit example: an accessory that represents a light bulb that
// only supports switching the light on and off.
//
// This implementation can work for upto two LEDs and upto 4 buttons.
//
// To enable user interaction, buttons and LEDs on the development board are used:
//
// - LED 1 is used to simulate the light bulb.
// - LED 2, if available, is switched on during identify.
//
// - Button 1 or signal `SIGUSR1` to clear pairings.
// - Button 2 or signal `SIGUSR2` to trigger a factory reset.
// - Button 3 or Signal `SIGTERM` to trigger pairing mode.
// - Button 4 or Signal `SIGQUIT` to toggle the light bulb state between on and off.
//
// The code consists of multiple parts:
//
//   1. The definition of the accessory configuration and its internal state.
//
//   2. Helper functions to load and save the state of the accessory.
//
//   3. The setup of the accessory HomeKit and the bridged accessories attribute database.
//
//   4. Device specific configuration, callbacks and LED switches.
//
//   5. Signal handlers.
//
//   6. The callbacks that implement the actual behavior of the accessory. If the
//      accessory state has changed, the corresponding device functions are called.
//
//   7. The initialization of the accessory state.
//
//   8. A callback that gets called when the server state is updated.
#include <string.h>

#include "HAP.h"

#include "HAPPlatform+Init.h"

#include "AccessoryInformationServiceDB.h"
#include "App.h"
#include "AppBase.h"
#include "AppLED.h"
#if (HAP_TESTING == 1)
#include "AppUserInput.h"
#endif
#include "DB.h"
#if (HAVE_ADAPTIVE_LIGHT == 1)
#include "AdaptiveLight.h"
#endif
#if (HAVE_THREAD == 1)
#include "ThreadManagementServiceDB.h"
#endif
#if (HAP_APP_USES_HDS == 1)
#include "DataStreamTransportManagementServiceDB.h"
#endif
#if (HAVE_FIRMWARE_UPDATE == 1)
#include "FirmwareUpdate.h"
#include "FirmwareUpdateServiceDB.h"
#endif
#if (HAVE_ACCESSORY_REACHABILITY == 1)
#include "AccessoryRuntimeInformationServiceDB.h"
#endif
#if (HAVE_WIFI_RECONFIGURATION == 1)
#include "WifiTransportServiceDB.h"
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    AppLEDIdentifier lightBulbPin;
    AppLEDIdentifier identifyPin;
} Device;

/**
 * Global accessory configuration.
 */
typedef struct {
    struct {
        bool lightBulbOn;
        int32_t brightness;
        float hue;
        float saturation;
        uint32_t colorTemperature;
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
        uint32_t diagnosticsSelectedModeState;
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
    } state;
    struct {
        int32_t brightness;
        uint32_t colorTemperature;
    } prevNotificationValue;
    Device device;
    HAPPlatformTimerRef identifyTimer;
    HAPAccessoryServer* server;
    HAPPlatformKeyValueStoreRef keyValueStore;
} AccessoryConfiguration;

static AccessoryConfiguration accessoryConfiguration;

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
static HAPDataSendDataStreamProtocolContext dataSendDataStreamProtocolContexts[kApp_NumDataStreams];
static HAPDataSendDataStreamProtocolListener dataSendDataStreamProtocolListeners[kApp_NumDataStreams];
static HAPDataSendDataStreamProtocol
        dataSendDataStreamProtocol = { .base = &kHAPDataSendDataStreamProtocol_Base,
                                       .storage = { .numDataStreams = kApp_NumDataStreams,
                                                    .protocolContexts = dataSendDataStreamProtocolContexts,
                                                    .listeners = dataSendDataStreamProtocolListeners },
                                       .callbacks = {
                                               .numStreamAvailableCallbacks = 1,
                                               .streamAvailableCallbacks = dataSendDataStreamProtocolAvailableCallbacks,
                                       },
};
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)

static HAPDataStreamDescriptor dataStreamDescriptors[kApp_NumDataStreams];
static HAPDataStreamDispatcher dataStreamDispatcher;
const HAPDataStreamDispatcherStorage dataStreamDispatcherStorage = {
    .numDataStreams = kApp_NumDataStreams,
    .dataStreamDescriptors = dataStreamDescriptors,
    .dataStreamProtocols =
            (HAPDataStreamProtocol* const[]) {
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
                    &dataSendDataStreamProtocol,
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
#if (HAP_APP_USES_HDS_STREAM == 1)
                    &streamDataStreamProtocol,
#endif
                    NULL,
            },
};
#endif // HAP_APP_USES_HDS

//----------------------------------------------------------------------------------------------------------------------

#if (HAVE_ADAPTIVE_LIGHT == 1)
static AdaptiveLightTransitionStorage adaptiveLightStorage;
#endif

/**
 * Initialize the default accessory state
 */
static void SetupDefaultAccessoryState(void) {
    HAPRawBufferZero(&accessoryConfiguration.state, sizeof accessoryConfiguration.state);
    accessoryConfiguration.state.brightness = 0;
    accessoryConfiguration.state.hue = 0.0F;
    accessoryConfiguration.state.saturation = 0.0F;
    accessoryConfiguration.state.colorTemperature = lightBulbColorTemperatureCharacteristic.constraints.minimumValue;
    accessoryConfiguration.prevNotificationValue.colorTemperature = accessoryConfiguration.state.colorTemperature;
    accessoryConfiguration.prevNotificationValue.brightness = accessoryConfiguration.state.brightness;
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    accessoryConfiguration.state.diagnosticsSelectedModeState = kHAPCharacteristicValue_SelectedDiagnosticsModes_None;
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}

//----------------------------------------------------------------------------------------------------------------------
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
    } else {
        // This is not saved in the state, so it needs to be intialized
        accessoryConfiguration.prevNotificationValue.colorTemperature = accessoryConfiguration.state.colorTemperature;
        accessoryConfiguration.prevNotificationValue.brightness = accessoryConfiguration.state.brightness;
    }
}

/**
 * Save the accessory state to persistent memory.
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
 * HomeKit accessory that provides the Light Bulb service.
 */
static HAPAccessory accessory = { .aid = 1,
                                  .category = kHAPAccessoryCategory_Lighting,
                                  .name = "Nordic Light Bulb",
                                  .productData = "b3e691cc3020f889",
                                  .manufacturer = "Nordic Semiconductor",
                                  .model = "LightBulb1,1",
                                  .serialNumber = "000000001001",
                                  .firmwareVersion = firmware_version,
                                  .hardwareVersion = "1.0.0",
                                  .services = (const HAPService* const[]) { &accessoryInformationService,
                                                                            &hapProtocolInformationService,
                                                                            &pairingService,
#if (HAVE_WIFI_RECONFIGURATION == 1)
                                                                            &wiFiTransportService,
#endif
                                                                            &lightBulbService,
#if (HAP_APP_USES_HDS == 1)
                                                                            &dataStreamTransportManagementService,
#endif
#if (HAVE_FIRMWARE_UPDATE == 1)
                                                                            &firmwareUpdateService,
#endif
#if (HAVE_THREAD == 1)
                                                                            &threadManagementService,
#endif
#if (HAVE_ACCESSORY_REACHABILITY == 1)
                                                                            &accessoryRuntimeInformationService,
#endif
#if (HAP_TESTING == 1)
                                                                            &debugService,
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
                                                                            &diagnosticsService,
#endif
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

/**
 * Turn the light bulb on.
 */
static void TurnOnLightBulb(void) {
    DeviceEnableLED(accessoryConfiguration.device.lightBulbPin);
}

/**
 * Turn the light bulb off.
 */
static void TurnOffLightBulb(void) {
    DeviceDisableLED(accessoryConfiguration.device.lightBulbPin);
}

#if (HAP_TESTING == 1)

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
            ToggleLightBulbState();
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
 * Toggle the light bulb.
 */
void ToggleLightBulbState(void) {
    accessoryConfiguration.state.lightBulbOn = !accessoryConfiguration.state.lightBulbOn;

    HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, accessoryConfiguration.state.lightBulbOn ? "true" : "false");

    if (accessoryConfiguration.state.lightBulbOn) {
        TurnOnLightBulb();
    } else {
        TurnOffLightBulb();
    }

    SaveAccessoryState();

    HAPAccessoryServerRaiseEvent(
            accessoryConfiguration.server, &lightBulbOnCharacteristic, &lightBulbService, &accessory);
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
    accessoryConfiguration.device.lightBulbPin = kAppLEDIdentifier_1;
    accessoryConfiguration.device.identifyPin = kAppLEDIdentifier_2;

    // Initialize LED driver
    AppLEDInit();
}

//----------------------------------------------------------------------------------------------------------------------

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

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbOnRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPBoolCharacteristicReadRequest* request HAP_UNUSED,
        bool* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.lightBulbOn;
    HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, *value ? "true" : "false");
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbOnWrite(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPBoolCharacteristicWriteRequest* request HAP_UNUSED,
        bool value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %s", __func__, value ? "true" : "false");
    if (accessoryConfiguration.state.lightBulbOn != value) {
        accessoryConfiguration.state.lightBulbOn = value;

        SaveAccessoryState();

        if (value) {
            TurnOnLightBulb();
        } else {
            TurnOffLightBulb();
        }

        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbHueRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPFloatCharacteristicReadRequest* request HAP_UNUSED,
        float* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.hue;
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, *value);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbHueWrite(
        HAPAccessoryServer* server,
        const HAPFloatCharacteristicWriteRequest* request,
        float value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, value);
    if (accessoryConfiguration.state.hue != value) {
        accessoryConfiguration.state.hue = value;
        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbBrightnessRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPIntCharacteristicReadRequest* request HAP_UNUSED,
        int32_t* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.brightness;
    HAPLogInfo(&kHAPLog_Default, "%s: %d", __func__, (int) *value);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbBrightnessWrite(
        HAPAccessoryServer* server,
        const HAPIntCharacteristicWriteRequest* request,
        int32_t value,
        void* _Nullable context HAP_UNUSED) {
    HAPError err = kHAPError_None;
    HAPLogInfo(&kHAPLog_Default, "%s: %d", __func__, (int) value);
    if (accessoryConfiguration.state.brightness != value) {
        accessoryConfiguration.state.brightness = value;
        SaveAccessoryState();
        accessoryConfiguration.prevNotificationValue.brightness = value;
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
#if (HAVE_ADAPTIVE_LIGHT == 1)
    // Cancel ongoing Transition, if any.
    RemoveTransition(lightBulbBrightnessCharacteristic.iid);
    UpdateDerivedTransitionValue(lightBulbBrightnessCharacteristic.iid);
#endif
    return err;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbSaturationRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPFloatCharacteristicReadRequest* request HAP_UNUSED,
        float* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.saturation;
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, *value);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbSaturationWrite(
        HAPAccessoryServer* server,
        const HAPFloatCharacteristicWriteRequest* request,
        float value,
        void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "%s: %g", __func__, value);
    if (accessoryConfiguration.state.saturation != value) {
        accessoryConfiguration.state.saturation = value;
        SaveAccessoryState();
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbColorTemperatureRead(
        HAPAccessoryServer* server HAP_UNUSED,
        const HAPUInt32CharacteristicReadRequest* request HAP_UNUSED,
        uint32_t* value,
        void* _Nullable context HAP_UNUSED) {
    *value = accessoryConfiguration.state.colorTemperature;
    HAPLogInfo(&kHAPLog_Default, "%s: %u", __func__, (unsigned int) *value);
    return kHAPError_None;
}

HAP_RESULT_USE_CHECK
HAPError HandleLightBulbColorTemperatureWrite(
        HAPAccessoryServer* server,
        const HAPUInt32CharacteristicWriteRequest* request,
        uint32_t value,
        void* _Nullable context HAP_UNUSED) {
    HAPError err = kHAPError_None;
    HAPLogInfo(&kHAPLog_Default, "%s: %u", __func__, (unsigned int) value);
    if (accessoryConfiguration.state.colorTemperature != value) {
        accessoryConfiguration.state.colorTemperature = value;
        SaveAccessoryState();
        accessoryConfiguration.prevNotificationValue.colorTemperature = value;
        HAPAccessoryServerRaiseEvent(server, request->characteristic, request->service, request->accessory);
    }
#if (HAVE_ADAPTIVE_LIGHT == 1)
    // Cancel ongoing Transition, if any.
    // Ignore the return error code.
    RemoveTransition(lightBulbColorTemperatureCharacteristic.iid);
#endif
    return err;
}

#if (HAVE_ADAPTIVE_LIGHT == 1)

static void CharacteristicValueUpdate(uint64_t characteristicID, int32_t value, bool sendNotification) {
    // Evaluate whether or not the brightness characteristic has changed
    if (characteristicID == lightBulbBrightnessCharacteristic.iid) {
        if (value < lightBulbBrightnessCharacteristic.constraints.minimumValue) {
            HAPLogInfo(&kHAPLog_Default, "Calculated brightness %d is lower than minimum, capping to min", (int) value);
            value = lightBulbBrightnessCharacteristic.constraints.minimumValue;
        }
        if (value > lightBulbBrightnessCharacteristic.constraints.maximumValue) {
            HAPLogInfo(
                    &kHAPLog_Default, "Calculated brightness %d is higher than maximum, capping to max", (int) value);
            value = lightBulbBrightnessCharacteristic.constraints.maximumValue;
        }

        // Update accessory's brightness value.
        if (accessoryConfiguration.state.brightness != value) {
            HAPLogInfo(&kHAPLog_Default, "Brightness changed to %d", (int) value);
            accessoryConfiguration.state.brightness = value;
        }

        // Send a notification, if the brighntess value has changed since the last notification was sent.
        if (sendNotification) {
            // This covers the case when this function is called without a current change to the accessory's brightness
            // state, though it has had a brigthness change since a notification was last sent.
            if (accessoryConfiguration.prevNotificationValue.brightness != value) {
                HAPLogInfo(&kHAPLog_Default, "Sending notification that Brightness has changed to %d", (int) value);
                accessoryConfiguration.prevNotificationValue.brightness = value;
                HAPAccessoryServerRaiseEvent(
                        accessoryConfiguration.server,
                        &lightBulbBrightnessCharacteristic,
                        &lightBulbService,
                        &accessory);
            }
        }
    } // if (characteristicID == lightBulbBrightnessCharacteristic.iid)

    // Evaluate whether or not the Color Temperature characteristic has changed
    if (characteristicID == lightBulbColorTemperatureCharacteristic.iid) {
        if ((uint32_t) value < lightBulbColorTemperatureCharacteristic.constraints.minimumValue) {
            HAPLogInfo(&kHAPLog_Default, "Calculated Color Temp %d is lower than minimum, capping to min", (int) value);
            value = lightBulbColorTemperatureCharacteristic.constraints.minimumValue;
        }
        if ((uint32_t) value > lightBulbColorTemperatureCharacteristic.constraints.maximumValue) {
            HAPLogInfo(
                    &kHAPLog_Default, "Calculated Color Temp %d is higher than maximum, capping to max", (int) value);
            value = lightBulbColorTemperatureCharacteristic.constraints.maximumValue;
        }

        // Update accessory's Color Temperature value.
        if (accessoryConfiguration.state.colorTemperature != (uint32_t) value) {
            HAPLogInfo(&kHAPLog_Default, "Color Temp changed to %d", (int) value);
            accessoryConfiguration.state.colorTemperature = value;
        }

        // Send a notification, if the Color Temperature value has changed since the last notification was sent.
        if (sendNotification) {
            // This covers the case when this function is called without a current change to the accessory's color
            // temperature state, though it has had a color temperature change since a notification was last sent.
            if (accessoryConfiguration.prevNotificationValue.colorTemperature != (uint32_t) value) {
                HAPLogInfo(&kHAPLog_Default, "Sending notification that Color Temp has changed to %d", (int) value);
                accessoryConfiguration.prevNotificationValue.colorTemperature = value;
                HAPAccessoryServerRaiseEvent(
                        accessoryConfiguration.server,
                        &lightBulbColorTemperatureCharacteristic,
                        &lightBulbService,
                        &accessory);
            }
        }
    } // if (characteristicID == lightBulbColorTemperatureCharacteristic.iid)
}

static void CharacteristicValueRequest(uint64_t characteristicID, int32_t* value) {
    HAPPrecondition(value);
    if (characteristicID == lightBulbBrightnessCharacteristic.iid) {
        *value = accessoryConfiguration.state.brightness;
    }
    if (characteristicID == lightBulbColorTemperatureCharacteristic.iid) {
        *value = accessoryConfiguration.state.colorTemperature;
    }
}

static void TransitionExpiry() {
    HAPLogInfo(&kHAPLog_Default, "Number of active transitions changed");
    HAPAccessoryServerRaiseEvent(
            accessoryConfiguration.server, &lightBulbTransitionCountCharacteristic, &lightBulbService, &accessory);
}

static void PurgeAdaptiveLightKeyStore() {
    // Purge persistent state.
    HAPPlatformKeyValueStoreDomain domainsToPurge[] = { kAppKeyValueStoreDomain_AdaptiveLight };
    for (size_t i = 0; i < HAPArrayCount(domainsToPurge); i++) {
        HAPError err = HAPPlatformKeyValueStorePurgeDomain(accessoryConfiguration.keyValueStore, domainsToPurge[i]);
        if (err) {
            HAPAssert(err == kHAPError_Unknown);
            HAPFatalError();
        }
    }
    RemoveTransition(lightBulbBrightnessCharacteristic.iid);
    RemoveTransition(lightBulbColorTemperatureCharacteristic.iid);
}

static void InitializeAdaptiveLightWithConfig(
        uint32_t supportedTransitionTypesBrightness,
        uint32_t supportedTransitionTypesColorTemp) {
    HAPLogInfo(
            &kHAPLog_Default,
            "Using supported transition types for brightness as 0x%x and color temp as 0x%x",
            (unsigned int) supportedTransitionTypesBrightness,
            (unsigned int) supportedTransitionTypesColorTemp);

    // Currently we support only two transition types
    if (supportedTransitionTypesBrightness >> 2 || supportedTransitionTypesColorTemp >> 2) {
        HAPLogInfo(&kHAPLog_Default, "Invalid value for supported transition type");
        return;
    }

    AdaptiveLightSupportedTransition supportedTransitions[] = {
        { lightBulbBrightnessCharacteristic.iid, supportedTransitionTypesBrightness },
        { lightBulbColorTemperatureCharacteristic.iid, supportedTransitionTypesColorTemp },
    };

    AdaptiveLightCallbacks callbacks = {
        .handleCharacteristicValueUpdate = CharacteristicValueUpdate,
        .handleTransitionExpiry = TransitionExpiry,
        .handleCharacteristicValueRequest = CharacteristicValueRequest,
    };

    HAPError err = InitializeAdaptiveLightParameters(
            accessoryConfiguration.keyValueStore,
            &adaptiveLightStorage,
            supportedTransitions,
            HAPArrayCount(supportedTransitions),
            &callbacks);
    if (err) {
        HAPLogError(&kHAPLog_Default, "Failed to initialize Adaptive Light parameters");
    }
}

static void InitializeAdaptiveLight() {
    InitializeAdaptiveLightWithConfig(
            kHAPCharacteristicValue_SupportedTransitionConfiguration_SupportedTransition_Type_Linear,
            kHAPCharacteristicValue_SupportedTransitionConfiguration_SupportedTransition_Type_LinearDerived);
}

#if (HAP_TESTING == 1)
void SetSupportedAdaptiveLightConfig(uint32_t val) {
    PurgeAdaptiveLightKeyStore();

    // The lower-most byte indicates setting for brightness and the next byte is for Color temp.
    // Refer documentation for more details and examples.
    uint8_t brightnessConfigSetting = val & 0xff;
    uint8_t colorTempConfigSetting = (val >> 8) & 0xff;

    InitializeAdaptiveLightWithConfig(brightnessConfigSetting, colorTempConfigSetting);
}
#endif
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

//----------------------------------------------------------------------------------------------------------------------

void AppCreate(HAPAccessoryServer* server, HAPPlatformKeyValueStoreRef keyValueStore) {
    HAPPrecondition(server);
    HAPPrecondition(keyValueStore);

    HAPLogInfo(&kHAPLog_Default, "%s", __func__);
    fix_firmware_version_for_HAP();

    HAPRawBufferZero(&accessoryConfiguration, sizeof accessoryConfiguration);
    accessoryConfiguration.server = server;
    accessoryConfiguration.keyValueStore = keyValueStore;

    ConfigureIO();

    LoadAccessoryState();

    if (accessoryConfiguration.state.lightBulbOn) {
        TurnOnLightBulb();
    } else {
        TurnOffLightBulb();
    }

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

#if (HAVE_ADAPTIVE_LIGHT == 1)
    InitializeAdaptiveLight();
#endif
}

void AppRelease(HAPAccessoryServer* _Nonnull server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
    UnconfigureIO();
}

void AppAccessoryServerStart(void) {
    HAPAccessoryServerStart(accessoryConfiguration.server, &accessory);
#if (HAVE_FIRMWARE_UPDATE == 1)
    FirmwareUpdateStart(accessoryConfiguration.server, &accessory);
#endif // HAVE_FIRMWARE_UPDATE
}

//----------------------------------------------------------------------------------------------------------------------

void AppHandleAccessoryServerStateUpdate(HAPAccessoryServer* server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
}

void AppHandleFactoryReset(HAPAccessoryServer* server HAP_UNUSED, void* _Nullable context HAP_UNUSED) {
    HAPLogInfo(&kHAPLog_Default, "Resetting accessory configuration.");
#if (HAVE_ADAPTIVE_LIGHT == 1)
    PurgeAdaptiveLightKeyStore();
#endif
}

void AppHandlePairingStateChange(
        HAPAccessoryServer* server HAP_UNUSED,
        HAPPairingStateChange state,
        void* _Nullable context HAP_UNUSED) {
    switch (state) {
        case kHAPPairingStateChange_Unpaired: {
#if (HAVE_ADAPTIVE_LIGHT == 1)
            PurgeAdaptiveLightKeyStore();
#endif
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
            DiagnosticsHandlePairingStateChange(
                    state, &accessoryConfiguration.state.diagnosticsSelectedModeState, &SaveAccessoryState);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
            break;
        }
        default: {
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

void AppInitialize(
        HAPAccessoryServerOptions* hapAccessoryServerOptions HAP_UNUSED,
        HAPPlatform* hapPlatform HAP_UNUSED,
        HAPAccessoryServerCallbacks* hapAccessoryServerCallbacks HAP_UNUSED) {
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    InitializeDiagnostics(&accessoryConfiguration.state.diagnosticsSelectedModeState, &accessory, hapPlatform);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}

void AppDeinitialize(void) {
#if (HAVE_DIAGNOSTICS_SERVICE == 1)
    DeinitializeDiagnostics(&accessory);
#endif // (HAVE_DIAGNOSTICS_SERVICE == 1)
}
