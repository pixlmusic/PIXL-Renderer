include_guard(GLOBAL)

function(pixl_add_portable_test TARGET SOURCE)
    add_executable(${TARGET} EXCLUDE_FROM_ALL ${SOURCE} ${ARGN})
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    target_include_directories(${TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/engine")
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ${TARGET} COMMAND $<TARGET_FILE:${TARGET}>)
    set_tests_properties(${TARGET} PROPERTIES LABELS "portable;cpp")
endfunction()

if(BUILD_TESTING)
    pixl_add_portable_test(PIXLTestCameraReprojection
        "${CMAKE_SOURCE_DIR}/tools/TestCameraReprojection.cpp")
    if(WIN32)
        pixl_add_portable_test(PIXLTestDofTiles "${CMAKE_SOURCE_DIR}/tools/TestDofTiles.cpp")
        target_link_libraries(PIXLTestDofTiles PRIVATE d3d11 d3dcompiler)
        set_tests_properties(PIXLTestDofTiles PROPERTIES LABELS "shader;warp" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        pixl_add_portable_test(PIXLTestModernShaderMath
            "${CMAKE_SOURCE_DIR}/tools/TestModernShaderMath.cpp")
        target_link_libraries(PIXLTestModernShaderMath PRIVATE d3d11 d3dcompiler)
        set_tests_properties(PIXLTestModernShaderMath PROPERTIES LABELS "shader;warp")
        set_property(TEST PIXLTestModernShaderMath PROPERTY WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        pixl_add_portable_test(PIXLTestCompilationArtifacts
            "${CMAKE_SOURCE_DIR}/tools/TestCompilationArtifacts.cpp")
        target_link_libraries(PIXLTestCompilationArtifacts PRIVATE d3dcompiler bcrypt)
        set(PIXL_WINDOWS_MODERNISATION_TESTS PIXLTestCompilationArtifacts PIXLTestModernShaderMath PIXLTestDofTiles)
    endif()
	pixl_add_portable_test(PIXLTestReactiveFXSpawnSafety
		"${CMAKE_SOURCE_DIR}/tools/TestReactiveFXSpawnSafety.cpp")
    pixl_add_portable_test(PIXLTestRenderOrigin
        "${CMAKE_SOURCE_DIR}/tools/TestRenderOrigin.cpp")
    pixl_add_portable_test(PIXLTestContainedLiquidMath
        "${CMAKE_SOURCE_DIR}/tools/TestContainedLiquidMath.cpp")
    pixl_add_portable_test(PIXLTestHybridGIMath
        "${CMAKE_SOURCE_DIR}/tools/TestHybridGIMath.cpp")
    pixl_add_portable_test(PIXLTestHybridReflectionHiZ
        "${CMAKE_SOURCE_DIR}/tools/TestHybridReflectionHiZ.cpp")
    pixl_add_portable_test(PIXLTestAtmosphereWeather
        "${CMAKE_SOURCE_DIR}/tools/TestAtmosphereWeather.cpp")
    pixl_add_portable_test(PIXLTestModulePolicies
        "${CMAKE_SOURCE_DIR}/tools/TestPhase2ModuleInternals.cpp"
        "${CMAKE_SOURCE_DIR}/engine/Modules/CameraSuite/CameraPolicy.cpp"
        "${CMAKE_SOURCE_DIR}/engine/Modules/GroundResponse/SurfaceClassifier.cpp"
        "${CMAKE_SOURCE_DIR}/engine/Modules/HybridGI/TemporalPolicy.cpp")
    pixl_add_portable_test(PIXLTestRendererMetadata
        "${CMAKE_SOURCE_DIR}/tools/TestRendererMetadata.cpp")

    find_program(PIXL_TEST_POWERSHELL NAMES pwsh powershell)
    if(WIN32 AND PIXL_TEST_POWERSHELL)
        foreach(_contract IN ITEMS RenderPassScheduler GPUResourceServices HookRegistry RendererMetadata LightTransportWorld ModuleRulesCache TemporalContext PixelAnnotations ReconstructionContext ReconstructionSafety GPUWorkloadBudgeter ReflectionContext VolumetricContext)
            add_test(
                NAME PIXLContract${_contract}
                COMMAND ${PIXL_TEST_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_SOURCE_DIR}/tools/Test${_contract}.ps1")
            set_tests_properties(PIXLContract${_contract} PROPERTIES LABELS "portable;contract")
        endforeach()

        add_test(
            NAME PIXLShaderRenderOrigin
            COMMAND ${PIXL_TEST_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/TestRenderOriginShaders.ps1")
        set_tests_properties(PIXLShaderRenderOrigin PROPERTIES LABELS "portable;shader;fxc")

        add_test(
            NAME PIXLShaderVisibility
            COMMAND ${PIXL_TEST_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tools/TestVisibilityShaders.ps1")
        set_tests_properties(PIXLShaderVisibility PROPERTIES LABELS "portable;shader;fxc")

        if(TARGET PIXLRenderer)
            add_test(
                NAME PIXLRepositoryAudit
                COMMAND ${PIXL_TEST_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_SOURCE_DIR}/tools/AuditPixlRenderer.ps1"
                    -BuildDirectory "$<TARGET_FILE_DIR:PIXLRenderer>")
            set_tests_properties(PIXLRepositoryAudit PROPERTIES LABELS "full-build;audit")
        endif()
    endif()

    add_custom_target(PIXL-Portable-Tests
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}"
            -C $<CONFIG> --output-on-failure -L portable
        DEPENDS
            ${PIXL_WINDOWS_MODERNISATION_TESTS}
            PIXLTestCameraReprojection
            PIXLTestReactiveFXSpawnSafety
            PIXLTestRenderOrigin
            PIXLTestContainedLiquidMath
            PIXLTestHybridGIMath
            PIXLTestHybridReflectionHiZ
            PIXLTestAtmosphereWeather
            PIXLTestModulePolicies
            PIXLTestRendererMetadata
        USES_TERMINAL
        COMMENT "Running PIXL portable CTest validation")
endif()
