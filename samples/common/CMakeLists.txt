#
# Copyright (c) 2021, Nordic Semiconductor ASA
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

cmake_minimum_required(VERSION 3.20.0)

cmake_path(SET ZEPHYR_BASE_DIR $ENV{ZEPHYR_BASE})

# Default configuration
list(APPEND OVERLAY_CONFIG
  "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-defaults.conf"
  "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-bluetooth-host.conf"
)

if (NOT ("${BOARD}" STREQUAL "nrf5340dk_nrf5340_cpuapp"))
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-bluetooth-controler.conf")
endif()

option(THREAD "Enable Thread transport support" OFF)
if (THREAD)
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-thread.conf")
else()
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-ble-only.conf")
endif()

option(DEBUG "Enable debug configuration" OFF)
if (DEBUG)
  set(CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/prj_debug.conf")
endif()

option(NFC "Enable NFC support" OFF)
if (NFC)
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-nfc.conf")
endif()

option(MTD "Enable MTD Sleepy End Device" OFF)
if (MTD)
  if (THREAD)
    list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-mtd-sed.conf")
  else()
    message(FATAL_ERROR "MTD Sleepy End Device available only in Thread configuration (THREAD=y)")
  endif()
endif()

option(NORDIC_DFU "Enable Nordic DFU support" OFF)
if (NORDIC_DFU)
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-nordic-dfu.conf")
endif()

option(EUI64 "Enable custom EUI-64 address" OFF)
if (EUI64)
  if (THREAD)
    list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-custom-EUI-64.conf")
  else()
    message(FATAL_ERROR "Custom EUI-64 available only in Thread configuration (THREAD=y)")
  endif()
endif()

option(EXT_FLASH "Enable External flash support" OFF)

# Enable External flash and front-end module (FEM) support for Thread on nRF52840 DK
if (("${BOARD}" STREQUAL "nrf52840dk_nrf52840") AND (THREAD))
  set(EXT_FLASH ON)
  set(SHIELD nrf21540_ek)
endif()

# For nRF5340 DK:
# - enable external flash by default for BLE only and Thread
# - enable front-end module (FEM) support for Thread
if ("${BOARD}" STREQUAL "nrf5340dk_nrf5340_cpuapp")
  set(EXT_FLASH ON)
  if (THREAD)
    set(SHIELD nrf21540_ek)
    set(multiprotocol_rpmsg_SHIELD nrf21540_ek)
    list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf5340dk_nrf5340_cpuapp_thread.overlay")
    list(APPEND multiprotocol_rpmsg_DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf5340dk_nrf5340_cpunet_thread.overlay")
  endif()
endif()

if (("${BOARD}" STREQUAL "nrf52833dk_nrf52833" OR "${BOARD}" STREQUAL "nrf21540dk_nrf52840"))
#  Ignore those boards for HomeKit DFU due to lack of external memory on DK
else()
  if (NOT (NORDIC_DFU))
    set(EXT_FLASH ON)
    list(APPEND OVERLAY_CONFIG
    "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-firmware-update.conf")
  endif()
endif()

if (EXT_FLASH)
  list(APPEND OVERLAY_CONFIG
    "${CMAKE_CURRENT_LIST_DIR}/overlay-external_flash.conf")

  # This needs to be made globally available as it is used in other files.
  set(mcuboot_OVERLAY_CONFIG ${CMAKE_CURRENT_LIST_DIR}/mcuboot/overlay-external_flash.conf CACHE INTERNAL "")

  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/dts_external_flash.overlay")

  if ("${BOARD}" STREQUAL "nrf5340dk_nrf5340_cpuapp")
    list(APPEND mcuboot_DTC_OVERLAY_FILE
        "${CMAKE_CURRENT_LIST_DIR}/dts_external_flash.overlay"
        "${ZEPHYR_BASE_DIR}/../nrf/modules/mcuboot/flash_sim.overlay")
  else()
    list(APPEND mcuboot_DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/dts_external_flash.overlay")
  endif()

endif()

if (THREAD AND DEBUG)
  list(APPEND OVERLAY_CONFIG "${CMAKE_CURRENT_LIST_DIR}/overlay-hk-thread_debug.conf")
endif()

# Board configuration
if (NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}")
message(FATAL_ERROR
        "Board ${BOARD} is not supported.\n"
        "Please make sure board specific configuration files are added to "
        "${COMMON_ROOT}/configuration/${BOARD}")
endif()

# Use static partition layout to ensure consistency between builds.
# This is to ensure settings storage will be at the same location after the DFU.
if (EXT_FLASH)
  if (EXISTS "${COMMON_ROOT}/configuration/${BOARD}/pm_static_ext_flash.yml")
    set(PM_STATIC_YML_FILE ${COMMON_ROOT}/configuration/${BOARD}/pm_static_ext_flash.yml)
  else()
    message(FATAL_ERROR "File pm_static_ext_flash.yml not found")
  endif()
else()
  if (EXISTS "${COMMON_ROOT}/configuration/${BOARD}/pm_static.yml")
    set(PM_STATIC_YML_FILE ${COMMON_ROOT}/configuration/${BOARD}/pm_static.yml)
  else()
    message(FATAL_ERROR "File pm_static.yml not found")
  endif()
endif()

if (("${BOARD}" STREQUAL "nrf52833dk_nrf52833") AND (THREAD))
  message(FATAL_ERROR "${BOARD} is not supported for Thread configuration")
endif()

if (("${BOARD}" STREQUAL "nrf52833dk_nrf52833" OR
     "${BOARD}" STREQUAL "nrf21540dk_nrf52840") AND (NORDIC_DFU))
  message(FATAL_ERROR "Nordic DFU is not supported for ${BOARD} due to lack of external flash memory on DK")
endif()

option(JTRACE "Configuration for SW7 switch set to `alt`" OFF)
if ("${BOARD}" STREQUAL "nrf52840dk_nrf52840")
  if (JTRACE)
    list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/nrf52840dk_nrf52840/J_trace.overlay")
  endif()
endif()

if ("${BOARD}" STREQUAL "nrf52833dk_nrf52833")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf52833dk_nrf52833.overlay")
endif()

if ("${BOARD}" STREQUAL "nrf21540dk_nrf52840")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf21540dk_nrf52840.overlay")
endif()

if ("${BOARD}" STREQUAL "nrf52840dk_nrf52840")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf52840dk_nrf52840.overlay")
endif()

if ("${BOARD}" STREQUAL "nrf5340dk_nrf5340_cpuapp")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/configuration/${BOARD}/nrf5340dk_nrf5340_cpuapp.overlay")
endif()
