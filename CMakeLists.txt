cmake_minimum_required(VERSION 3.16)
project(mcgl C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- GLFW (окно/ввод) — собирается из исходников ----
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)

# ---- GLM (математика, header-only) ----
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

# ---- stb (шрифты/пнг, header-only) ----
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(stb)

# ---- glad2 (загрузчик OpenGL 4.3 core, генерируется на лету) ----
FetchContent_Declare(glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v2.0.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl STATIC API gl:core=4.3)

add_executable(game src/main.cpp)

target_include_directories(game PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(game PRIVATE glfw glad_gl glm::glm m pthread)
target_compile_options(game PRIVATE -O2 -Wall -Wno-unused-function)
