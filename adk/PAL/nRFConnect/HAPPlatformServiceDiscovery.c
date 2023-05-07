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

#include "HAPPlatform+Init.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pal_service_discovery, CONFIG_PAL_SERVICE_DISCOVERY_LOG_LEVEL);

#if HAP_FEATURE_ENABLED(HAP_FEATURE_THREAD)

#include "HAPPlatformServiceDiscovery+Init.h"

static ServiceDiscoveryCallback replyCallback = NULL;
static void* replyContext = NULL;

static void HandleServiceRegisterReply(
        DNSServiceRef service HAP_UNUSED,
        DNSServiceFlags flags HAP_UNUSED,
        DNSServiceErrorType errorCode,
        const char* name HAP_UNUSED,
        const char* regtype HAP_UNUSED,
        const char* domain HAP_UNUSED,
        void* context_ HAP_UNUSED) {
    if (errorCode != kDNSServiceErr_NoError) {
        LOG_ERR("Service discovery registration failed: %ld.", (long) errorCode);
    }
    if (replyCallback) {
        replyCallback(errorCode == kDNSServiceErr_NoError, replyContext);
    }
}
void HAPPlatformServiceDiscoveryCreate(
        HAPPlatformServiceDiscoveryRef serviceDiscovery,
        const HAPPlatformServiceDiscoveryOptions* options) {
    HAPPrecondition(serviceDiscovery);
    HAPPrecondition(options);

    LOG_DBG("Storage configuration: serviceDiscovery = %lu", (unsigned long) sizeof *serviceDiscovery);

    HAPRawBufferZero(serviceDiscovery, sizeof *serviceDiscovery);
    serviceDiscovery->dnsService = NULL;
}

void HAPPlatformServiceDiscoveryRegister(
        HAPPlatformServiceDiscoveryRef serviceDiscovery,
        const char* name,
        const char* protocol,
        HAPNetworkPort port,
        HAPPlatformServiceDiscoveryTXTRecord* txtRecords,
        size_t numTXTRecords) {

    HAPPrecondition(serviceDiscovery);
    HAPPrecondition(!serviceDiscovery->dnsService);
    HAPPrecondition(name);
    HAPPrecondition(protocol);
    HAPPrecondition(txtRecords);

    DNSServiceErrorType errorCode;

    uint32_t interfaceIndex = 0;

    LOG_DBG("name: \"%s\"", name);
    LOG_DBG("protocol: \"%s\"", protocol);
    LOG_DBG("port: %u", port);

    TXTRecordCreate(
        &serviceDiscovery->txtRecord,
        sizeof serviceDiscovery->txtRecordBuffer,
        &serviceDiscovery->txtRecordBuffer[0]);

    for (size_t i = 0; i < numTXTRecords; i++) {
        HAPPrecondition(!txtRecords[i].value.numBytes || txtRecords[i].value.bytes);
        HAPPrecondition(txtRecords[i].value.numBytes <= UINT8_MAX);
        if (txtRecords[i].value.bytes) {
            LOG_DBG("txtRecord[%lu]: key: \"%s\"  val \"%s\"",
                    (unsigned long) i,
                    txtRecords[i].key,
                    (char*) txtRecords[i].value.bytes);
        } else {
            LOG_DBG("txtRecord[%lu]: \"%s\" NO VALUE", (unsigned long) i, txtRecords[i].key);
        }
        errorCode = kDNSServiceErr_NoError;
        errorCode = TXTRecordSetValue(
                &serviceDiscovery->txtRecord,
                txtRecords[i].key,
                (uint8_t) txtRecords[i].value.numBytes,
                txtRecords[i].value.bytes);
        if (errorCode != kDNSServiceErr_NoError) {
            LOG_ERR("TXTRecordSetValue failed: %ld.", (long) errorCode);
            HAPFatalError();
        }
    }

    errorCode = kDNSServiceErr_NoError;
    errorCode = DNSServiceRegister(
            &serviceDiscovery->dnsService,
            /* flags: */ 0,
            interfaceIndex,
            name,
            protocol,
            /* domain: */ NULL,
            /* host: */ NULL,
            port,
            TXTRecordGetLength(&serviceDiscovery->txtRecord),
            TXTRecordGetBytesPtr(&serviceDiscovery->txtRecord),
            HandleServiceRegisterReply,
            replyContext);
    if (errorCode != kDNSServiceErr_NoError) {
        LOG_ERR("DNSServiceRegister failed: %ld.", (long) errorCode);
        HAPFatalError();
    }
}

void HAPPlatformServiceDiscoveryUpdateTXTRecords(
        HAPPlatformServiceDiscoveryRef serviceDiscovery,
        HAPPlatformServiceDiscoveryTXTRecord* txtRecords,
        size_t numTXTRecords) {

    HAPPrecondition(serviceDiscovery);
    HAPPrecondition(serviceDiscovery->dnsService);
    HAPPrecondition(txtRecords);

    DNSServiceErrorType errorCode;

    TXTRecordDeallocate(&serviceDiscovery->txtRecord);
    TXTRecordCreate(
            &serviceDiscovery->txtRecord,
            sizeof serviceDiscovery->txtRecordBuffer,
            &serviceDiscovery->txtRecordBuffer[0]);
    for (size_t i = 0; i < numTXTRecords; i++) {
        HAPPrecondition(!txtRecords[i].value.numBytes || txtRecords[i].value.bytes);
        HAPPrecondition(txtRecords[i].value.numBytes <= UINT8_MAX);
        if (txtRecords[i].value.bytes) {
            LOG_DBG("txtRecord[%lu]: \"%s\"", (unsigned long) i, txtRecords[i].key);
            LOG_HEXDUMP_DBG(txtRecords[i].value.bytes, txtRecords[i].value.numBytes, "");
        } else {
            LOG_DBG("txtRecord[%lu]: \"%s\"", (unsigned long) i, txtRecords[i].key);
        }
        errorCode = kDNSServiceErr_NoError;
        errorCode = TXTRecordSetValue(
                &serviceDiscovery->txtRecord,
                txtRecords[i].key,
                (uint8_t) txtRecords[i].value.numBytes,
                txtRecords[i].value.bytes);
        if (errorCode != kDNSServiceErr_NoError) {
            LOG_ERR("TXTRecordSetValue failed: %ld.", (long) errorCode);
            HAPFatalError();
        }
    }

    errorCode = kDNSServiceErr_NoError;
    errorCode = DNSServiceUpdateRecord(
            serviceDiscovery->dnsService,
            /* recordRef: */ NULL,
            /* flags: */ 0,
            TXTRecordGetLength(&serviceDiscovery->txtRecord),
            TXTRecordGetBytesPtr(&serviceDiscovery->txtRecord),
            0);
    if (errorCode != kDNSServiceErr_NoError) {
        LOG_ERR("DNSServiceUpdateRecord failed: %ld.", (long) errorCode);
        HAPFatalError();
    }
}

void HAPPlatformServiceDiscoveryStop(HAPPlatformServiceDiscoveryRef serviceDiscovery) {
    HAPPrecondition(serviceDiscovery);

    if (serviceDiscovery->dnsService) {
        DNSServiceRefDeallocate(serviceDiscovery->dnsService);
        serviceDiscovery->dnsService = NULL;

        TXTRecordDeallocate(&serviceDiscovery->txtRecord);
    }
}

void HAPPlatformServiceDiscoveryRegisterReplyCallback(ServiceDiscoveryCallback callback, void* context) {
    replyCallback = callback;
    replyContext = context;
}

#endif
