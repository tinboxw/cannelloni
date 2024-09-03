cmake_minimum_required(VERSION 3.12)
project(cannelloni
        VERSION ${PROJECT_VERSION}
        DESCRIPTION "Soft CAN DTU")

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/opt/obss/candtu")
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Options
option(SCTP_SUPPORT "SCTP_SUPPORT" OFF)
option(USE_GENERIC_FORMAT "USE_GENERIC_FORMAT" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")

if(SCTP_SUPPORT)
  include(FindSCTP)
else(SCTP_SUPPORT)
  message(STATUS "Building cannelloni without SCTP support (SCTP_SUPPORT=OFF)")
endif(SCTP_SUPPORT)

if(NOT SCTP_FOUND AND SCTP_SUPPORT)
  set(SCTP_SUPPORT OFF)
  message(STATUS "SCTP not found. cannelloni will be build without SCTP support.")
  message(STATUS "Install lksctp-tools for SCTP.")
endif(NOT SCTP_FOUND AND SCTP_SUPPORT)

CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

if(USE_GENERIC_FORMAT)
  add_definitions(-DUSE_GENERIC_FORMAT)
  message(STATUS "Use generic format (Can DTU Format)")
endif()

add_executable(cannelloni cannelloni.cpp)
add_library(addsources STATIC
            connection.cpp
            framebuffer.cpp
            inet_address.cpp
            thread.cpp
            timer.cpp
            udpthread.cpp
            tcpthread.cpp
            tcp_client_thread.cpp
            tcp_server_thread.cpp
            canthread.cpp)

add_library(cannelloni-common SHARED
            parser.cpp
            decoder.cpp)

add_library(cannelloni-common-static STATIC
            parser.cpp
            decoder.cpp)

set_target_properties ( cannelloni-common
  PROPERTIES
  VERSION 0.0.1
  SOVERSION 0
)

include(GNUInstallDirs)

if(SCTP_SUPPORT)
    add_library(sctpthread STATIC sctpthread.cpp)
    target_link_libraries(sctpthread addsources sctp)
    target_link_libraries(addsources sctpthread)
endif(SCTP_SUPPORT)
set_target_properties(addsources PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cannelloni addsources cannelloni-common-static pthread)
target_compile_features(cannelloni PRIVATE cxx_auto_type)
target_compile_features(addsources PRIVATE cxx_auto_type)

install(TARGETS cannelloni DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/)
install(TARGETS cannelloni-common DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/)
install(FILES service/startup.sh DESTINATION ${CMAKE_INSTALL_PREFIX}/sbin/)

if(UNIX)
  # 通过configure_file将变量替换到config.h
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/service/obss-candtu.in"
    "${CMAKE_CURRENT_BINARY_DIR}/service/obss-candtu.service"
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/service/obss-candtu.service"
    DESTINATION /etc/systemd/system/
    #FILES_MATCHING PATTERN "*.service"
  )
endif ()


# 可选：指定兼容的架构
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
else ()
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif ()

# 配置CPack的DEB打包参数
set(CPACK_GENERATOR "DEB")
set(CPACK_PRODUCT_NAME "obss-${PRODUCT_VERSION}")
set(CPACK_PACKAGE_NAME "${CPACK_PRODUCT_NAME}-candtu")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}-0focal")
set(CPACK_PACKAGE_CONTACT "Your Name <your.email@example.com>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "tinbox.wu") # Maintainer for debian
set(CPACK_PACKAGE_DESCRIPTION "Soft DTU based on CAN implementation")
set(CPACK_PACKAGE_VENDOR "sword")
#set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.27), libstdc++6 (>= 7)")
# obts-1.0.0-pilot_0.0.0-0focal_amd64.deb
# obts-1.0.0-pilot_0.0.0-0focal_amd64.deb
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")

# 可选：设置安装目录
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# 可选：设置安装后的脚本
#set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/postinst;${CMAKE_CURRENT_SOURCE_DIR}/postrm")

# 初始化CPack
include(CPack)