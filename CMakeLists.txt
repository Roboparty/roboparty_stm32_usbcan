# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(NO_BUILD_TYPE_WARNING TRUE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(roboto_usb2can)

# Version definition
set(APP_VERSION_MAJOR 3)
set(APP_VERSION_MINOR 0)
set(APP_VERSION_PATCH 0)

# Get build date
string(TIMESTAMP BUILD_DATE "%Y%m%d" UTC)

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/version.h)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)

target_sources(app PRIVATE src/main.c src/led.c)

# Print version info for reference
message(STATUS "Building roboto_usb2can v${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH} (${BUILD_DATE})")

# Define version as compile definitions
target_compile_definitions(app PRIVATE
  APP_VERSION_MAJOR=${APP_VERSION_MAJOR}
  APP_VERSION_MINOR=${APP_VERSION_MINOR}
  APP_VERSION_PATCH=${APP_VERSION_PATCH}
  BUILD_DATE=\"${BUILD_DATE}\"
)

# Create versioned release files when building in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
  set(VERSION_NAME "roboto_usb2can_v${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}_${BUILD_DATE}")

  add_custom_target(release_files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/zephyr/zephyr.bin
    ${CMAKE_CURRENT_BINARY_DIR}/zephyr/${VERSION_NAME}.bin
    COMMENT "Creating release binary: ${VERSION_NAME}.bin"
    VERBATIM
  )

  message(STATUS "Release mode: Will create versioned binary ${VERSION_NAME}.bin")
  message(STATUS "Run 'cmake --build . --target release_files' after build to generate release binary")
endif()
