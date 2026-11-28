project(Encoding)

file(GLOB INCLUDE_FILES
        include/*.h
)

file(GLOB SOURCE_FILES
        encoding.cpp
)

add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE
        ${INCLUDE_FILES}
        ${SOURCE_FILES})

set_source_files_properties(${SOURCE_FILES} TARGET_DIRECTORY SAMPMOBILE PROPERTIES COMPILE_OPTIONS "-w")


set(iconv_path "${CMAKE_CURRENT_SOURCE_DIR}/lib/${ANDROID_ABI}/libiconv.a")
add_library(iconv STATIC IMPORTED)
set_target_properties(iconv PROPERTIES IMPORTED_LOCATION ${iconv_path})

set(charset_path "${CMAKE_CURRENT_SOURCE_DIR}/lib/${ANDROID_ABI}/libcharset.a")
add_library(charset STATIC IMPORTED)
set_target_properties(charset PROPERTIES IMPORTED_LOCATION ${charset_path})

target_link_libraries(${PROJECT_NAME} INTERFACE
        iconv
        charset
)

target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
