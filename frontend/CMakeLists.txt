cmake_minimum_required(VERSION 3.16)
project(APLTerminal VERSION 1.0 LANGUAGES CXX)

# Qt6 requires C++17 minimum
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    PrintSupport
)

# ── Backend integration ───────────────────────────────────────────────
# Backend source tree is a sibling directory of this frontend tree.
# libapl.a must be built first with: cd ../backend && make libapl.a
set(BACKEND_DIR "${CMAKE_SOURCE_DIR}/../backend")

# Make sure the static lib exists before we try to link it
if(NOT EXISTS "${BACKEND_DIR}/libapl.a")
    message(WARNING
        "libapl.a not found in ${BACKEND_DIR} -- "
        "run 'make libapl.a' in the backend directory before building the frontend.")
endif()

# Headers: platform.h, externs.h, globals.h, depend.h, etc.
include_directories("${BACKEND_DIR}")

# ── MIDI sequencer library ────────────────────────────────────────────
# libapl.a's seqfuncs.c (the ⎕SEQ functions) calls it. Built by the
# backend's make (which runs `make libseq.a` in ../sequencer), or there
# directly. The front end includes none of its headers: stopEngine()
# reaches it through seqf_close() in libapl.a.
set(SEQUENCER_DIR "${CMAKE_SOURCE_DIR}/../sequencer")
if(NOT EXISTS "${SEQUENCER_DIR}/libseq.a")
    message(WARNING
        "libseq.a not found in ${SEQUENCER_DIR} -- "
        "run 'make' in the backend directory (or 'make libseq.a' in the "
        "sequencer directory) before building the frontend.")
endif()

# The sequencer's playback thread uses POSIX threads on both platforms
# (glibc; winpthreads in the Qt kit's MinGW).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ── Source files ──────────────────────────────────────────────────────
set(SOURCES
    main.cpp
    APLTerminalWidget.cpp
    graphwindow.cpp
    graphmanager.cpp
    graph_bridge.cpp
)

set(HEADERS
    APLTerminalWidget.h
    mappings.hpp
    graphwindow.h
    graphmanager.h
)

# ── Executable ────────────────────────────────────────────────────────
# WIN32 keyword: builds as a GUI-subsystem app on Windows (no console window).
# Ignored entirely on Linux/Unix -- no effect on that build.
add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
)

# ── Compile definitions ───────────────────────────────────────────────
# AS_DLL selects the dll_startup/exec_apl_line path in main.c
# NO_INPUT_PROC tells the backend to use the direct-buffer input path
# UNIX is required by the backend headers
target_compile_definitions(${PROJECT_NAME} PRIVATE
    AS_DLL
    NO_INPUT_PROC
	$<IF:$<BOOL:${WIN32}>,WINDOWS,UNIX>
)

# ── Link libraries ────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
    "${BACKEND_DIR}/libapl.a"
    "${SEQUENCER_DIR}/libseq.a"
    Threads::Threads
	$<$<NOT:$<BOOL:${WIN32}>>:m>
	$<$<BOOL:${WIN32}>:bcrypt>
	$<$<BOOL:${WIN32}>:winmm>
)

# ── Warnings ──────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        # Suppress warnings that originate from backend headers included
        # into main.cpp — these are C idioms not valid concerns in this context
        -Wno-unused-parameter
        -Wno-pedantic
    )
endif()

# ── Debug symbols ─────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(${PROJECT_NAME} PRIVATE -g)
endif()

# ── Install ───────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
install(FILES kapl.ttf
    DESTINATION share/${PROJECT_NAME}/fonts
)

# =============================================================================
# Build instructions
# =============================================================================
#
# 1. Build the backend static library and the sequencer library first:
#      cd ../backend
#      make libapl.a
#      make -C ../sequencer libseq.a
#    (a plain `make` in the backend does both, and builds apl_cli)
#
# 2. Build the frontend:
#      cd ../frontend
#      mkdir build && cd build
#      cmake ..
#      make
#
# 3. Run from the frontend directory (so kapl.ttf and FNS/ are found):
#      cd ..
#      ./build/APLTerminal
#
# =============================================================================
