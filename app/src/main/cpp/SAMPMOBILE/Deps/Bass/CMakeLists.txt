project(bass)

file(GLOB INCLUDE_FILES
        Bass.h
        Bass_fx.h
)
file(GLOB SOURCE_FILES
        bass.cpp
)

add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE
        ${INCLUDE_FILES}
        ${SOURCE_FILES}
)

set_source_files_properties(${SOURCE_FILES} TARGET_DIRECTORY SAMPMOBILE PROPERTIES COMPILE_OPTIONS "-w")

target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
