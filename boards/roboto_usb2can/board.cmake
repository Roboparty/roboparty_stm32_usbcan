# Copyright (c) 2025 roboparty <2321901849@qq.com>
# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=STM32G431CB")
board_runner_args(pyocd "--target=stm32g431cbtx")
board_runner_args(openocd "--config=${BOARD_DIR}/support/openocd.cfg")
board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(probe-rs "--chip=STM32G431CBTx")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/probe-rs.board.cmake)