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
#include <zephyr/kernel.h>

#include "HAPPlatformMFiHWAuth+Init.h"
LOG_MODULE_REGISTER(pal_hw_auth, CONFIG_PAL_HW_AUTH_LOG_LEVEL);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)

#include <stdio.h>
#include <zephyr/device.h>
#include "nrf_gpio.h"
#include <zephyr/drivers/i2c.h>

#define I2C_DEV DT_LABEL(DT_NODELABEL(i2c0))


#endif

void HAPPlatformMFiHWAuthCreate(HAPPlatformMFiHWAuthRef mfiHWAuth, const HAPPlatformMFiHWAuthOptions* options) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(options);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    // Get configuration.
    mfiHWAuth->vccPin = options->vccPin;
    mfiHWAuth->sclPin = options->sclPin;
    mfiHWAuth->sdaPin = options->sdaPin;

    HAPPrecondition(options->i2cAddress == 0x10 || options->i2cAddress == 0x11);
    mfiHWAuth->i2cAddress = options->i2cAddress;

    // Configure pins.
    nrf_gpio_pin_set(mfiHWAuth->vccPin);
    nrf_gpio_cfg(
            mfiHWAuth->vccPin,
            NRF_GPIO_PIN_DIR_OUTPUT,
            NRF_GPIO_PIN_INPUT_DISCONNECT,
            NRF_GPIO_PIN_NOPULL,
            NRF_GPIO_PIN_H0H1,
            NRF_GPIO_PIN_NOSENSE);

    // Initialize I2C module.
    mfiHWAuth->twi_dev = device_get_binding(I2C_DEV);
    if (!mfiHWAuth->twi_dev) {
        LOG_INF("I2C: Device driver not found.");
        HAPFatalError();
    }

    // Enter power-saving.
    mfiHWAuth->enabled = false;
    mfiHWAuth->sclConfiguration = NRF_GPIO->PIN_CNF[mfiHWAuth->sclPin];
    mfiHWAuth->sdaConfiguration = NRF_GPIO->PIN_CNF[mfiHWAuth->sdaPin];
    nrf_gpio_cfg_output(mfiHWAuth->sclPin);
    nrf_gpio_pin_clear(mfiHWAuth->sclPin);
    nrf_gpio_cfg_output(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->vccPin);

    // Initialization complete.
    mfiHWAuth->initialized = true;
#endif
}

void HAPPlatformMFiHWAuthRelease(HAPPlatformMFiHWAuthRef mfiHWAuth) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    // Disable and deinitialize I2C module.
    if (mfiHWAuth->enabled) {
        // TODO: Handle TWI unregister: NCSDK-3213
        mfiHWAuth->enabled = false;
    }

    // Enter power-saving.
    nrf_gpio_cfg_output(mfiHWAuth->sclPin);
    nrf_gpio_pin_clear(mfiHWAuth->sclPin);
    nrf_gpio_cfg_output(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->vccPin);

    mfiHWAuth->initialized = false;
#endif
}

HAP_RESULT_USE_CHECK
bool HAPPlatformMFiHWAuthIsPoweredOn(HAPPlatformMFiHWAuthRef mfiHWAuth) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    return mfiHWAuth->enabled;
#else
    return false;
#endif
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformMFiHWAuthPowerOn(HAPPlatformMFiHWAuthRef mfiHWAuth) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);
    HAPPrecondition(!mfiHWAuth->enabled);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    NRF_GPIO->PIN_CNF[mfiHWAuth->sclPin] = mfiHWAuth->sclConfiguration;
    NRF_GPIO->PIN_CNF[mfiHWAuth->sdaPin] = mfiHWAuth->sdaConfiguration;
    nrf_gpio_pin_set(mfiHWAuth->vccPin);
    k_sleep(K_MSEC(10)); // 10ms startup time
    mfiHWAuth->enabled = true;
    return kHAPError_None;
#endif

    return kHAPError_Unknown;
}

void HAPPlatformMFiHWAuthPowerOff(HAPPlatformMFiHWAuthRef mfiHWAuth) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);
    HAPPrecondition(mfiHWAuth->enabled);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    mfiHWAuth->sclConfiguration = NRF_GPIO->PIN_CNF[mfiHWAuth->sclPin];
    mfiHWAuth->sdaConfiguration = NRF_GPIO->PIN_CNF[mfiHWAuth->sdaPin];
    nrf_gpio_cfg_output(mfiHWAuth->sclPin);
    nrf_gpio_pin_clear(mfiHWAuth->sclPin);
    nrf_gpio_cfg_output(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->sdaPin);
    nrf_gpio_pin_clear(mfiHWAuth->vccPin);
    mfiHWAuth->enabled = false;
#endif
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformMFiHWAuthWrite(HAPPlatformMFiHWAuthRef mfiHWAuth, const void* bytes, size_t numBytes) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);
    HAPPrecondition(mfiHWAuth->enabled);
    HAPPrecondition(bytes);
    HAPPrecondition(numBytes >= 1 && numBytes <= 128);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    LOG_HEXDUMP_DBG(bytes, numBytes, "MFi >");
    for (int repeat = 0; repeat < 1000; repeat++) {

        int e = i2c_write(mfiHWAuth->twi_dev, bytes, numBytes, mfiHWAuth->i2cAddress);
        if (e) {
            k_sleep(K_USEC(500));
            continue;
        }
        LOG_DBG("MFi write complete.");
        return kHAPError_None;
    }
    LOG_INF("i2c_write failed too often. Giving up.");
#endif

    return kHAPError_Unknown;
}

HAP_RESULT_USE_CHECK
HAPError HAPPlatformMFiHWAuthRead(
        HAPPlatformMFiHWAuthRef mfiHWAuth,
        uint8_t registerAddress,
        void* bytes,
        size_t numBytes) {
    HAPPrecondition(mfiHWAuth);
    HAPPrecondition(mfiHWAuth->initialized);
    HAPPrecondition(mfiHWAuth->enabled);
    HAPPrecondition(bytes);
    HAPPrecondition(numBytes >= 1 && numBytes <= 128);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_MFI_HW_AUTH)
    LOG_DBG("MFi read 0x%02x.", registerAddress);

    struct i2c_msg msg;
    msg.buf = &registerAddress;
    msg.len = 1U;
    msg.flags = I2C_MSG_WRITE;
    int repeat;
    for (repeat = 0; repeat < 1000; repeat++)
    {
        int e = i2c_transfer(mfiHWAuth->twi_dev, &msg, 1, mfiHWAuth->i2cAddress);
        if (e) {
            k_sleep(K_USEC(500));
            continue;
        }
        break;
      }
      for (; repeat < 1000; repeat++)
      {
        int e = i2c_read(mfiHWAuth->twi_dev, bytes, numBytes, mfiHWAuth->i2cAddress);
        if (e) {
            k_sleep(K_USEC(500));
            continue;
        }
        LOG_DBG("MFi < %02x", registerAddress);
        LOG_HEXDUMP_DBG(bytes, numBytes, "");
        return kHAPError_None;
    }
    LOG_INF("i2c_read failed too often. Giving up.");
#endif

    return kHAPError_Unknown;
}
