cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR FALSE)

include(compilerconfig)
include(defaults)
include(helpers)
include(ExternalProject)
include(FetchContent)

ExternalProject_ADD(
  rubberband
  GIT_REPOSITORY    https://github.com/breakfastquay/rubberband.git
  GIT_TAG           a19a891b3619ecea00f5c402006fe95b9c4d8a7f
  BUILD_COMMAND     cd ${CMAKE_CURRENT_BINARY_DIR}/rubberband-prefix/src/rubberband && CFLAGS="-fPIC" make -j$(nproc) ARCHFLAGS="-fPIC" -f otherbuilds/Makefile.linux && cd -
  CONFIGURE_COMMAND ""
  INSTALL_COMMAND   ""
)
ExternalProject_ADD(
  asio
  GIT_REPOSITORY    https://github.com/chriskohlhoff/asio.git
  GIT_TAG           asio-1-32-0
  BUILD_COMMAND     ""
  CONFIGURE_COMMAND ""
  INSTALL_COMMAND   ""
)
set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/asio-prefix/src/asio/asio/include")

FetchContent_Declare(
  Crow
  GIT_REPOSITORY    https://github.com/CrowCpp/Crow.git
  GIT_TAG           v1.2.0
)
FetchContent_MakeAvailable(Crow)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE "$${CMAKE_CURRENT_BINARY_DIR}/rubberband-prefix/src/rubberband/lib")
target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}/rubberband-prefix/src/rubberband/rubberband"
  "${CMAKE_CURRENT_BINARY_DIR}/_deps/crow-src/include"
  "${ASIO_INCLUDE_DIR}")

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  Crow
  "${CMAKE_CURRENT_BINARY_DIR}/rubberband-prefix/src/rubberband/lib/librubberband.a")
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fPIC)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
