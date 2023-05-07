/*
 * Copyright (c) 2020, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Use in source and binary forms, redistribution in binary form only, with
 * or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 2. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 3. This software, with or without modification, must only be used with a Nordic
 *    Semiconductor ASA integrated circuit.
 *
 * 4. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef CONFIG_HOMEKIT_NORDIC_DFU

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

#include "AppBase.h"
#include "AppLED.h"
#include "StatusLED.h"
#include "HAP+API.h"
#include "HAPBLEAccessoryServer.h"
#include "HAPBLESession.h"

#include "bluetooth_smp.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include "PowerManagment.h"

LOG_MODULE_REGISTER(nordic_dfu_ble, CONFIG_NORDIC_DFU_BLE_LOG_LEVEL);

#define DFU_PAUSED_TIMEOUT_MILLIS 1000u
static bool update_mode = false;
static bool is_dfu_paused = false;

bool NordicUpdate_is_update_mode() {
    return update_mode;
}

struct k_work_delayable dfu_not_started_handler;
struct k_work_delayable failed_to_finish_update;

static void dfu_paused_cb(struct k_timer *timer) {
    is_dfu_paused = true;
    dfuLedOnDFUMode();
    pm_qspi_nor_sleep();
}

K_TIMER_DEFINE(dfu_paused_timer, dfu_paused_cb, NULL);

static int32_t dfu_mode_cb(uint32_t event, int32_t rc, bool *abort_more,
                              void *data, size_t data_size) {
    switch (event) {
        case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
            if (update_mode == false) {
                // we did not enter into dfu mode before update! do it now.
                NordicUpdate_enter_update_mode();
            }

            LOG_INF("DFU Started");
            k_work_cancel_delayable(&dfu_not_started_handler);
            dfuLedOnDFUStarted();
            break;
        case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
            LOG_INF("DFU Stopped");
            pm_qspi_nor_wake_up();
            sys_reboot(SYS_REBOOT_COLD);
            break;
        case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
            LOG_INF("DFU Pending");
            break;
        case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK:
            if (is_dfu_paused) {
                dfuLedOnDFUStarted();
                is_dfu_paused = false;
                pm_qspi_nor_wake_up();
            }
            k_timer_start(&dfu_paused_timer, K_MSEC(DFU_PAUSED_TIMEOUT_MILLIS), K_MSEC(0));
            struct img_mgmt_upload_check *img_data = (struct img_mgmt_upload_check *)data;
            LOG_INF("Software update progress of image %u: %u B / %u B",
            (unsigned) (img_data->req->image),
            (unsigned) (img_data->req->off),
            (unsigned) (img_data->action->size));
            break;
        default:
            break;
    }
    return MGMT_ERR_EOK;
}

static struct mgmt_callback dfu_mode_mgmt_cb = {
    .callback = dfu_mode_cb,
    .event_id = MGMT_EVT_OP_IMG_MGMT_DFU_STARTED |
                MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED |
                MGMT_EVT_OP_IMG_MGMT_DFU_PENDING |
                MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK,
};

void NordicUpdate_init()
{
    LOG_DBG("Register DFU callback");
    mgmt_callback_register(&dfu_mode_mgmt_cb);
}

static void exit_dfu_mode(struct k_work* work) {
    LOG_ERR("DFU did not start or could not complete. Reset to exit dfu mode");
    pm_qspi_nor_wake_up();
    sys_reboot(SYS_REBOOT_COLD);
}

void NordicUpdate_enter_update_mode(){
    if (update_mode) {
        LOG_DBG("Already in update mode");
        return;
    }

    LOG_INF("Enter update mode");
    update_mode = true;

    pm_qspi_nor_wake_up();

    k_work_init_delayable(&dfu_not_started_handler, exit_dfu_mode);
    k_work_reschedule(&dfu_not_started_handler, K_MINUTES(5));

    k_work_init_delayable(&failed_to_finish_update, exit_dfu_mode);
    k_work_reschedule(&failed_to_finish_update, K_MINUTES(60));

    HAPAccessoryServer* server = AppGetAccessoryServer();
    HAPAccessoryServerKeepBleOn(server, true);
    dfuLedOnDFUMode();
}

bool NordicUpdate_timer_should_be_blocked(HAPPlatformTimerRef* timer) {
    HAPAccessoryServer* server = AppGetAccessoryServer();
    HAPBLESession* bleSession = &server->ble.storage->session->_.ble;

    if (&bleSession->linkTimer == timer)
    {
        LOG_DBG("limit link timer = %p", timer);
        return true;
    }
    if (&server->unpairedStateTimer == timer)
    {
        LOG_DBG("limit unpairedStateTimer = %p", timer);
        return true;
    }

    if (&server->accessorySetup.nfcPairingModeTimer == timer)
    {
        LOG_DBG("limit nfcPairingModeTimer = %p", timer);
        return true;
    }

    return false;
}
#endif
