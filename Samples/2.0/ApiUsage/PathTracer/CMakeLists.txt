#-------------------------------------------------------------------
# This file is part of the CMake build system for OGRE-Next
#     (Object-oriented Graphics Rendering Engine)
# For the latest info, see http://www.ogre3d.org/
#
# The contents of this file are placed in the public domain. Feel
# free to make use of it in any way you like.
#-------------------------------------------------------------------

macro( add_recursive dir retVal )
    file( GLOB_RECURSE ${retVal} ${dir}/*.h ${dir}/*.cpp ${dir}/*.c )
endmacro()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${CMAKE_SOURCE_DIR}/Components/Hlms/Common/include)
ogre_add_component_include_dir(Hlms/Pbs)

if( APPLE )
    set( PATH_TRACING_METAL_FILES
         ${CMAKE_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/PathTracing/Metal/PathTracingBuildAsInstances.metal
         ${CMAKE_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/PathTracing/Metal/PathTracingCopy_ps.metal
         ${CMAKE_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/PathTracing/Metal/PathTracingFrameGenerationComposite.metal
         ${CMAKE_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/PathTracing/Metal/PathTracingResolve_ps.metal
         ${CMAKE_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/PathTracing/Metal/PathTracingTrace.metal )
    set_source_files_properties( ${PATH_TRACING_METAL_FILES}
        PROPERTIES
            LANGUAGE METAL
            XCODE_EXPLICIT_FILE_TYPE sourcecode.metal )
endif()

add_recursive( ./ SOURCE_FILES )

ogre_add_executable(Sample_PathTracer WIN32 MACOSX_BUNDLE ${SOURCE_FILES} ${PATH_TRACING_METAL_FILES} ${SAMPLE_COMMON_RESOURCES})

target_link_libraries(Sample_PathTracer ${OGRE_LIBRARIES} ${OGRE_SAMPLES_LIBRARIES})
ogre_config_sample_lib(Sample_PathTracer)
ogre_config_sample_pkg(Sample_PathTracer)
