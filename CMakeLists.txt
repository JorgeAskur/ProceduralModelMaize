cmake_minimum_required(VERSION 3.10.0)
project(maize VERSION 0.1.0 LANGUAGES C CXX)
add_compile_options(-fpermissive)

# find conda version of freeglut, GL, and GLU
set(OpenGL_GL_PREFERENCE "GLVND")
set(OpenGL_GL_LIBRARY   "$ENV{CONDA_PREFIX}/lib/libOpenGL.so.0")
set(OpenGL_GLU_LIBRARY  "$ENV{CONDA_PREFIX}/lib/libGLU.so")

find_library(FREEGLUT_LIBRARY NAMES glut freeglut)
find_path(FREEGLUT_INCLUDE NAMES GL/freeglut.h)
message(STATUS "Using freeglut library: ${FREEGLUT_LIBRARY}")
message(STATUS "Using freeglut include: ${FREEGLUT_INCLUDE}")

find_library(TINYXML_LIBRARY NAMES tinyxml2)
message(STATUS "Using tinyxml library: ${TINYXML_LIBRARY}")

file(GLOB SRC ./*.cpp src/*.cpp src/math/*.cpp)

add_executable(maize ${SRC})
message(STATUS "source files: ${SRC}")

target_include_directories(maize PRIVATE
    ${FREEGLUT_INCLUDE}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/ImGUI
    ${CMAKE_SOURCE_DIR}/include/math
    ${CMAKE_SOURCE_DIR}/include/stb_image
)

target_link_libraries(maize PRIVATE
    ${FREEGLUT_LIBRARY}
    ${TINYXML_LIBRARY}
    ${OpenGL_GL_LIBRARY}
    ${OpenGL_GLU_LIBRARY}
)