cmake_minimum_required(VERSION 3.24)

project(
    MouseSensitivityFix
    VERSION 0.54.1
    LANGUAGES CXX
)

# CMake VERSION is numeric; ZIP name and BuildIdentity use this public identity.
set(MSF_PLUGIN_VERSION_STRING "0.54b.1")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(MSF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MSF_BUILD_PLUGIN "Build the SKSE plugin" ON)
option(MSF_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
msf_apply_project_warnings(MSF_WARNINGS_AS_ERRORS)

set(MSF_PUBLIC_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(MSF_BUILD_PLUGIN)
    set(MSF_PLUGIN_SOURCES
        src/main.cpp
        src/Log.cpp
        src/Plugin.cpp
        src/config/Config.cpp
        src/hooks/Hooks.cpp
        src/compat/Compatibility.cpp
        src/ui/MenuFrameworkBridge.cpp
    )

    find_package(CommonLibSSE CONFIG REQUIRED)

    # SKSEPlugin_Version / SKSEPlugin_Query are defined in src/main.cpp so we can
    # declare Address Library v5 plus NoStructUse without relying on NG's
    # generated PluginDeclaration (Independent currently maps to NoStructUse only).
    add_library(${PROJECT_NAME} SHARED ${MSF_PLUGIN_SOURCES})
    target_link_libraries(${PROJECT_NAME}
        PRIVATE CommonLibSSE::CommonLibSSE
    )
    if(NOT PROJECT_VERSION_TWEAK)
        set(MSF_PLUGIN_VERSION_TWEAK 0)
    else()
        set(MSF_PLUGIN_VERSION_TWEAK ${PROJECT_VERSION_TWEAK})
    endif()
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            MSF_USE_COMMONLIBSSE=1
            MSF_PLUGIN_VERSION="${MSF_PLUGIN_VERSION_STRING}"
            MSF_PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
            MSF_PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
            MSF_PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
            MSF_PLUGIN_VERSION_TWEAK=${MSF_PLUGIN_VERSION_TWEAK}
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME}
            PRIVATE
                /FI${CMAKE_CURRENT_SOURCE_DIR}/include/MouseSensitivityFix/StringViewLiteralsCompat.h
        )
    endif()
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

    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION Data/SKSE/Plugins
    )
    install(FILES dist/Data/SKSE/Plugins/MouseSensitivityFix.ini
        DESTINATION Data/SKSE/Plugins
    )
    install(FILES dist/README.md
        DESTINATION .
        RENAME README.md
    )
    install(FILES CHANGELOG.md
        DESTINATION .
    )

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "Concise-Mouse-Consistency-${MSF_PLUGIN_VERSION_STRING}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    include(CPack)
endif()

if(MSF_BUILD_TESTS)
    include(CTest)
    find_package(Threads REQUIRED)
    add_executable(MouseSensitivityFixTests
        tests/TestMain.cpp
        src/Log.cpp
        src/config/Config.cpp
        src/hooks/Hooks.cpp
        src/compat/Compatibility.cpp
    )
    target_compile_definitions(MouseSensitivityFixTests
        PRIVATE MSF_USE_COMMONLIBSSE=0
    )
    target_include_directories(MouseSensitivityFixTests
        PRIVATE ${MSF_PUBLIC_INCLUDES}
    )
    target_link_libraries(MouseSensitivityFixTests
        PRIVATE Threads::Threads
    )
    add_test(NAME MouseSensitivityFix.UnitTests COMMAND MouseSensitivityFixTests)
endif()
