cmake_minimum_required(VERSION 3.16)
project(bounce_notify VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Single-config generators (Makefiles, Ninja) default to Release unless specified.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

add_executable(bounce-notify src/main.c include/bouncer_proto.h)
target_include_directories(bounce-notify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(bounce-notify PRIVATE BOUNCE_NOTIFY_VERSION="${PROJECT_VERSION}")
add_executable(mock-bouncer-server tests/mock_bouncer_server.c include/bouncer_proto.h)
target_include_directories(mock-bouncer-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  foreach(target bounce-notify mock-bouncer-server)
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      $<$<CONFIG:Debug>:-O0 -g3>
      $<$<CONFIG:Release>:-O3 -DNDEBUG -ffunction-sections -fdata-sections>
      $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
      $<$<CONFIG:MinSizeRel>:-Os -DNDEBUG -ffunction-sections -fdata-sections>
    )

    if(APPLE)
      target_link_options(${target} PRIVATE
        $<$<CONFIG:Release>:-Wl,-dead_strip>
        $<$<CONFIG:MinSizeRel>:-Wl,-dead_strip>
      )
    else()
      target_link_options(${target} PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:MinSizeRel>:-Wl,--gc-sections>
      )
    endif()
  endforeach()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
  foreach(target bounce-notify mock-bouncer-server)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  endforeach()
endif()
