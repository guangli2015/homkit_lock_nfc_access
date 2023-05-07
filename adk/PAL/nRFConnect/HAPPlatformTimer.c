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
#include <stdint.h>
#include <string.h>
#include "HAPPlatform.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>

LOG_MODULE_REGISTER(pal_timer, CONFIG_PAL_TIMER_LOG_LEVEL);

#define kTimerStorage_MaxTimers ((size_t) 32)
#define TIMER_NOT_USED          0x0

// TODO This is needed for Nordic DFU, so enable it when DFU is needed.
//      When new uses will be found, move to KConfig
#define TIMER_BLACKLIST CONFIG_HOMEKIT_NORDIC_DFU

#if TIMER_BLACKLIST
#include "bluetooth_smp.h"
#endif

typedef struct {
    /** Callback. NULL if timer inactive. */
    HAPPlatformTimerCallback _Nullable callback;

    /** The context parameter given to the HAPPlatformTimerRegister function. */
    void* _Nullable context;
} HAPPlatformTimer;

struct PAL_timer {
    sys_snode_t node;
    HAPPlatformTimerRef timer_id; // if not used TIMER_NOT_USED is set.
    HAPPlatformTimer HAPTimer;
    HAPTime deadline;
};

static void Initialize(void);
static void ClearTimer(struct PAL_timer* tim);
static void RescheduleTimer(void);
static struct PAL_timer* PlaceInRepo(HAPPlatformTimerCallback callback, void* _Nullable context, HAPTime deadline);
static bool GetNextTimerDeadline(HAPTime* next_deadline);

static void WorkHandler(struct k_work* work);
static void TimerHandlerISR(struct k_timer* dummy);

K_TIMER_DEFINE(kernel_isr_timer, TimerHandlerISR, NULL);
K_MUTEX_DEFINE(timer_repo_mutex);
static struct k_work work;

static sys_slist_t submitted_timers;
static struct PAL_timer timer_repo[kTimerStorage_MaxTimers]; // 24B * 32 = 768B

#if CONFIG_LOG
static void PrintStats();
#else
inline static void PrintStats() { /*NOOP*/ };
#endif

#if TIMER_BLACKLIST
/**
 * @brief This feature is used to temporary block execution of selected timers.
 *
 * Registering a callback to black list does not block it entirely.
 * During registration special checking function has to be provided.
 * When the timer expires, and its callback is in black list, this
 * `condition` function is used to determine if it should be blocked
 * (return true) or not ( return false)
 *
 */
#define BLACK_LIST_MAX_ELEMENT 3
static struct {
    // callback to block
    HAPPlatformTimerCallback callback;
    // called before execution callback to check if it should be blocked
    // if true, callback will be blocked
    bool (*condition)();
} black_list[BLACK_LIST_MAX_ELEMENT];
void AddTimerToBlackList(HAPPlatformTimerCallback callback, bool (*condition)());
#endif

// ///////////////////////////////////////////////////////////////////////////
HAP_RESULT_USE_CHECK
HAPError HAPPlatformTimerRegister(
        HAPPlatformTimerRef* timer,
        HAPTime deadline,
        HAPPlatformTimerCallback callback,
        void* _Nullable context) {
    HAPPrecondition(timer);
    HAPPrecondition(callback);
    Initialize();
    k_mutex_lock(&timer_repo_mutex, K_FOREVER);
    struct PAL_timer* timer_or_null = PlaceInRepo(callback, context, deadline);

#if TIMER_BLACKLIST
    if (NordicUpdate_timer_should_be_blocked(timer))
        AddTimerToBlackList(callback, NordicUpdate_is_update_mode);
#endif

    if (timer_or_null == NULL) {
        LOG_DBG("Cannot allocate more timers.");
        k_mutex_unlock(&timer_repo_mutex);
        return kHAPError_OutOfResources;
    }

    *timer = (HAPPlatformTimerRef) timer_or_null;
    sys_slist_append(&submitted_timers, &timer_or_null->node);

    LOG_DBG("timer = 0x%lx callback = %p, expires at timestamp %02llu:%02llu:%02llu.%03llu",
            (long unsigned int) *timer,
            callback,
            deadline / HAPHour,
            (deadline / HAPMinute) % 60,
            (deadline / HAPSecond) % 60,
            (deadline / HAPMillisecond) % 1000);

    if (deadline <= HAPPlatformClockGetCurrent()) {
        // do not use kernel irq timer to schedule immediate callbacks
        LOG_DBG("immediate timer");
        k_timer_stop(&kernel_isr_timer); // work handler will reshcedule after executing the callback, prevent running
                                         // twice if other timer expires in near future
        k_mutex_unlock(&timer_repo_mutex);
        k_work_submit(&work);
        return kHAPError_None;
    }

    RescheduleTimer();
    PrintStats();
    k_mutex_unlock(&timer_repo_mutex);
    return kHAPError_None;
}

void HAPPlatformTimerDeregister(HAPPlatformTimerRef timer) {
    HAPPrecondition(timer);

    Initialize();
    k_mutex_lock(&timer_repo_mutex, K_FOREVER);
    LOG_DBG("timer = 0x%lx", (long unsigned int) timer);

    struct PAL_timer* timer_handle = (struct PAL_timer*) timer;
    bool removed = sys_slist_find_and_remove(&submitted_timers, &timer_handle->node);
    if (removed) {
        ClearTimer(timer_handle);
    } else {
        LOG_ERR("Can not find timer in execution queue");
    }

    RescheduleTimer();

    k_mutex_unlock(&timer_repo_mutex);
}

// //////////////////////////////////////////////////////////////////////

static bool initialized = false;

/**
 * @brief Initialize timer module.
 *
 */
static void Initialize(void) {
    k_mutex_lock(&timer_repo_mutex, K_FOREVER);
    if (!initialized) {
        initialized = true;
        for (size_t i = 0; i < kTimerStorage_MaxTimers; ++i) {
            ClearTimer(&timer_repo[i]);
        }
        sys_slist_init(&submitted_timers);
        k_work_init(&work, WorkHandler);
    }
    k_mutex_unlock(&timer_repo_mutex);
}

void HAPPlatformDeregisterAllADKTimers(void) {
    k_mutex_lock(&timer_repo_mutex, K_FOREVER);
    initialized = false;
    for (size_t i = 0; i < kTimerStorage_MaxTimers; ++i) {
        ClearTimer(&timer_repo[i]);
    }
    sys_slist_init(&submitted_timers); // clear list of timers
    k_work_init(&work, WorkHandler);

    k_mutex_unlock(&timer_repo_mutex);
}

/**
 * @brief Clear timer node, to make it avaliable.
 *
 * @param tim timer to clear.
 */
static void ClearTimer(struct PAL_timer* tim) {
    tim->HAPTimer = (HAPPlatformTimer) { .callback = NULL, .context = NULL };
    tim->timer_id = TIMER_NOT_USED;
    tim->deadline = TIMER_NOT_USED;
}

/**
 * @brief calculate when next timer in list should expire, and adjust kernel timer.
 *
 */
static void RescheduleTimer(void) {
    k_timer_stop(&kernel_isr_timer);
    HAPTime next_deadline = UINT64_MAX;
    const HAPTime now = HAPPlatformClockGetCurrent();
    if (GetNextTimerDeadline(&next_deadline)) {
        const k_timeout_t delay = next_deadline >= now ? K_MSEC(next_deadline - now) : K_USEC(1);
        k_timer_start(&kernel_isr_timer, delay, K_NO_WAIT);
    } else {
        // there is no timer registered, next timer registration will schedule timer.
    }
}

/*
 *  @brief This function is loops through registered timers, and finds the next timer to execute.
 *
 *  @param next_deadline[out] write the deadline of the closest timer.
 *  @return true on success, false when the list of timers is empty
 */
static bool GetNextTimerDeadline(HAPTime* next_deadline) {
    const HAPTime now = HAPPlatformClockGetCurrent();
    HAPTime next = UINT64_MAX;
    struct PAL_timer* element = NULL;
    if (sys_slist_peek_head(&submitted_timers) == NULL) {
        return false; // empty list
    }
    SYS_SLIST_FOR_EACH_CONTAINER(&submitted_timers, element, node) {
        if (element->deadline < next) {
            next = element->deadline;
        }
        if (next <= now) {
            // found timer that should already be executed, break, and run it.
            break;
        }
    }
    *next_deadline = next;
    return true;
}

/**
 * @brief ISR callback for kernel timer
 *
 * @param dummy not used
 */
static void TimerHandlerISR(struct k_timer* dummy) {
    (void) dummy;
    k_work_submit(&work); // call WorkHandler in sysworkq
}

/**
 * @brief callback for sysworkq, running expired timers.
 *
 * @param dummy  not used
 */
static void WorkHandler(struct k_work* dummy) {
    (void) dummy;
    k_mutex_lock(&timer_repo_mutex, K_FOREVER);
    const HAPTime now = HAPPlatformClockGetCurrent();
    struct PAL_timer* timer_to_run = NULL;
    struct PAL_timer* element = NULL;
    SYS_SLIST_FOR_EACH_CONTAINER(&submitted_timers, element, node) {
        if (element->deadline <= now) {
            timer_to_run = element;
            break;
        }
    }

    if (timer_to_run == NULL) {
        RescheduleTimer();
        k_mutex_unlock(&timer_repo_mutex);
        return;
    }

    // clear callback from list and call it
    sys_slist_find_and_remove(&submitted_timers, &timer_to_run->node);
    const HAPPlatformTimerCallback callback = timer_to_run->HAPTimer.callback;
    void* context = timer_to_run->HAPTimer.context;
    const HAPPlatformTimerRef id = timer_to_run->timer_id;
    ClearTimer(timer_to_run);

    k_mutex_unlock(&timer_repo_mutex);
#if TIMER_BLACKLIST
    for (int i = 0; i < BLACK_LIST_MAX_ELEMENT; ++i) {
        if (black_list[i].callback == callback && black_list[i].condition()) {
            LOG_DBG("ignore call timer 0x%lx callback = %p", (long unsigned int) id, callback);
            k_work_submit(&work);
            return;
        }
    }
#endif
    LOG_DBG("start call timer 0x%lx callback = %p", (long unsigned int) id, callback);
    callback(id, context);
    LOG_DBG("end call timer 0x%lx callback = %p", (long unsigned int) id, callback);
    // call WorkHandler in sysworkq to check if other timer need execution (callback can register immediate timer, or
    // few immediate timers may be already in queue) - can not call WorkHandler directly, because it will lock sysworkq
    // (like adding immediate timer in the callback can cause infinite loop)
    k_work_submit(&work);
}

/**
 * @brief fill element of timer repo, in preperation for inserting to list.
 *
 * @param callback timer callback
 * @param context context of the timer
 * @param deadline time of execution
 * @return struct PAL_timer* timer object from repo
 */
static struct PAL_timer* PlaceInRepo(HAPPlatformTimerCallback callback, void* _Nullable context, HAPTime deadline) {
    size_t index = 0xffff;
    for (uint8_t i = 0; i < kTimerStorage_MaxTimers; ++i) {
        if (timer_repo[i].timer_id == TIMER_NOT_USED) {
            index = i;
            break;
        }
    }

    if (index < kTimerStorage_MaxTimers && timer_repo[index].timer_id == TIMER_NOT_USED) {
        timer_repo[index].timer_id = (HAPPlatformTimerRef) &timer_repo[index];
        timer_repo[index].HAPTimer = (HAPPlatformTimer) { .callback = callback, .context = context };
        timer_repo[index].deadline = deadline;
        return (struct PAL_timer*) timer_repo[index].timer_id;
    } else {
        return NULL;
    }
}

#if CONFIG_LOG
/**
 * @brief Print usage of the timer repo.
 *
 */
static void PrintStats() {
    static size_t max_usage = 0;
    size_t used = 0;
    for (uint8_t i = 0; i < kTimerStorage_MaxTimers; ++i) {
        if (timer_repo[i].timer_id != TIMER_NOT_USED) {
            used++;
        }
    }
    if (used > max_usage) {
        max_usage = used;
    }

    LOG_DBG("timers: %u / %u timers used, max usage %d",
            (unsigned int) used,
            (unsigned int) kTimerStorage_MaxTimers,
            (unsigned int) max_usage);
}
#endif

#if TIMER_BLACKLIST
void AddTimerToBlackList(HAPPlatformTimerCallback callback, bool (*condition)()) {
    if (callback == NULL || condition == NULL) {
        LOG_ERR("%s Invalid parameter! callback (%p) and condition (%p) has to be valid",
                __FUNCTION__,
                callback,
                condition);
        return;
    }
    for (int i = 0; i < BLACK_LIST_MAX_ELEMENT; ++i) {
        if (black_list[i].callback == NULL || black_list[i].callback == callback) {
            black_list[i].callback = callback;
            black_list[i].condition = condition;
            return;
        }
    }

    for (int i = 0; i < BLACK_LIST_MAX_ELEMENT; ++i) {
        LOG_ERR("%d: callback: %p", i, black_list[i].callback);
    }

    LOG_ERR("No more space in black list!");
}
#endif
