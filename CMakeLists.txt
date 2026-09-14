cmake_minimum_required(VERSION 4.2)

set(VCPKG_BUILD_TYPE release CACHE STRING "")

project(
    # gersemi: ignore
    PIXLRenderer
    VERSION 1.0.1
    LANGUAGES CXX
)

# PIXL Renderer is one closed DX11 pipeline: host code under engine/, integrated
# shader modules under pipeline/, and the authored runtime image under
# distribution/. Nothing in the build treats modules as optional add-ons.
source_group(TREE "${CMAKE_SOURCE_DIR}/engine/Renderer" PREFIX "PIXL Renderer" FILES
    "${CMAKE_SOURCE_DIR}/engine/Renderer/QualityProfiles.cpp"
    "${CMAKE_SOURCE_DIR}/engine/Renderer/QualityProfiles.h"
)

# default install path
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(
        CACHE CMAKE_INSTALL_PREFIX
        PROPERTY VALUE "${CMAKE_CURRENT_BINARY_DIR}/aio"
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ########################################################################################################################
# ## Build options
# ########################################################################################################################
message("Options:")
option(
    AUTO_PLUGIN_DEPLOYMENT
    "Copy the PIXL Renderer build output to the configured test game."
    OFF
)
option(TRACY_SUPPORT "Enable support for tracy profiler" OFF)
message("\tAuto plugin deployment: ${AUTO_PLUGIN_DEPLOYMENT}")
message("\tTracy profiler: ${TRACY_SUPPORT}")

# #######################################################################################################################
# # Build version info from git
# #######################################################################################################################
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_DESCRIBE "unknown")
endif()
message("\tBuild version: ${GIT_DESCRIBE}")

# #######################################################################################################################
# # Add CMake features
# #######################################################################################################################
include(XSEPlugin)

# #######################################################################################################################
# # Find dependencies
# #######################################################################################################################
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
find_package(pystring CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(unordered_dense CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)

add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/Streamline)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
include(FidelityFX-SDK)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE "$<$<BOOL:${TRACY_SUPPORT}>:TRACY_SUPPORT>"
)

file(
    GLOB FEATURE_SHADER_DIRS
    RELATIVE "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/pipeline/*/Kernels"
)

foreach(_dir IN LISTS FEATURE_SHADER_DIRS)
    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE "${CMAKE_SOURCE_DIR}/${_dir}"
    )
endforeach()

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
        ${CLIB_UTIL_INCLUDE_DIRS}
        "${CMAKE_SOURCE_DIR}/distribution/Shaders"
        "${CMAKE_SOURCE_DIR}/extern/sk_hdr_png/include"
        ${DETOURS_INCLUDE_DIRS}
)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE SK_HDR_PNG_PIXL_RENDERER
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        Microsoft::CppWinRT
        magic_enum::magic_enum
        xbyak::xbyak
        nlohmann_json::nlohmann_json
        imgui::imgui
        EASTL
        Microsoft::DirectXTK
        Microsoft::DirectXTex
        pystring::pystring
        unordered_dense::unordered_dense
        efsw::efsw
        Tracy::TracyClient
        Streamline
        d3d12.lib
        Microsoft::DirectX-Headers
        ${DETOURS_LIBRARY}
        windowsapp
        windowscodecs
)

# https://gitlab.kitware.com/cmake/cmake/-/issues/24922#note_1371990
if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE) # 17.6+
    # When using /std:c++latest, "Build ISO C++23 Standard Library Modules" defaults to "Yes".
    # Default to "No" instead.
    #
    # As of CMake 3.26.4, there isn't a way to control this property
    # (https://gitlab.kitware.com/cmake/cmake/-/issues/24922),
    # We'll use the MSBuild project system instead
    # (https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-file-structure)
    file(
        CONFIGURE
        OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props"
        CONTENT
            [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==]
        @ONLY
    )
endif()

# #######################################################################################################################
# # RenderModule version detection
# #######################################################################################################################
file(
    GLOB_RECURSE FEATURE_CONFIG_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "pipeline/*/Module.ini"
)

foreach(FEATURE_PATH ${FEATURE_CONFIG_FILES})
    file(READ "${FEATURE_PATH}" CONFIG_VALUE)
    string(STRIP "${CONFIG_VALUE}" CONFIG_VALUE)
    string(REGEX MATCH "(^|[\r\n])[ \t]*Id[ \t]*=[ \t]*([A-Za-z0-9_]+)" _PIXL_MODULE_ID_MATCH "${CONFIG_VALUE}")
    if(NOT _PIXL_MODULE_ID_MATCH)
        message(FATAL_ERROR "Integrated module descriptor has no Id: ${FEATURE_PATH}")
    endif()
    set(FEATURE "${CMAKE_MATCH_2}")

    # Retired descriptors remain in source only when they document a historical
    # shader ABI or provide an inert compatibility include. They must not be
    # advertised as integrated runtime modules or contribute version/visibility
    # metadata to the shipping DLL.
    string(REGEX MATCH "(^|[\r\n])[ \t]*Pipeline[ \t]*=[ \t]*([A-Za-z0-9_]+)" _PIXL_PIPELINE_MATCH "${CONFIG_VALUE}")
    if(_PIXL_PIPELINE_MATCH)
        string(TOLOWER "${CMAKE_MATCH_2}" _PIXL_PIPELINE)
        if(_PIXL_PIPELINE STREQUAL "retired")
            continue()
        endif()
    endif()

    if(CONFIG_VALUE)
        string(
            REGEX MATCH "[Vv]ersion = ([0-9]+)-([0-9]+)-([0-9]+)"
            _
            "${CONFIG_VALUE}"
        )
        if(
            DEFINED CMAKE_MATCH_1
            AND DEFINED CMAKE_MATCH_2
            AND DEFINED CMAKE_MATCH_3
        )
            set(ver_major ${CMAKE_MATCH_1})
            set(ver_minor ${CMAKE_MATCH_2})
            set(ver_patch ${CMAKE_MATCH_3})
            list(
                APPEND FEATURE_VERSIONS
                "\t\t{\"${FEATURE}\"sv, {${ver_major},${ver_minor},${ver_patch}}}"
            )
        else()
            message(
                WARNING
                "RenderModule config file '${FEATURE_PATH}' does not contain a valid version string. Skipping."
            )
        endif()
    else()
        message(
            WARNING
            "RenderModule config file '${FEATURE_PATH}' is empty or contains only whitespace. Skipping version detection for this feature."
        )
    endif()

    # Detect release stage from Alpha/Beta flags. Alpha takes precedence when
    # both are set. A truthy value is required; absent or non-truthy means the
    # feature is a full Release.
    # Anchor to a line start ((^|[\r\n]); CMake regex has no multiline mode) so a
    # settings key ending in "alpha"/"beta" (e.g. WaterAlpha = 1) is not mistaken
    # for a stage flag. Keep in sync with the line-anchored match in
    # tools/feature_version_audit.py.
    # NOTE: guard on the match-result variable because CMAKE_MATCH_2 (the value)
    # retains its prior value (the version major, above) when a REGEX MATCH misses.
    set(_STAGE_ALPHA "")
    set(_STAGE_BETA "")
    string(REGEX MATCH "(^|[\r\n])[ \t]*[Aa]lpha[ \t]*=[ \t]*([A-Za-z0-9]+)" _STAGE_ALPHA_MATCH "${CONFIG_VALUE}")
    if(_STAGE_ALPHA_MATCH)
        string(TOLOWER "${CMAKE_MATCH_2}" _STAGE_ALPHA)
    endif()
    string(REGEX MATCH "(^|[\r\n])[ \t]*[Bb]eta[ \t]*=[ \t]*([A-Za-z0-9]+)" _STAGE_BETA_MATCH "${CONFIG_VALUE}")
    if(_STAGE_BETA_MATCH)
        string(TOLOWER "${CMAKE_MATCH_2}" _STAGE_BETA)
    endif()
    if(_STAGE_ALPHA MATCHES "^(true|1|yes|on)$")
        list(APPEND FEATURE_ALPHA_NAMES "\t\t\"${FEATURE}\"sv")
    elseif(_STAGE_BETA MATCHES "^(true|1|yes|on)$")
        list(APPEND FEATURE_BETA_NAMES "\t\t\"${FEATURE}\"sv")
    endif()

    # Detect the Unreleased flag, which gates UI visibility independently of the
    # release stage above. Same line anchoring and CMAKE_MATCH_2 guard.
    set(_STAGE_UNRELEASED "")
    string(REGEX MATCH "(^|[\r\n])[ \t]*[Uu]nreleased[ \t]*=[ \t]*([A-Za-z0-9]+)" _STAGE_UNRELEASED_MATCH "${CONFIG_VALUE}")
    if(_STAGE_UNRELEASED_MATCH)
        string(TOLOWER "${CMAKE_MATCH_2}" _STAGE_UNRELEASED)
    endif()
    if(_STAGE_UNRELEASED MATCHES "^(true|1|yes|on)$")
        list(APPEND FEATURE_UNRELEASED_NAMES "\t\t\"${FEATURE}\"sv")
    endif()

    # Every descriptor belongs to the integrated PIXL pipeline.
    list(APPEND FEATURE_CORE_NAMES "\t\t\"${FEATURE}\"sv")
endforeach()

set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${FEATURE_CONFIG_FILES}"
)

string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")
string(REPLACE ";" ",\n" FEATURE_CORE_NAMES "${FEATURE_CORE_NAMES}")
string(REPLACE ";" ",\n" FEATURE_ALPHA_NAMES "${FEATURE_ALPHA_NAMES}")
string(REPLACE ";" ",\n" FEATURE_BETA_NAMES "${FEATURE_BETA_NAMES}")
string(REPLACE ";" ",\n" FEATURE_UNRELEASED_NAMES "${FEATURE_UNRELEASED_NAMES}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ModuleVersions.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/ModuleVersions.h
    @ONLY
)

target_sources(
    "${PROJECT_NAME}"
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cmake/ModuleVersions.h
)

# #######################################################################################################################
# # Theme presets
# #######################################################################################################################
set(THEMES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/distribution/SKSE/Plugins/PIXLRenderer/Themes"
)
file(GLOB THEME_JSON_FILES CONFIGURE_DEPENDS "${THEMES_DIR}/*.json")
set(THEME_PRESET_NAMES_LIST "")
foreach(f IN LISTS THEME_JSON_FILES)
    get_filename_component(name "${f}" NAME_WE)
    list(APPEND THEME_PRESET_NAMES_LIST "${name}")
endforeach()
list(SORT THEME_PRESET_NAMES_LIST)
list(LENGTH THEME_PRESET_NAMES_LIST THEME_PRESET_COUNT)
set(THEME_PRESET_NAMES "")
foreach(p IN LISTS THEME_PRESET_NAMES_LIST)
    set(THEME_PRESET_NAMES "${THEME_PRESET_NAMES}\t\t\"${p}\",\n")
endforeach()
string(REGEX REPLACE ",\n$" "" THEME_PRESET_NAMES "${THEME_PRESET_NAMES}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThemePresets.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/ThemePresets.h
    @ONLY
)

target_sources(
    "${PROJECT_NAME}"
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cmake/ThemePresets.h
)

# #######################################################################################################################
# # clang-format
# #######################################################################################################################

find_program(CLANG_FORMAT_PATH clang-format)
if(CLANG_FORMAT_PATH)
    add_custom_target(
        FORMAT_CODE
        COMMAND ${CLANG_FORMAT_PATH} -i -style=file ${CPP_SOURCES};${HLSL_FILES}
        COMMENT "Running clang format for cpp and hlsl files"
    )
endif()

# #######################################################################################################################
# # HLSL additional include directories for VS intellisense
# #######################################################################################################################

set(HLSL_INCLUDE_DIRS ${FEATURE_SHADER_DIRS} "distribution/Shaders")

set(HLSL_INCLUDE_JSON "")
foreach(dir IN LISTS HLSL_INCLUDE_DIRS)
    if(HLSL_INCLUDE_JSON STREQUAL "")
        set(HLSL_INCLUDE_JSON "    \"${dir}\"")
    else()
        set(HLSL_INCLUDE_JSON "${HLSL_INCLUDE_JSON},\n    \"${dir}\"")
    endif()
endforeach()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shadertoolsconfig.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/shadertoolsconfig.json"
    @ONLY
)

# #######################################################################################################################
# # PIXL standalone validation and packaging
# #######################################################################################################################
find_program(POWERSHELL_PATH NAMES pwsh powershell)
set(PIXL_AUDIT_SCRIPT "${CMAKE_SOURCE_DIR}/tools/AuditPixlRenderer.ps1")
set(PIXL_STAGE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/StagePixlRendererStandalone.ps1")

if(WIN32 AND POWERSHELL_PATH)
    add_custom_target(
        PIXL-Audit
        COMMAND
            ${POWERSHELL_PATH} -NoProfile -ExecutionPolicy Bypass -File
            "${PIXL_AUDIT_SCRIPT}" -BuildDirectory "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        DEPENDS ${PROJECT_NAME}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Auditing the integrated PIXL Renderer pipeline"
        VERBATIM
    )

    add_custom_target(
        PIXL-Standalone-Beta
        COMMAND
            ${POWERSHELL_PATH} -NoProfile -ExecutionPolicy Bypass -File
            "${PIXL_STAGE_SCRIPT}"
            -BuildDirectory "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            -SkipPipelineLibrary
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Staging a clean compile-on-device PIXL Renderer package"
        VERBATIM
    )
    add_dependencies(PIXL-Standalone-Beta PIXL-Audit)
endif()

message("*************************************************************")
message("PIXL Renderer v${PROJECT_VERSION} configuration complete")
message("Build target: PIXLRenderer")
if(WIN32 AND POWERSHELL_PATH)
    message("Validation target: PIXL-Audit")
else()
    message("Validation target: unavailable (PowerShell was not found)")
endif()
message("Clean and preloaded live-test archives are produced by:")
message("  tools/StagePixlRendererStandalone.ps1")
message("*************************************************************")
