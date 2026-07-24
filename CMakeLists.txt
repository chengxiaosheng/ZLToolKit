cmake_minimum_required(VERSION 3.1.3...3.26)
project(ZLToolKit)

option(BUILD_C-ARES "Build C-ARES" ON)
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_SHARED_LIBS "Build trantor as a shared lib" ON)
option(ENABLE_WEPOLL "enable wepoll on window" ON)
option(STRIP_SYMBOL "strip symbol on release build" ON)


set(ZLToolKit_MAJOR_VERSION 1)
set(ZLToolKit_MINOR_VERSION 5)
set(ZLToolKit_PATCH_VERSION 19)
set(ZLToolKit_VERSION ${ZLToolKit_MAJOR_VERSION}.${ZLToolKit_MINOR_VERSION}.${ZLToolKit_PATCH_VERSION})

include(GNUInstallDirs)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(GenerateExportHeader)


add_library(${PROJECT_NAME})
add_library(ZLToolKit::ZLToolKit ALIAS ${PROJECT_NAME})
generate_export_header(${PROJECT_NAME}
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/exports/toolkit/exports.h
        BASE_NAME ZLToolKit
        STATIC_DEFINE ZLToolKit_STATIC_DEFINE
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "ZLToolKit")
if (BUILD_SHARED_LIBS)
    set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${ZLToolKit_VERSION} SOVERSION ${ZLToolKit_MAJOR_VERSION})
    if(WIN32)
        target_compile_options(${PROJECT_NAME} PUBLIC /wd4251 /wd4275)
    endif ()
endif ()

set(INSTALL_ZLToolKit_CMAKE_DIR
        ${CMAKE_INSTALL_LIBDIR}/cmake/ZLToolKit
        CACHE PATH "Installation directory for cmake files"
)

# 加载自定义cmake模块
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
# 检查并判断是否支持 mmsghdr
check_struct_has_member("struct mmsghdr" msg_hdr sys/socket.h HAVE_MMSG_HDR)
# 检查并判断是否支持 sendmmsg
check_symbol_exists(sendmmsg sys/socket.h HAVE_SENDMMSG_API)
# 检查并判断是否支持 recvmmsg
check_symbol_exists(recvmmsg sys/socket.h HAVE_RECVMMSG_API)
if (HAVE_MMSG_HDR)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DHAVE_MMSG_HDR=1)
endif ()
if (HAVE_SENDMMSG_API)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DHAVE_SENDMMSG_API=1)
endif ()
if (HAVE_RECVMMSG_API)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DHAVE_RECVMMSG_API=1)
endif ()

# check the socket buffer size set by the upper cmake project, if it is set, use the setting of the upper cmake project, otherwise set it to 256K
# if the socket buffer size is set to 0, it means that the socket buffer size is not set, and the kernel default value is used(just for linux)
if (DEFINED SOCKET_DEFAULT_BUF_SIZE)
    if (SOCKET_DEFAULT_BUF_SIZE EQUAL 0)
        set(SOCKET_DEFAULT_BUF_SIZE 262144)
        message(STATUS "Socket default buffer size is not set, use the kernel default value")
    endif ()
    message(STATUS "Socket default buffer size is set to ${SOCKET_DEFAULT_BUF_SIZE}")
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DSOCKET_DEFAULT_BUF_SIZE=${SOCKET_DEFAULT_BUF_SIZE})
else ()
    message(STATUS "Socket default buffer size is set to 262144")
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DSOCKET_DEFAULT_BUF_SIZE=262144)
endif ()


if (WIN32)
    if (MSVC)
        target_compile_definitions(${PROJECT_NAME} PUBLIC -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS -DMP4V2_NO_STDINT_DEFS)
        target_compile_options(${PROJECT_NAME} PUBLIC "/utf-8")
        target_compile_options(${PROJECT_NAME} PUBLIC "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
        if (MSVC_VERSION GREATER_EQUAL 1914)
            target_compile_options(${PROJECT_NAME} PUBLIC "/Zc:__cplusplus")
        endif (MSVC_VERSION GREATER_EQUAL 1914)
    else ()
        target_compile_definitions(${PROJECT_NAME} PUBLIC -DMP4V2_NO_STDINT_DEFS)
        target_compile_definitions(${PROJECT_NAME} PUBLIC -D_WIN32_WINNT=0x0601)
    endif ()
else ()
    target_compile_options(${PROJECT_NAME} PUBLIC
            "-fPIC"
            "-Wall"
            "-Wextra"
            "-Wno-unused-function"
            "-Wno-unused-parameter"
            "-Wno-unused-variable"
            "-Wno-error=extra"
            "-Wno-error=missing-field-initializers"
            "-Wno-error=type-limits"
            "-Wno-comment")
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Haiku")
        target_link_libraries(${PROJECT_NAME} PRIVATE network)
    endif ()
endif ()


set(SOURCE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# include directories
target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${SOURCE_ROOT_DIR}>
        $<INSTALL_INTERFACE:${INSTALL_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/exports>
        $<BUILD_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/toolkit>
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/exports>
        ${SOURCE_ROOT_DIR}
        ${SOURCE_ROOT_DIR}/Util
        ${SOURCE_ROOT_DIR}/Network
        ${SOURCE_ROOT_DIR}/Poller
        ${SOURCE_ROOT_DIR}/trantor
)
if (WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SOURCE_ROOT_DIR}/win32)
endif ()

if (MINGW)
endif (MINGW)

set(ZLToolKit_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Buffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/BufferSock.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Kcp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Session.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Socket.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/sockutil.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/TcpClient.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/TcpServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/UdpServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/UdpClient.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/EventPoller.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/Pipe.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/PipeWrap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/SelectWrap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/Timer.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/TaskExecutor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/WorkThreadPool.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/base64.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/CMD.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/File.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/local_time.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/logger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/MD5.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/mini.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/NoticeCenter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SHA1.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SSLBox.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SSLUtil.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/strptime_win.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/util.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/uv_errno.cpp


        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/InetAddress.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/Channel.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/Certificate.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/TcpConnection.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/TcpClient.cc

        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/Date.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/MsgBuffer.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/Utilities.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/AsyncStreamBufferNode.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/MemBufferNode.cc
#        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/bcypt/bcrypt.cpp
#        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/bcypt/blowfish.cpp
#        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/auth.cc
)
set(network_public_header
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Buffer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/BufferSock.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Server.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Session.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Socket.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/sockutil.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/TcpClient.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/TcpServer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/UdpServer.h
)
set(poller_public_header
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/EventPoller.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/Pipe.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/PipeWrap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/SelectWrap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Poller/Timer.h
)
set(thread_public_header
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/semaphore.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/TaskExecutor.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/TaskQueue.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/threadgroup.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/ThreadPool.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread/WorkThreadPool.h
)
set(util_public_header
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/base64.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/CMD.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/File.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/function_traits.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/local_time.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/logger.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/MD5.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/mini.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/NoticeCenter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/onceToken.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/ResourcePool.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/RingBuffer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SHA1.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SpeedStatistic.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SSLBOX.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/SSLUtil.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/TimeTicker.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/strptime_win.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/util.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Util/uv_errno.h
)
set(win32_public_header)
set(trantor_public_header
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/AsyncStream.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/callbacks.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/Certificate.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/Channel.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/InetAddress.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/TcpClient.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/TcpConnection.h

        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/Date.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/Funcs.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/MsgBuffer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/Utilities.h
)
set(trantor_private_header)

if (IOS)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Network/Socket_ios.mm)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/FileBufferNodeUnix.cc)

elseif (WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DHAS_EPOLL=1)
    message(STATUS "Wepoll Enabled")
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 Iphlpapi shlwapi)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/getopt.c)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/wepoll.c)
    set(win32_public_header
            ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/getopt.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/tailor.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/wepoll.h
    )
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/FileBufferNodeWin.cc)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/WindowsSupport.cc)


else ()
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/FileBufferNodeUnix.cc)
endif ()

find_package(c-ares)
if (c-ares_FOUND)
    message(STATUS "c-ares found!")
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/AresResolver.cc)
    list(APPEND trantor_private_header ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/AresResolver.h)
#    if (NOT BUILD_SHARED_LIBS)
#        target_compile_definitions(${PROJECT_NAME} PRIVATE CARES_STATICLIB)
#    endif ()
    target_link_libraries(${PROJECT_NAME} PRIVATE c-ares::cares)
    if (APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE resolv)
    endif ()
else ()
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/NormalResolver.cc)
    list(APPEND trantor_private_header ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/net/inner/NormalResolver.h)
endif ()

find_package(Threads)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

find_package(OpenSSL 1.1)
if (OpenSSL_FOUND)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_OPENSSL=1)
    target_compile_definitions(${PROJECT_NAME} PUBLIC USE_OPENSSL=1)
    target_compile_definitions(${PROJECT_NAME} PUBLIC TRANTOR_TLS_PROVIDER=OpenSSL)
    list(APPEND ZLToolKit_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/openssl.cc)
else ()
    set(ZLToolKit_SOURCES ${ZLToolKit_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha3.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/md5.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha1.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha256.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/blake2.cc
    )
    set(trantor_private_header ${trantor_private_header}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha3.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/md5.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha1.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/sha256.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/trantor/utils/crypto/blake2.h
    )
    target_compile_definitions(${PROJECT_NAME} PUBLIC TRANTOR_TLS_PROVIDER=None)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME ZLToolKit)
target_sources(${PROJECT_NAME} PRIVATE ${ZLToolKit_SOURCES})

source_group(
        "Public API" FILES ${CMAKE_CURRENT_BINARY_DIR}/exports/toolkit/exports.h
        ${network_public_header}
        ${poller_public_header}
        ${thread_public_header}
        ${util_public_header}
        ${trantor_public_header}
        ${win32_public_header}
)
source_group("Private Headers" FILES ${trantor_private_header})


if(BUILD_SHARED_LIBS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND UNIX AND STRIP_SYMBOL)
    add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${PROJECT_NAME}> "$<TARGET_FILE:${PROJECT_NAME}>.sym"
            COMMAND ${CMAKE_OBJCOPY} --strip-all $<TARGET_FILE:${PROJECT_NAME}>
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink="$<TARGET_FILE:${PROJECT_NAME}>.sym" $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Stripping symbols from ${PROJECT_NAME} and saving"
    )
endif ()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()


install(TARGETS ${PROJECT_NAME}
        EXPORT ZLToolKitTargets
        RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}" COMPONENT lib
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}" COMPONENT lib
)
install(FILES ${network_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/Network)
install(FILES ${poller_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/Poller)
install(FILES ${thread_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/Thread)
install(FILES ${util_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/Util)
install(FILES ${trantor_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/trantor)
install(FILES ${win32_public_header} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit/win32)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/exports/toolkit/exports.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include/toolkit)

include(CMakePackageConfigHelpers)
# ... for the install tree
configure_package_config_file(
        cmake/templates/ZLToolKitConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/ZLToolKitConfig.cmake
        INSTALL_DESTINATION
        ${INSTALL_ZLToolKit_CMAKE_DIR}
)

# version
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ZLToolKitConfigVersion.cmake
        VERSION ${ZLToolKit_VERSION}
        COMPATIBILITY SameMajorVersion
)

# Install the TrantorConfig.cmake and TrantorConfigVersion.cmake
install(FILES "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/ZLToolKitConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ZLToolKitConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Findc-ares.cmake"
        DESTINATION "${INSTALL_ZLToolKit_CMAKE_DIR}"
        COMPONENT dev
)

# Install the export set for use with the install-tree
install(
        EXPORT ZLToolKitTargets
        DESTINATION "${INSTALL_ZLToolKit_CMAKE_DIR}"
        NAMESPACE ZLToolKit::
        COMPONENT dev
)