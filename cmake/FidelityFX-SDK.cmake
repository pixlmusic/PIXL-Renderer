set(FFX_API_VK OFF)
set(FFX_API_DX12 OFF)
set(FFX_ALL OFF)
set(FFX_FSR3 ON)
set(FFX_FSR ON)
set(FFX_AUTO_COMPILE_SHADERS 1)
set(
  PIXL_FFX_SHORT_BINARY_ROOT
  "$ENV{PIXL_FFX_SHORT_BINARY_ROOT}"
  CACHE PATH
  "Optional short alias of CMAKE_BINARY_DIR for FidelityFX_SC on long Windows paths"
)

# Note: extern/FidelityFX-SDK/sdk/CMakeLists.txt detects x64 via an exact
# STREQUAL on CMAKE_EXE_LINKER_FLAGS == "/machine:x64" when
# CMAKE_GENERATOR_PLATFORM is unset (Ninja). The ninja preset sets exactly
# that value; appending anything else to the variable breaks the configure.

# Keep the public FidelityFX submodule pinned to its upstream revision while
# applying PIXL's reproducible Windows path-length repair. A source checkout may
# already contain the patch (for example the active developer tree), so accept
# either a cleanly applicable patch or a clean reverse check.
find_package(Git REQUIRED QUIET)
set(
  _pixl_ffx_patch
  "${CMAKE_SOURCE_DIR}/cmake/patches/FidelityFX-DX11-Short-Output.patch"
)
set(_pixl_ffx_source "${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --check "${_pixl_ffx_patch}"
  WORKING_DIRECTORY "${_pixl_ffx_source}"
  RESULT_VARIABLE _pixl_ffx_patch_check
  OUTPUT_QUIET
  ERROR_QUIET
)
if(_pixl_ffx_patch_check EQUAL 0)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${_pixl_ffx_patch}"
    WORKING_DIRECTORY "${_pixl_ffx_source}"
    RESULT_VARIABLE _pixl_ffx_patch_apply
  )
  if(NOT _pixl_ffx_patch_apply EQUAL 0)
    message(FATAL_ERROR "Failed to apply PIXL's FidelityFX DX11 build patch")
  endif()
else()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_pixl_ffx_patch}"
    WORKING_DIRECTORY "${_pixl_ffx_source}"
    RESULT_VARIABLE _pixl_ffx_reverse_check
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(NOT _pixl_ffx_reverse_check EQUAL 0)
    message(
      FATAL_ERROR
      "FidelityFX source is neither clean nor compatible with PIXL's required DX11 build patch"
    )
  endif()
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk)

# Upstream bug: the FFX dx11 backend's compile_shaders() leaks literal
# out-variable names (e.g. "FSR2_PERMUTATION_OUTPUTS") into the dependency
# list of the phony ffx_shader_permutations_dx11 target. The VS generator
# ignores the bogus deps; Ninja fails with "missing and no known rule".
# Pre-creating empty placeholder files at those paths satisfies Ninja without
# triggering rebuilds. If an SDK update adds a new leaked name, add it here.
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_ffx_dx11_bindir
      "${CMAKE_BINARY_DIR}/extern/FidelityFX-SDK/sdk/src/backends/dx11"
  )
  foreach(
    _ffx_bogus_dep
    FSR1_PERMUTATION_OUTPUTS
    FSR2_PERMUTATION_OUTPUTS
    FSR3UPSCALER_PERMUTATION_OUTPUTS
    FRAMEINTERPOLATION_PERMUTATION_OUTPUTS
    OPTICALFLOW_PERMUTATION_OUTPUTS
  )
    if(NOT EXISTS "${_ffx_dx11_bindir}/${_ffx_bogus_dep}")
      file(
        WRITE "${_ffx_dx11_bindir}/${_ffx_bogus_dep}"
        "placeholder for upstream FFX CMake dependency-name leak; see cmake/FidelityFX-SDK.cmake\n"
      )
    endif()
  endforeach()
endif()

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  ffx_backend_dx11_x64
  ffx_fsr3_x64
)
