#
# Copyright (c) 2020, Nordic Semiconductor ASA
# All rights reserved.
#
# Use in source and binary forms, redistribution in binary form only, with
# or without modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions in binary form, except as embedded into a Nordic
#    Semiconductor ASA integrated circuit in a product or a software update for
#    such product, must reproduce the above copyright notice, this list of
#    conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
#
# 2. Neither the name of Nordic Semiconductor ASA nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# 3. This software, with or without modification, must only be used with a Nordic
#    Semiconductor ASA integrated circuit.
#
# 4. Any software provided in binary form under this license must not be reverse
#    engineered, decompiled, modified and/or disassembled.
#
# THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CONFIG_HOMEKIT)

  ## Protocols
  zephyr_interface_library_named(homekit_interface)
  zephyr_link_libraries(homekit_interface)

  if (CONFIG_HAP_HAVE_BLE)
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_BLE=1
      HAP_FEATURE_BLE=1
    )
  endif () # CONFIG_HAP_HAVE_BLE
  
  if (CONFIG_HAP_HAVE_THREAD)
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_THREAD=1
      HAP_FEATURE_THREAD=1
      THREAD_DEVKIT
      THREAD_DEVKIT_ADK
    )

    if (CONFIG_HAP_USE_THREAD_STATIC_COMMISSIONING)
      target_compile_definitions(homekit_interface INTERFACE
        THREAD_STATIC_COMMISSIONING=1
      )
    endif ()

  endif () # CONFIG_HAP_HAVE_THREAD

  ## Fetures
  if (CONFIG_HAP_HAVE_MFI_HW_AUTH)
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_MFI_HW_AUTH=1
      HAP_FEATURE_MFI_HW_AUTH=1
    )
  else ()
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_MFI_HW_AUTH=0
      HAP_FEATURE_MFI_HW_AUTH=0
    )
  endif () # CONFIG_HAP_HAVE_MFI_HW_AUTH
  
  if(CONFIG_HAP_HAVE_NFC)
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_NFC=1
      HAP_FEATURE_NFC=1
	  HAVE_NFC_ACCESS=1
	  HAP_FEATURE_NFC_ACCESS=1
	  HAVE_KEY_EXPORT=1
	  HAP_FEATURE_KEY_EXPORT=1
    )
  else()
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_NFC=0
      HAP_FEATURE_NFC=0
    )
  endif() # CONFIG_HAP_HAVE_NFC

  if (CONFIG_HAP_HAVE_DISPLAY)
    target_compile_definitions(homekit_interface INTERFACE HAVE_DISPLAY=1)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAVE_DISPLAY=0)
  endif () # CONFIG_HAP_HAVE_DISPLAY

  if (CONFIG_HAP_HAVE_ACCESSORY_REACHABILITY)
    target_compile_definitions(homekit_interface INTERFACE
      HAVE_ACCESSORY_REACHABILITY=1
    )
  endif () # CONFIG_HAP_HAVE_ACCESSORY_REACHABILITY

  ## Logs
  if (CONFIG_HAP_LOG_LEVEL_NONE)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_LEVEL=0)
  elseif (CONFIG_HAP_LOG_LEVEL_ERROR)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_LEVEL=1)
  elseif (CONFIG_HAP_LOG_LEVEL_DEFAULT)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_LEVEL=2)
  elseif (CONFIG_HAP_LOG_LEVEL_INFO)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_LEVEL=3)
  elseif (CONFIG_HAP_LOG_LEVEL_DEBUG)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_LEVEL=4)
  endif () #CONFIG_HAP_LOG_LEVEL

  if (CONFIG_HAP_LOG_SENSITIVE)
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_SENSITIVE=1)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAP_LOG_SENSITIVE=0)
  endif () # CONFIG_HAP_LOG_SENSITIVE

  if (CONFIG_HAP_ENABLE_BUFFER_LOGS)
    target_compile_definitions(homekit_interface INTERFACE HAP_ENABLE_BUFFER_LOGS=1)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAP_ENABLE_BUFFER_LOGS=0)
  endif () # CONFIG_HAP_ENABLE_BUFFER_LOG
  
  if (CONFIG_HAP_ASSERTS)
    target_compile_definitions(homekit_interface INTERFACE HAP_DISABLE_ASSERTS=0)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAP_DISABLE_ASSERTS=1)
  endif () # CONFIG_HAP_ASSERTS
  
  if (CONFIG_HAP_PRECONDITIONS)
    target_compile_definitions(homekit_interface INTERFACE HAP_DISABLE_PRECONDITIONS=0)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAP_DISABLE_PRECONDITIONS=1)
  endif () # CONFIG_HAP_PRECONDITIONS

  if (CONFIG_HAP_TESTING)
    target_compile_definitions(homekit_interface INTERFACE HAP_TESTING=1)
  else ()
    target_compile_definitions(homekit_interface INTERFACE HAP_TESTING=0)
  endif () # CONFIG_HAP_TESTING



  ## Crypto
  target_compile_definitions(homekit_interface INTERFACE HAVE_CUSTOM_SINGLE_SHOT_CHACHA20_POLY1305)

  if(CONFIG_HAP_NRF_SECURITY)
    set(CRYPTO_PAL "nRFSecurity")
  else ()
    set(CRYPTO_PAL "Oberon")
  endif () # CONFIG_HAP_NRF_SECURITY

  ## ADK Version
  FILE(READ "adk/Build/Makefile.Version" VERSION)
  string(REGEX MATCH "BUILD_ID=([a-z0-9]*)" _ ${VERSION})
  set(BUILD_ID ${CMAKE_MATCH_1})
  string(REGEX MATCH "VERSION_MAJOR=([0-9]*)" _ ${VERSION})
  set(VERSION_MAJOR ${CMAKE_MATCH_1})
  string(REGEX MATCH "VERSION_MINOR=([0-9]*)" _ ${VERSION})
  set(VERSION_MINOR ${CMAKE_MATCH_1})
  
  target_compile_definitions(homekit_interface INTERFACE
    HAP_COMPILER_VERSION=__VERSION__
    HAP_BUILD=${BUILD_ID}
    HAP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}"
    HAP_FEATURE_MFI_TOKEN_AUTH=1
    HAVE_MFI_TOKEN_AUTH=1
  )

if (CONFIG_HAP_FIRMWARE_UPDATE)
  target_compile_definitions(homekit_interface INTERFACE
    HAP_FEATURE_UARP_SUPPORT=1
    HAVE_UARP_SUPPORT=1
    UARP_DISABLE_LOGS=1
    HAP_FEATURE_HDS_TRANSPORT_OVER_HAP=1
    HAVE_HDS_TRANSPORT_OVER_HAP=1
    HAP_APP_USES_HDS=1
    HAVE_FIRMWARE_UPDATE=1
  )
  target_include_directories(homekit_interface INTERFACE adk/External/UARP)
  target_include_directories(homekit_interface INTERFACE adk/Applications/Common/DB/)
  FILE(GLOB adk_pal_external_UARP_sources adk/External/UARP/*.c)
endif()

  ## ADK libraries
  zephyr_library()
  zephyr_library_link_libraries(homekit_interface)
  zephyr_library_link_libraries(nrfxlib_crypto)
  if(CONFIG_OPENTHREAD_NRF_SECURITY_CHOICE)
   zephyr_library_link_libraries(mbedtls_external)
  elseif(CONFIG_MBEDTLS)
    zephyr_library_link_libraries(mbedTLS)
  endif()

  if(CMAKE_COMPILER_IS_GNUCC)
    zephyr_library_compile_options("-Wno-unknown-pragmas")
  endif(CMAKE_COMPILER_IS_GNUCC)

  target_include_directories(homekit_interface INTERFACE adk/HAP)
  target_include_directories(homekit_interface INTERFACE adk/HAP/Thread)
  target_include_directories(homekit_interface INTERFACE adk/PAL)
  target_include_directories(homekit_interface INTERFACE adk/PAL/Thread)
  target_include_directories(homekit_interface INTERFACE adk/PAL/Thread/OpenThread)
  target_include_directories(homekit_interface INTERFACE adk/PAL/nRFConnect)
  target_include_directories(homekit_interface INTERFACE adk/External/HTTP)
  target_include_directories(homekit_interface INTERFACE adk/External/mdnsresponder/mDNSShared)
  target_include_directories(homekit_interface INTERFACE adk/PAL/Crypto/Common)
  target_include_directories(homekit_interface INTERFACE adk/External/Base64)
  target_include_directories(homekit_interface INTERFACE adk/External/JSON)
  target_include_directories(homekit_interface INTERFACE adk/External/mdnsresponder/ServiceRegistration)
  target_include_directories(homekit_interface INTERFACE adk/Applications/Common/Helper/)
  target_include_directories(homekit_interface INTERFACE adk/Applications/Common/Platform/)

  FILE(GLOB adk_pal_nRFConnect_sources adk/PAL/nRFConnect/*.c)
  FILE(GLOB adk_pal_sources adk/PAL/*.c)
  FILE(GLOB adk_hap_sources adk/HAP/*.c)

  if(NOT CONFIG_HAP_HAVE_BLE)
    list(REMOVE_ITEM adk_pal_nRFConnect_sources adk/PAL/nRFConnect/HAPPlatformBLE.c)
    list(REMOVE_ITEM adk_pal_nRFConnect_sources adk/PAL/nRFConnect/HAPPlatformBLEPeripheralManager.c)
    list(REMOVE_ITEM adk_hap_sources adk/HAP/HAPBLEAccessoryServer.c)
    list(REMOVE_ITEM adk_hap_sources adk/HAP/HAPBLEPeripheralManager.c)
    list(REMOVE_ITEM adk_hap_sources adk/HAP/HAPBLESession.c)
    list(REMOVE_ITEM adk_hap_sources adk/HAP/HAPBLEProcedure.c)
  endif() # NOT CONFIG_HAP_HAVE_BLE
  
  if(CONFIG_HAP_HAVE_THREAD)
    FILE(GLOB dnssd_clientlib adk/External/mdnsresponder/mDNSShared/*.c)
    FILE(GLOB dnssd_serviceregistration adk/External/mdnsresponder/ServiceRegistration/*.c)
    FILE(GLOB adk_pal_open_thread_sources adk/PAL/Thread/OpenThread/*.c)
    FILE(GLOB app_helper_thread adk/Applications/Common/Helper/ThreadReattachHelper.c)
    zephyr_library_sources(
      ${adk_pal_open_thread_sources}
      ${dnssd_clientlib}
      ${dnssd_serviceregistration}
      ${app_helper_thread})
  else()
    list(REMOVE_ITEM adk_pal_nRFConnect_sources adk/PAL/nRFConnect/HAPPlatformThread.c)
    FILE(GLOB adk_pal_open_thread_sources adk/PAL/Thread/Mock/*.c)
    zephyr_library_sources(${adk_pal_open_thread_sources})
  endif() # CONFIG_HAP_HAVE_THREAD

  FILE(GLOB adk_hap_thread_sources adk/HAP/Thread/*.c)
  FILE(GLOB adk_pal_crypto_sources adk/PAL/Crypto/${CRYPTO_PAL}/*.c)
  FILE(GLOB adk_pal_external_Base_sources adk/External/Base64/*.c)
  FILE(GLOB adk_pal_external_HTTP_sources adk/External/HTTP/*.c)
  FILE(GLOB adk_pal_external_JSON_sources adk/External/JSON/*.c)
  FILE(GLOB app_generic_input adk/Applications/Common/Platform/AppUserInputGeneric.c)
  FILE(GLOB app_db adk/Applications/Common/DB/*.c)
  FILE(GLOB app_helper adk/Applications/Common/Helper/*.c)

  list(FILTER app_helper EXCLUDE REGEX .*/adk/Applications/Common/Helper/StandardInputHandler.c)

  
  zephyr_library_sources(
    ${adk_pal_nRFConnect_sources}
    ${adk_pal_sources}
    ${adk_hap_sources}
    ${adk_hap_thread_sources}
    ${adk_pal_crypto_sources}
    ${adk_pal_external_Base_sources}
    ${adk_pal_external_HTTP_sources}
    ${adk_pal_external_JSON_sources}
    ${adk_pal_external_UARP_sources}
    ${app_generic_input}
    ${app_db}
    ${app_helper}
  )
  
  zephyr_library_include_directories(${ZEPHYR_BASE}/subsys/bluetooth/host)
  zephyr_library_compile_definitions(_DEFAULT_SOURCE=1)

  ## NCS Utils for ADK
  add_subdirectory(utils)

endif()
