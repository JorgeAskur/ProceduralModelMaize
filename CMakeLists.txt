cmake_minimum_required(VERSION 3.16)
project(MaizeGenerator CXX)

# -----------------------------------------------------------------------------
# Maize Procedural Generator — CMake build
#
# Cross-platform build for Windows, Linux, and macOS. The Visual Studio
# project (Maize.sln) remains the primary, fully-tested path on Windows.
#
# File-dialog backend per platform:
#   - Windows : native Win32 common dialog (built in)
#   - Linux   : shells out to `zenity --file-selection` (install zenity)
#   - macOS   : shells out to `osascript` (built in)
#
# Configure (Windows / vcpkg):
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#   cmake --build build --config Release
#
# Configure (Linux):
#   sudo apt install build-essential cmake freeglut3-dev libtinyxml2-dev zenity
#   cmake -B build -S . && cmake --build build -j
#
# Configure (macOS):
#   brew install cmake freeglut tinyxml2
#   cmake -B build -S . && cmake --build build -j
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 16)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user did not pick a config (single-config generators only).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -----------------------------------------------------------------------------
# Dependencies
#
# On Windows we rely on vcpkg-provided CMake config packages
# (FreeGLUT::freeglut, tinyxml2::tinyxml2). On Linux/macOS those packages
# usually come from the system package manager which exports plain Module
# files (FindGLUT.cmake, pkg-config tinyxml2). We try CONFIG mode first and
# fall back to the module/pkg-config path automatically.
# -----------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# freeglut: try CONFIG (vcpkg) → fall back to FindGLUT module (system)
find_package(FreeGLUT CONFIG QUIET)
if(NOT TARGET FreeGLUT::freeglut)
    find_package(GLUT REQUIRED)
    if(NOT TARGET GLUT::GLUT)
        message(FATAL_ERROR "GLUT/freeglut not found. Install freeglut3-dev (Linux) or freeglut (brew/vcpkg).")
    endif()
    add_library(FreeGLUT::freeglut ALIAS GLUT::GLUT)
endif()

# tinyxml2: try CONFIG → pkg-config fallback
find_package(tinyxml2 CONFIG QUIET)
if(NOT TARGET tinyxml2::tinyxml2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)
    add_library(tinyxml2::tinyxml2 ALIAS PkgConfig::TINYXML2)
endif()

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
set(CORE_SOURCES
    Descriptor.cpp
    Maize.cpp
    MaizeExport.cpp
    MaizeGenerator.cpp
    src/math/matrix4d.cpp
    src/math/vect3d.cpp
    src/math/vect4d.cpp
)

set(IMGUI_SOURCES
    src/imgui.cpp
    src/imgui_demo.cpp
    src/imgui_draw.cpp
    src/imgui_impl_glut.cpp
    src/imgui_impl_opengl2.cpp
    src/imgui_stdlib.cpp
    src/imgui_tables.cpp
    src/imgui_widgets.cpp
)

set(APP_SOURCES
    src/main.cpp
)

add_executable(MaizeGenerator
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
    ${APP_SOURCES}
)

# -----------------------------------------------------------------------------
# Include paths (mirrors the .vcxproj layout)
# -----------------------------------------------------------------------------
target_include_directories(MaizeGenerator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/stb_image
    ${CMAKE_SOURCE_DIR}
)

# -----------------------------------------------------------------------------
# Link
# -----------------------------------------------------------------------------
target_link_libraries(MaizeGenerator PRIVATE
    OpenGL::GL
    FreeGLUT::freeglut
    tinyxml2::tinyxml2
)

if(WIN32)
    # GLU + Win32 common-dialog API (file browser).
    target_link_libraries(MaizeGenerator PRIVATE glu32 comdlg32)
elseif(APPLE)
    # macOS GLU is part of the OpenGL framework, already pulled in via OpenGL::GL.
    find_library(COCOA_FRAMEWORK Cocoa)
    if(COCOA_FRAMEWORK)
        target_link_libraries(MaizeGenerator PRIVATE ${COCOA_FRAMEWORK})
    endif()
else()
    # Linux: GLU is a separate library on most distros.
    find_package(OpenGL REQUIRED COMPONENTS OpenGL)
    if(TARGET OpenGL::GLU)
        target_link_libraries(MaizeGenerator PRIVATE OpenGL::GLU)
    else()
        target_link_libraries(MaizeGenerator PRIVATE GLU)
    endif()
endif()

# -----------------------------------------------------------------------------
# Compiler-specific flags
# -----------------------------------------------------------------------------
if(MSVC)
    target_compile_definitions(MaizeGenerator PRIVATE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(MaizeGenerator PRIVATE /W3 /MP)

    # Make the debugger launch from the project root so the app can find
    # shaders/, maize_leaf.png, and the bundled config XML files.
    set_target_properties(MaizeGenerator PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
else()
    target_compile_options(MaizeGenerator PRIVATE -Wall -Wextra)
endif()

# -----------------------------------------------------------------------------
# Runtime data — copy shaders next to the executable so it can be launched
# from the build directory as well as from the project root.
# -----------------------------------------------------------------------------
add_custom_command(TARGET MaizeGenerator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:MaizeGenerator>/shaders"
    COMMENT "Copying shaders/ next to the executable"
)

# Copy the freeglut DLL next to the executable on Windows so the binary runs
# from the build tree without manual setup.
if(WIN32)
    add_custom_command(TARGET MaizeGenerator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FreeGLUT::freeglut>
            $<TARGET_FILE_DIR:MaizeGenerator>
        COMMENT "Copying freeglut.dll next to the executable"
    )
endif()

# -----------------------------------------------------------------------------
# Install rules — `cmake --install build` lays out a runnable directory.
# -----------------------------------------------------------------------------
install(TARGETS MaizeGenerator RUNTIME DESTINATION bin)
install(DIRECTORY shaders/         DESTINATION bin/shaders)
install(FILES
    maize_leaf.png
    maize_stem_texture.png
    maize_generator_config.xml
    small_maize_generator_config.xml
    THIRD_PARTY_LICENSES.md
    README.md
    DESTINATION bin OPTIONAL
)
if(WIN32)
    install(FILES $<TARGET_FILE:FreeGLUT::freeglut> DESTINATION bin)
endif()
