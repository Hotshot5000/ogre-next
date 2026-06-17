#-------------------------------------------------------------------
# This file is part of the CMake build system for OGRE-Next
#     (Object-oriented Graphics Rendering Engine)
# For the latest info, see http://www.ogre3d.org/
#
# The contents of this file are placed in the public domain. Feel
# free to make use of it in any way you like.
#-------------------------------------------------------------------

# Configure Metal RenderSystem build

if (OGRE_BUILD_PLATFORM_APPLE_IOS)
set(OS "iOS")
else()
set(OS "OSX")
endif()

file(GLOB HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/Vao/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/Windowing/${OS}/*.h")
file(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm" "${CMAKE_CURRENT_SOURCE_DIR}/src/Vao/*.mm" "${CMAKE_CURRENT_SOURCE_DIR}/src/Windowing/${OS}/*.mm")

include_directories(
  BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include/Windowing/${OS}
)

ogre_add_library(RenderSystem_Metal ${OGRE_LIB_TYPE} ${HEADER_FILES} ${SOURCE_FILES})
if (OGRE_BUILD_PLATFORM_APPLE_IOS)
target_link_libraries(RenderSystem_Metal ${OGRE_NEXT}Main "-framework Metal" "-framework MetalFX" "-framework QuartzCore")
else()
target_link_libraries(RenderSystem_Metal ${OGRE_NEXT}Main "-framework Metal" "-framework MetalFX" "-framework AppKit" "-framework QuartzCore")
endif()

set( OGRE_METAL_HAS_OIDN 0 )
if( NOT OGRE_BUILD_PLATFORM_APPLE_IOS )
  find_path( OIDN_INCLUDE_DIR
             NAMES OpenImageDenoise/oidn.hpp
             HINTS /opt/homebrew/include )
  find_library( OIDN_LIBRARY
                NAMES OpenImageDenoise
                HINTS /opt/homebrew/lib )

  if( OIDN_INCLUDE_DIR AND OIDN_LIBRARY )
    target_include_directories( RenderSystem_Metal PRIVATE ${OIDN_INCLUDE_DIR} )
    target_link_libraries( RenderSystem_Metal ${OIDN_LIBRARY} )
    set( OGRE_METAL_HAS_OIDN 1 )
  endif()
endif()

target_compile_options(RenderSystem_Metal PRIVATE "-fobjc-arc")
target_compile_definitions( RenderSystem_Metal PRIVATE OGRE_METAL_HAS_OIDN=${OGRE_METAL_HAS_OIDN} )
set_target_properties(RenderSystem_Metal PROPERTIES LINK_FLAGS "-fobjc-arc")

if (OGRE_CONFIG_THREADS)
  target_link_libraries(RenderSystem_Metal ${OGRE_THREAD_LIBRARIES})
endif ()

ogre_config_framework(RenderSystem_Metal)

ogre_config_plugin(RenderSystem_Metal)
install(FILES ${HEADER_FILES} DESTINATION include/${OGRE_NEXT_PREFIX}/RenderSystems/Metal)
install(FILES ${PLATFORM_HEADERS} DESTINATION include/${OGRE_NEXT_PREFIX}/RenderSystems/Metal/${PLATFORM_HEADER_INSTALL})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include DESTINATION include/${OGRE_NEXT_PREFIX}/RenderSystems/Metal)
