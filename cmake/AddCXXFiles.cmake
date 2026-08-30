function(add_cxx_files TARGET)
	file(GLOB_RECURSE INCLUDE_FILES
		LIST_DIRECTORIES false
		CONFIGURE_DEPENDS
		"include/*.h"
		"include/*.hpp"
		"include/*.hxx"
		"include/*.inl"
	)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include
		PREFIX "Header Files"
		FILES ${INCLUDE_FILES})

	target_sources("${TARGET}" PUBLIC ${INCLUDE_FILES})

	file(GLOB_RECURSE HEADER_FILES
		LIST_DIRECTORIES false
		CONFIGURE_DEPENDS
		"engine/*.h"
		"engine/*.hpp"
		"engine/*.hxx"
		"engine/*.inl"
	)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine
		PREFIX "Header Files"
		FILES ${HEADER_FILES})

	target_sources("${TARGET}" PRIVATE ${HEADER_FILES})

	file(GLOB_RECURSE SOURCE_FILES
		LIST_DIRECTORIES false
		CONFIGURE_DEPENDS
		"engine/*.cpp"
		"engine/*.cxx"
	)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine
		PREFIX "Source Files"
		FILES ${SOURCE_FILES})

	target_sources("${TARGET}" PRIVATE ${SOURCE_FILES})

	file(GLOB_RECURSE HLSL_FILES
		LIST_DIRECTORIES false
		CONFIGURE_DEPENDS
		"pipeline/*/Kernels/*.hlsl"
		"pipeline/*/Kernels/*.hlsli"
		"distribution/Shaders/*.hlsl"
		"distribution/Shaders/*.hlsli"
	)

	set(HLSL_FILES ${HLSL_FILES} PARENT_SCOPE)

	list(APPEND CPP_SOURCES ${HEADER_FILES})
	list(APPEND CPP_SOURCES ${SOURCE_FILES})
	set(CPP_SOURCES ${CPP_SOURCES} PARENT_SCOPE)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/
		PREFIX "HLSL Files"
		FILES ${HLSL_FILES})

	set_source_files_properties(${HLSL_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")

	target_sources("${TARGET}" PRIVATE ${HLSL_FILES})
endfunction()
