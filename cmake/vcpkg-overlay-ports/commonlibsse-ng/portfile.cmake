# Pin alandtse/CommonLibSSE-NG v7.0.0 (tag v7.0.0 / ng @ 8b032fa).
# Parses Address Library format 5, knows RUNTIME_SSE_1_7_99, and still
# builds SE+AE+VR. Do not switch this port to powerof3/CommonLibSSE.
set(MSF_COMMONLIBSSE_NG_REF "8b032fa992750d654d6d38a33731714d8b86be1f")
set(MSF_OPENVR_REF "60eb187801956ad277f1cae6680e3a410ee0873b")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF "${MSF_COMMONLIBSSE_NG_REF}"
    SHA512 9def8a8e954fca898f0c57481a5b6415ea53481b59ff66b5982bb1f63cb847aacf2b85ab021a7e26cdb9df7689534621e5c81fb43477fea1d3135e27a44391b6
    HEAD_REF ng
)

# OpenVR is a git submodule; vcpkg_from_github does not populate it.
vcpkg_from_github(
    OUT_SOURCE_PATH OPENVR_SOURCE_PATH
    REPO ValveSoftware/openvr
    REF "${MSF_OPENVR_REF}"
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
    HEAD_REF master
)
file(REMOVE_RECURSE "${SOURCE_PATH}/extern/openvr")
file(RENAME "${OPENVR_SOURCE_PATH}" "${SOURCE_PATH}/extern/openvr")

# Consumers need DirectXTK on the find_package path; upstream config only
# pulls spdlog.
vcpkg_replace_string(
    "${SOURCE_PATH}/cmake/config.cmake.in"
    "find_dependency(spdlog CONFIG)"
    "find_dependency(spdlog CONFIG)\nfind_dependency(directxtk CONFIG)"
)

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
    OPTIONS
        -DBUILD_TESTS=OFF
        -DSKSE_SUPPORT_XBYAK=ON
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
        -DENABLE_SKYRIM_SE=ON
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_VR=ON
        -DREX_OPTION_INI=OFF
        -DREX_OPTION_JSON=OFF
        -DREX_OPTION_TOML=OFF
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)
vcpkg_copy_pdbs()

file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
if(CMAKE_CONFIGS)
    file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
endif()
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE")

file(
    INSTALL "${SOURCE_PATH}/COPYING"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright
)
