PROJECT(XRT)
CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)

SET(PROJECT_DESCRIPTION "https://github.com/Xilinx/XRT")

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/runtime_src/driver/include
  )

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message("-- Host system processor is ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message("-- Target system processor is ${CMAKE_SYSTEM_PROCESSOR}")

set(XRT_NATIVE_BUILD "yes")
if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL ${CMAKE_HOST_SYSTEM_PROCESSOR})
  set(XRT_NATIVE_BUILD "no")
endif()

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RelWithDebInfo)
endif (NOT CMAKE_BUILD_TYPE)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set (CMAKE_INSTALL_PREFIX "/opt/xilinx")
endif()
set (CMAKE_INSTALL_PREFIX "/opt/xilinx")

# Default component name for any install() command without the COMPONENT argument
set (CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "xrt")

set (XRT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(XRT_VERSION_RELEASE 201920)
SET(XRT_VERSION_MAJOR 2)
SET(XRT_VERSION_MINOR 3)

if (DEFINED ENV{XRT_VERSION_PATCH})
  SET(XRT_VERSION_PATCH $ENV{XRT_VERSION_PATCH})
else(DEFINED $ENV{XRT_VERSION_PATCH})
  SET(XRT_VERSION_PATCH 0)
endif(DEFINED ENV{XRT_VERSION_PATCH})

#set(XRT_SOVERSION ${XRT_VERSION_MAJOR}.${XRT_VERSION_MINOR})
set(XRT_SOVERSION ${XRT_VERSION_MAJOR})
set(XRT_VERSION_STRING ${XRT_VERSION_MAJOR}.${XRT_VERSION_MINOR}.${XRT_VERSION_PATCH})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake/")

if (${XRT_NATIVE_BUILD} STREQUAL "yes")
  include(CMake/native.cmake)
else()
  include(CMake/embedded_system.cmake)
endif()
