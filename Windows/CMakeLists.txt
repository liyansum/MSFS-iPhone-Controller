cmake_minimum_required(VERSION 3.20)
project(MSFSiPhoneController LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# SimConnect SDK 路径
#   1) 命令行：-DSIMCONNECT_SDK_PATH=<路径>
#   2) 环境变量：SIMCONNECT_SDK_PATH=<路径>
#   3) 均未指定：使用仓库内置的 ThirdParty/SimConnect（供 CI/无 SDK 环境）
# 路径指向含 include/ 与 lib/ 的目录。
# ------------------------------------------------------------------
if(NOT DEFINED SIMCONNECT_SDK_PATH)
    set(SIMCONNECT_SDK_PATH $ENV{SIMCONNECT_SDK_PATH})
endif()

if(NOT SIMCONNECT_SDK_PATH)
    set(SIMCONNECT_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SimConnect")
    message(STATUS "未指定 SIMCONNECT_SDK_PATH，使用内置 SDK: ${SIMCONNECT_SDK_PATH}")
endif()

if(NOT EXISTS "${SIMCONNECT_SDK_PATH}/include/SimConnect.h")
    message(FATAL_ERROR
        "未找到 SimConnect SDK（缺少 include/SimConnect.h）。请指定路径：\n"
        "  cmake -B build -DSIMCONNECT_SDK_PATH=\"D:/MSFS SDK/SimConnect SDK\"")
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

add_executable(MSFSiPhoneController WIN32 ${SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")

target_include_directories(MSFSiPhoneController PRIVATE
    "${SIMCONNECT_SDK_PATH}/include")
target_link_directories(MSFSiPhoneController PRIVATE
    "${SIMCONNECT_SDK_PATH}/lib")
target_link_libraries(MSFSiPhoneController PRIVATE
    SimConnect.lib ws2_32 shell32 iphlpapi)

target_compile_definitions(MSFSiPhoneController PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

# 构建后把 SimConnect.dll 复制到 exe 旁（运行时依赖，避免缺少 DLL 报错）
if(EXISTS "${SIMCONNECT_SDK_PATH}/lib/SimConnect.dll")
    add_custom_command(TARGET MSFSiPhoneController POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SIMCONNECT_SDK_PATH}/lib/SimConnect.dll"
            "$<TARGET_FILE_DIR:MSFSiPhoneController>/SimConnect.dll")
endif()
