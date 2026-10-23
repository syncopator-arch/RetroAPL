# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Keith Doyle

cmake_minimum_required(VERSION 3.16)
project(RetroAPL VERSION 1.0 LANGUAGES CXX)

# The program's file name: retroapl on Linux, retroapl.exe on Windows.
# (Before V106 it was APLTerminal.) On Linux it is also the windows'
# WM_CLASS, which matches the menu entry, retroapl.desktop.
set(PROGRAM retroapl)

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
# libapl.a must be built first: build.sh / build.bat in the repository
# root build everything in order, or run `make` in ../backend.
set(BACKEND_DIR "${CMAKE_SOURCE_DIR}/../backend")

# Make sure the static lib exists before we try to link it
if(NOT EXISTS "${BACKEND_DIR}/libapl.a")
    message(FATAL_ERROR
        "libapl.a not found in ${BACKEND_DIR} -- "
        "build with build.sh (Linux) or build.bat (Windows) in the "
        "repository root, or run 'make' in the backend directory first.")
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
    message(FATAL_ERROR
        "libseq.a not found in ${SEQUENCER_DIR} -- "
        "build with build.sh (Linux) or build.bat (Windows) in the "
        "repository root, or run 'make libseq.a' in the sequencer "
        "directory first. (The backend's 'make libapl.a' doesn't build it.)")
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
add_executable(${PROGRAM} WIN32
    ${SOURCES}
    ${HEADERS}
)

# ── Icon ──────────────────────────────────────────────────────────────
# Windows: retroapl.rc builds retroapl.ico into the .exe: Explorer,
# shortcuts and the taskbar show it, and Qt uses it for its windows.
# Linux: retroapl.qrc builds the same .ico in as a Qt resource, and
# main() sets it as the window icon. The menu entry's icon is
# setup-aplhome.sh's job.
if(WIN32)
    enable_language(RC)
    target_sources(${PROGRAM} PRIVATE retroapl.rc)
else()
    target_sources(${PROGRAM} PRIVATE retroapl.qrc)
endif()

# ── Compile definitions ───────────────────────────────────────────────
# AS_DLL selects the dll_startup/exec_apl_line path in main.c
# NO_INPUT_PROC tells the backend to use the direct-buffer input path
# UNIX is required by the backend headers
target_compile_definitions(${PROGRAM} PRIVATE
    AS_DLL
    NO_INPUT_PROC
	$<IF:$<BOOL:${WIN32}>,WINDOWS,UNIX>
)

# ── Link libraries ────────────────────────────────────────────────────
target_link_libraries(${PROGRAM} PRIVATE
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
    target_compile_options(${PROGRAM} PRIVATE
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
    target_compile_options(${PROGRAM} PRIVATE -g)
endif()

# ── Install ───────────────────────────────────────────────────────────
install(TARGETS ${PROGRAM}
    RUNTIME DESTINATION bin
)
install(FILES "${CMAKE_SOURCE_DIR}/../fonts/kapl.ttf"
    DESTINATION share/${PROGRAM}/fonts
)
# Note: the program loads the font only from $APLHOME/fonts/kapl.ttf,
# so this install isn't how RetroAPL is normally set up: use
# setup-aplhome.sh / setup-aplhome.bat in the repository root instead.

# =============================================================================
# Build instructions
# =============================================================================
#
# The usual way: build.sh (Linux) or build.bat (Windows) in the
# repository root, which build all three parts in order. By hand:
#
# 1. The sequencer library, then the interpreter:
#      make -C ../sequencer libseq.a
#      make -C ../backend
#    (the backend's default target builds libapl.a and apl_cli, and
#    runs the sequencer's make itself; `make libapl.a` alone doesn't)
#
# 2. The front end:
#      cmake -S . -B build
#      cmake --build build
#
# 3. Set up $APLHOME (font, help files, workspaces) and install the
#    program there: ../setup-aplhome.sh. The program reads everything,
#    the font included, from $APLHOME, not from this directory.
#
# =============================================================================
