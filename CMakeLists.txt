cmake_minimum_required(VERSION 3.22)
project(obs-ktotakoyben-vi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(OBS_SDK_DIR "" CACHE PATH "Path to OBS SDK containing include/ and lib/")
if(NOT OBS_SDK_DIR)
  message(FATAL_ERROR "Please pass -DOBS_SDK_DIR=/path/to/obs-sdk")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/core/vi-core.hpp
  src/core/vi-core.cpp
  src/sources/smart-frame-source.cpp
  src/sources/live-top-source.cpp
  src/sources/event-popup-source.cpp
  src/ui/control-dock.hpp
  src/ui/control-dock.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
  "${OBS_SDK_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  "${OBS_SDK_DIR}/lib/obs.lib"
  "${OBS_SDK_DIR}/lib/obs-frontend-api.lib"
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  OUTPUT_NAME "${PROJECT_NAME}"
)
