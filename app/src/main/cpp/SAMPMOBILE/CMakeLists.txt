project(SAMPMOBILE)

set(CMAKE_CXX_STANDARD 20)

if (${ANDROID_ABI} STREQUAL "armeabi-v7a")
    add_definitions(-DVER_x32=true)
elseif (${ANDROID_ABI} STREQUAL "arm64-v8a")
    add_definitions(-DVER_x32=false)
endif ()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Deps/spdlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Deps/Opus/include
)

file(GLOB INCLUDE_FILES CONFIGURE_DEPENDS
        *.h
        Game/*.h
        Game/**/*.h
        Java/*.h
        Net/*.h
        UI/*.h
        UI/**/*.h
        UI/**/**/*.h
        Voice/*.h
        Voice/**/*.h
        Voice/**/**/*.h
)

# Core native client, ModLoader, and CleoEngine sources
file(GLOB SOURCE_FILES CONFIGURE_DEPENDS
        *.cpp
        Game/*.cpp
        Game/**/*.cpp
        Java/*.cpp
        Net/*.cpp
        UI/*.cpp
        UI/**/*.cpp
        UI/**/**/*.cpp
        Voice/*.cpp
        Voice/**/*.cpp
        Voice/**/**/*.cpp
)

# omp-cef protocol client (Android port): transport, crypto, session glue and
# vendored third-party C sources (tweetnacl, kcp, tiny-AES-c, BLAKE2 ref).
# CONFIGURE_DEPENDS: new Cef/*.cpp files must be picked up without a clean build.
file(GLOB CEF_SOURCES CONFIGURE_DEPENDS
        Cef/*.cpp
        Cef/thirdparty/*.c
)

add_library(${PROJECT_NAME} SHARED
        ${INCLUDE_FILES}
        ${SOURCE_FILES}
        ${CEF_SOURCES})

add_subdirectory(Memory)
add_subdirectory(Deps)
add_subdirectory(GameSA)

target_link_libraries(${PROJECT_NAME} PRIVATE
        android
        log
        z
        EGL
        GLESv2
        Memory
        GameSA
        imgui
        rapidjson
        RakNet
        Encoding
        opus
        obfuscator
        bass
        spdlog)

# 16KB page size alignment (Android 15+/16 kernels): the built ELF loads natively
# on both 4K and 16KB page devices without relying on the fragile backcompat mode.
if (${ANDROID_ABI} STREQUAL "arm64-v8a")
    target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,-z,max-page-size=16384
            -Wl,-z,common-page-size=16384)
endif ()

target_compile_options(${PROJECT_NAME} PRIVATE
        -O3 # Optimize
        -fexceptions # Enable exceptions
        -fstack-protector # Stack protector
        # High-risk diagnostics on project code (third-party Deps keep -w)
        -Werror=return-type
        -Werror=return-stack-address
        -Werror=mismatched-new-delete
        -Werror=varargs
        -Wuninitialized
        -Wformat
        -Wformat-security
        -Wint-to-pointer-cast
        -Wpointer-to-int-cast
        -Wsizeof-pointer-memaccess
        -Warray-bounds
        -Wliteral-conversion
        -Wpointer-bool-conversion
        -Wparentheses
        -Wnonportable-include-path
        -Winvalid-offsetof
        # Silence noisy legacy patterns
        -Wno-writable-strings
        -Wno-null-conversion
        -Wno-null-arithmetic
        -Wno-deprecated-enum-enum-conversion
        -Wno-switch
#        -fvisibility=hidden # Hide symbols
#        -fvisibility-inlines-hidden # Hide inline symbols
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RAKSAMP_CLIENT)