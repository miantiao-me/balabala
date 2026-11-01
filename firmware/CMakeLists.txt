cmake_minimum_required(VERSION 3.16)
# 注册表ZIP及其传递依赖由项目摘要锁下载；不允许构建时重新解析版本。
set(ENV{IDF_COMPONENT_MANAGER} "0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../.tools/components/network_provisioning"
                         "${CMAKE_CURRENT_LIST_DIR}/../.tools/components/json_generator")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(balabala_microts_probe)
idf_component_get_property(netprov_lib network_provisioning COMPONENT_LIB)
target_link_libraries(${netprov_lib} PUBLIC idf::json_generator)
# 上游debug路径会打印SSID/密码；任何运行时日志级别也不能启用它。
target_compile_definitions(${netprov_lib} PRIVATE LOG_LOCAL_LEVEL=0)
