cmake_minimum_required(VERSION 3.24)

project(
    MouseSensitivityFix
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(MSF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
msf_apply_project_warnings(MSF_WARNINGS_AS_ERRORS)

set(MSF_SOURCES
    src/main.cpp
    src/Log.cpp
    src/Plugin.cpp
    src/config/Config.cpp
    src/hooks/Hooks.cpp
    src/compat/Compatibility.cpp
    src/ui/MenuFrameworkBridge.cpp
)

set(MSF_PUBLIC_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

find_package(CommonLibSSE REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES ${MSF_SOURCES}
)
target_link_libraries(${PROJECT_NAME}
    PRIVATE CommonLibSSE::CommonLibSSE
)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE MSF_USE_COMMONLIBSSE=1
)
target_compile_options(${PROJECT_NAME}
    PRIVATE
        /FI${CMAKE_CURRENT_SOURCE_DIR}/include/MouseSensitivityFix/StringViewLiteralsCompat.h
)
target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        <SKSE/Impl/PCH.h>
)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${MSF_PUBLIC_INCLUDES}
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME "MouseSensitivityFix"
)
