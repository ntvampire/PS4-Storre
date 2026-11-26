cmake_minimum_required(VERSION 3.20)
project(ps4_storre C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# Source files
set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/catalog/catalog.cpp
    ${SRC_DIR}/network/http_client.cpp
    ${SRC_DIR}/network/orchestrator.cpp
    ${INC_DIR}/cJSON.c
)

include_directories(
    ${INC_DIR}
    ${SRC_DIR}
)

if(WIN32 AND NOT PS4)
    # ========================================================
    # Windows Host Build (MinGW / w64devkit)
    # ========================================================
    message(STATUS "Configuring PS4 Storre for Windows Host...")

    set(SDL2_BASE "D:/Projects/Tools/SDL2")
    set(SDL2_DIR "${SDL2_BASE}/SDL2-2.30.10/x86_64-w64-mingw32")
    set(SDL2_IMAGE_DIR "${SDL2_BASE}/SDL2_image-2.8.2/x86_64-w64-mingw32")
    set(SDL2_TTF_DIR "${SDL2_BASE}/SDL2_ttf-2.22.0/x86_64-w64-mingw32")

    include_directories(
        ${SDL2_DIR}/include/SDL2
        ${SDL2_IMAGE_DIR}/include/SDL2
        ${SDL2_TTF_DIR}/include/SDL2
    )

    link_directories(
        ${SDL2_DIR}/lib
        ${SDL2_IMAGE_DIR}/lib
        ${SDL2_TTF_DIR}/lib
    )

    add_executable(ps4_storre ${SOURCES})

    target_link_libraries(ps4_storre
        mingw32
        SDL2main
        SDL2
        SDL2_image
        SDL2_ttf
        ws2_32
    )

    # Post-build: Copy DLLs and Assets to output dir
    add_custom_command(TARGET ps4_storre POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DIR}/bin/SDL2.dll" "$<TARGET_FILE_DIR:ps4_storre>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL2_IMAGE_DIR}/bin" "$<TARGET_FILE_DIR:ps4_storre>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL2_TTF_DIR}/bin" "$<TARGET_FILE_DIR:ps4_storre>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_DIR}" "$<TARGET_FILE_DIR:ps4_storre>/assets"
        COMMENT "Copying runtime DLLs and assets to binary output directory"
    )

elseif(PS4)
    # ========================================================
    # PS4 OpenOrbis Target Build
    # ========================================================
    message(STATUS "Configuring PS4 Storre for Sony PlayStation 4 (OpenOrbis)...")

    set(OO_DIR "$ENV{OO_PS4_TOOLCHAIN}")
    if(NOT OO_DIR)
        set(OO_DIR "D:/Projects/Tools/OpenOrbis")
    endif()

    include_directories(
        ${OO_DIR}/include
        ${OO_DIR}/include/SDL2
    )

    link_directories(${OO_DIR}/lib)

    add_executable(ps4_storre ${SOURCES})

    target_link_libraries(ps4_storre
        SDL2
        SDL2_ttf
        SDL2_image
        freetype
        png
        z
        kernel
        ScePad
        SceVideoOut
        SceSysmodule
        SceIme
        SceUserService
    )
endif()
