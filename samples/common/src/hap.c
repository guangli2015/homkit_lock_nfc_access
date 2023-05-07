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

#include <zephyr/kernel.h>
#include "ADK.h"
#include "HAPPlatformLog.h"
#include <zephyr/sys/reboot.h>
#include "PowerManagment.h"

#ifdef CONFIG_HAP_BT_ABSTRACT_LAYER
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include "hap_bt_proxy_init.h"
#endif

#define HAP_STACK_SIZE CONFIG_HAP_STACK_SIZE

#define HAP_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)

void hap_enable(void* arg1, void* arg2, void* arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

#ifdef CONFIG_HAP_BT_ABSTRACT_LAYER
    struct hap_bt_cb bt_api = { .enable = bt_enable,
                                .conn_cb_register = bt_conn_cb_register,
                                .adv_start = bt_le_adv_start,
                                .adv_stop = bt_le_adv_stop,
                                .set_name = bt_set_name,
                                .bt_id_create = bt_id_create};

    int ret = hap_bt_cb_register(&bt_api);

    if (ret) {
        HAPLogError(&kHAPLog_Default, "Cannot register bt abstraction layer api.");
        HAPFatalError();
    }
#endif
HAPLogInfo(&kHAPLog_Default, "lg************************************");
    // Start HomeKit.
    void* ctx = NULL;
    (void) AdkRunApplication(ctx);

    // Factory reset.
    HAPLogInfo(&kHAPLog_Default, "HAP Application stopped. Restarting.");
    pm_qspi_nor_wake_up();
    sys_reboot(SYS_REBOOT_COLD);
}

K_THREAD_DEFINE(hap, HAP_STACK_SIZE, hap_enable, NULL, NULL, NULL, HAP_PRIORITY, 0, 0);
