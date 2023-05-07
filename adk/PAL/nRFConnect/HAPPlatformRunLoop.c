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
#include <string.h>

#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformMutex+Init.h" // TODO: RG use zephyr mutex
#include "HAPPlatformMutex.h"
#include "HAPPlatformRunLoop+Init.h"
#include <zephyr/sys/reboot.h>
#include "PowerManagment.h"

LOG_MODULE_REGISTER(pal_run_loop, CONFIG_PAL_RUN_LOOP_LOG_LEVEL);

static HAPPlatformMutex runLoopMutex;
/*
 * HAP Platform message queue.
 */
K_MSGQ_DEFINE(happl_msgq, MSGQ_ELEMENT_SIZE, MSGQ_MAX_ELEMENTS, MSGQ_ALIGN);

/*
 * HAP Platform message heap for events context data.
 */
K_HEAP_DEFINE(happl_msg_heap, (MSGQ_MAX_ELEMENTS * MSGQ_MAX_CONTEXT_SIZE));

/**
 * State of the run loop.
 */
HAP_ENUM_BEGIN(uint8_t, HAPPlatformRunLoopState) {
    /**
     * The run loop is not yet started or finished stopping.
     */
    kHAPPlatformRunLoopState_Idle,

    /**
     * The run loop is running.
     */
    kHAPPlatformRunLoopState_Running,

    /**
     * The run loop is stopping.
     */
    kHAPPlatformRunLoopState_Stopping
} HAP_ENUM_END(uint8_t, HAPPlatformRunLoopState);

static HAPPlatformRunLoopState state;

static HAPPlatformKeyValueStoreRef keyValueStore;

static void HAPPlatformRunLoopRunDispatchEvent(
    RunLoopEvent *event)
{
    if (event->callback)
    {
        if (HAPPlatformMutexLock(&runLoopMutex) != kHAPError_None) {
            LOG_ERR("Can not lock mutex. Dropping event.");
            return;
        }
        event->callback(event->context, event->contextSize);

        if(event->context){
            k_heap_free(&happl_msg_heap, event->context);
        }

        if (HAPPlatformMutexUnlock(&runLoopMutex) != kHAPError_None) {
            LOG_ERR("Can not unlock mutex. Dropping event.");
            return;
        }
    }
    else
    {
        LOG_INF("Event received with no handler. Dropping event.");
    }
}

void HAPPlatformRunLoopCreate(
    const HAPPlatformRunLoopOptions *options)
{
    HAPPrecondition(options);
    HAPPrecondition(options->keyValueStore);

    keyValueStore = options->keyValueStore;

    const HAPPlatformMutexOptions mutex_options = { .mutexType = kHAPPlatformMutexType_ErrorCheck };
    HAPPlatformMutexInit(&runLoopMutex, &mutex_options);
}

void HAPPlatformRunLoopRun(void)
{
    HAPPrecondition(state == kHAPPlatformRunLoopState_Idle);

    LOG_INF("Entering run loop.");
    state = kHAPPlatformRunLoopState_Running;

    for (;;) {
        // Execute pending events.
        RunLoopEvent event = {};
        int ret = k_msgq_get(&happl_msgq, &event, K_MSEC(1000));

        while (!ret)
        {
            HAPPlatformRunLoopRunDispatchEvent(&event);
            ret = k_msgq_get(&happl_msgq, &event, K_NO_WAIT);
        }

        // Exit run loop if requested.
        if (state != kHAPPlatformRunLoopState_Running && !HAPPlatformKeyValueStoreIsBusy(keyValueStore)) {
            HAPAssert(state == kHAPPlatformRunLoopState_Stopping);

            LOG_INF("Exiting run loop.");
            state = kHAPPlatformRunLoopState_Idle;
            return;
        }
    }
}

void HAPPlatformRunLoopStop(void)
{
    if (state == kHAPPlatformRunLoopState_Running) {
        LOG_INF("Registering request to stop the run loop.");
        state = kHAPPlatformRunLoopState_Stopping;
    }
}

void HAPPlatformRunLoopRelease(void) {
    /* Do nothing */
    LOG_DBG("%s", __PRETTY_FUNCTION__);
    HAPPlatformDeregisterAllADKTimers();

#if CONFIG_HAP_REBOOT_ON_RUNLOOP_RELEASE
    pm_qspi_nor_wake_up();
    sys_reboot(SYS_REBOOT_COLD);
#endif/* CONFIG_HAP_REBOOT_ON_RUNLOOP_RELEASE */
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformRunLoopScheduleCallback(
    HAPPlatformRunLoopCallback callback,
    void *_Nullable context,
    size_t contextSize)
{
    HAPPrecondition(callback);
    HAPPrecondition(!contextSize || context);

    // Prepare callback data.
    void * callbackData = NULL;

    if (contextSize > MSGQ_MAX_CONTEXT_SIZE) {
        LOG_ERR("Contexts larger than %ul are not supported.", MSGQ_MAX_CONTEXT_SIZE);
        return kHAPError_OutOfResources;
    }

    if(contextSize) {
        callbackData = k_heap_alloc(&happl_msg_heap, contextSize, K_NO_WAIT);
        if(!callbackData) {
            LOG_ERR("No memory to allocate callback context on heap.");
            return kHAPError_OutOfResources;
        }

        void * ret = memcpy(callbackData, context, contextSize);
        if(ret != callbackData) {
            LOG_ERR("Can't copy context data.");
            return kHAPError_OutOfResources;
        }
    }

    // Create callback event.
    RunLoopEvent callbackEvent = {
        .callback = callback,
        .context = callbackData,
        .contextSize = contextSize,
    };

    // Schedule event.
    int e = k_msgq_put(&happl_msgq, &callbackEvent, K_NO_WAIT);
    if (e) {
        LOG_ERR("HAPPlatformRunLoopScheduleCallback failed: %d", e);
        return kHAPError_OutOfResources;
    }

    return kHAPError_None;
}
