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

set(COMMON_ROOT $ENV{ZEPHYR_BASE}/../homekit/samples/common)

# Common configuration
include(${COMMON_ROOT}/CMakeLists.txt)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(homekit_lock)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/app.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/tag_reader.c)
target_sources(app PRIVATE ${COMMON_ROOT}/src/main.c)
target_sources(app PRIVATE ${COMMON_ROOT}/src/hap.c)

target_sources(app PRIVATE
  ${ZEPHYR_BASE}/../homekit/adk/Applications/AppBase.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Lock/DB.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Lock/DebugCommandHandler.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/AppLED.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/AppUserInput.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/BLEStatusLED.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/ThreadStatusLED.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/DFUStatusLED.c
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform/nRFConnect/PowerManagment.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/../homekit/adk/Applications
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Lock
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/DB
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Helper
  ${ZEPHYR_BASE}/../homekit/adk/Applications/Common/Platform
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
add_custom_command(TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy compile_commands.json ${ZEPHYR_BASE}/../compile_commands.json
)
endif()
