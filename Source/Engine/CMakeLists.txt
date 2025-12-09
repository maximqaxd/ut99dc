project(Engine CXX)

file(GLOB SRC_FILES "Src/*.cpp")

if(LOW_MEMORY)
  list(FILTER SRC_FILES EXCLUDE REGEX "UnPath")
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

if(TARGET_IS_DREAMCAST AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  # FIXME: the rest is built with -Os, but building this file with -Os causes lightmaps to break
  set_source_files_properties("Src/UnActCol.cpp" PROPERTIES COMPILE_FLAGS "-O3 -ffast-math -ffp-contract=fast")
  set_source_files_properties("Src/UnCamera.cpp" PROPERTIES COMPILE_FLAGS "-O3 -ffast-math -ffp-contract=fast")
  set_source_files_properties("Src/UnFPoly.cpp" PROPERTIES COMPILE_FLAGS "-O3 -ffast-math -ffp-contract=fast")
  set_source_files_properties("Src/UnTrace.cpp" PROPERTIES COMPILE_FLAGS "-O3 -ffast-math -ffp-contract=fast")
endif()

target_link_libraries(${PROJECT_NAME} Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE ENGINE_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
