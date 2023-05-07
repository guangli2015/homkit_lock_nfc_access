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

#include "HAP+API.h"
#include "HAPPlatformBLEPeripheralManager+Init.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include "StatusLED.h"

LOG_MODULE_REGISTER(pal_platform_ble, CONFIG_PAL_PLATFORM_BLE_LOG_LEVEL);

#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
#include "hap_bt_proxy.h"
#endif

#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
/**
 * The BLE stack has been initialized. The stack cannot be disabled once enabled.
 */
static bool bleStackIsInitialized = false;

/**
 * BLE peripheral manager reference
 */
static HAPPlatformBLEPeripheralManager* blePeripheralManager;

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
        HAPPlatformBLEPeripheralManagerHandleBLEEvent(blePeripheralManager, conn, BLE_PLATFORM_CONN_PARAM_UPDATE_REQUEST);

        /* Accept the request */
        return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
        int err;

        HAPPlatformBLEPeripheralManagerHandleBLEEvent(blePeripheralManager, conn, BLE_PLATFORM_CONN_PARAM_UPDATE);

        const struct bt_le_conn_param param = {
                .interval_min = interval,
                .interval_max = interval,
                .latency = latency,
                .timeout = timeout
        };

        err = bt_conn_le_param_update(conn, &param);
        if (!err || (err == -EALREADY)) {
            LOG_INF("BLE latency %screased", latency ? "de" : "in");

            if (err == -EALREADY) {
                LOG_INF("Conn parameters were already updated");
            }
        } else {
            LOG_WRN("Failed to update conn parameters (err %d)", err);
        }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)\n", err);
        HAPFatalError();
        return;
    }

    HAPPlatformBLEPeripheralManagerHandleBLEEvent(blePeripheralManager, conn, BLE_PLATFORM_CONNECTED);
    bleLedOnBLEConnected();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x) check value in (zephyr/include/bluetooth/hci_err.h)", reason);
    HAPPlatformBLEPeripheralManagerHandleBLEEvent(blePeripheralManager, conn, BLE_PLATFORM_DISCONNECTED);
    bleLedOnBLEDisabled();
}

#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
static void security_changed(struct bt_conn *conn, bt_security_t level,	 enum bt_security_err err)
{
    if (!err) {
        LOG_INF("Security level was raised to %d\n", level);
    }
    else {
        LOG_INF("Security changed error = %d\n", (int) err);
    }
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
        char addr[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

        LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
        char addr[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

        LOG_INF("Pairing cancelled: %s\n", addr);
}
#endif

static struct bt_conn_cb conn_callbacks = {
        .connected        = connected,
        .disconnected     = disconnected,
        .le_param_req     = le_param_req,
        .le_param_updated = le_param_updated,
#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
        .security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
static struct bt_conn_auth_cb conn_auth_callbacks = {
        .passkey_display = auth_passkey_display,
        .passkey_entry = NULL,
        .cancel = auth_cancel,
};
#endif

#endif /* HAP_FEATURE_ENABLED(HAP_FEATURE_BLE) */


void HAPPlatformBLEInitialize(HAPPlatformBLEPeripheralManager* manager) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
    blePeripheralManager = manager;

    int err;

    // With multiprotocol support, BLE cannot be disabled till system reboot
    // and enabling BLE again will fail.
    if (bleStackIsInitialized) {
        return;
    }

#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
    err = hap_bt_enable(NULL);
#else
    err = bt_enable(NULL);
#endif

    if (!err || (-EALREADY == err)) {
#if defined(CONFIG_HAP_BT_ABSTRACT_LAYER)
        hap_bt_conn_cb_register(&conn_callbacks);
#else
        bt_conn_cb_register(&conn_callbacks);
#endif
#if defined(CONFIG_BT_SMP) || defined(CONFIG_BT_BREDR)
        bt_conn_auth_cb_register(&conn_auth_callbacks);
#endif

        LOG_INF("Bluetooth initialized\n");
    } else {
        LOG_ERR("BLE init failed with error code %d\n", err);
        HAPFatalError();
    }

    bleStackIsInitialized = true;

#else
    HAPAssertionFailure();
#endif
}

void HAPPlatformBLEDeinitialize(void) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
    // Need to clear BLE Peripheral Manager data in order to re-register UUID
    // next time BLE transport starts
    HAPPlatformBLEPeripheralManagerClear(blePeripheralManager);
#else
    HAPAssertionFailure();
#endif
}

bool HAPPlatformBLEStackIsEnabled(void) {
#if HAP_FEATURE_ENABLED(HAP_FEATURE_BLE)
    return bleStackIsInitialized;
#else
    return false;
#endif
}
