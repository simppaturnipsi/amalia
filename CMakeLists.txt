cmake_minimum_required(VERSION 3.24)
project(Amalia VERSION 2026.08.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(AMALIA_BUILD_DESKTOP "Build Amalia Desktop" ON)
option(AMALIA_BUILD_LAUNCHER "Build Amalia Launcher" ON)

if(AMALIA_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()
if(AMALIA_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()
