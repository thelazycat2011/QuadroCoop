# Why am I doing this
I saw this mod being posted on Geode SDK for being fully vibecoded. Just to see how bad is it I forked it (as you can see here) and try to see how far can I get it to compile.  
But after a while the codebase is so broken and incomplete I decided to leave it here as an example of what not to do.

# Object 1 : CMakeLists.txt
Exclude the commentesque of the file itself, the problem is this.
```
# Package everything into a .geode archive.
# Must be called AFTER target_link_libraries.
create_geode_file(${PROJECT_NAME})
```

The thing is, this function does not exist, the entire file dont follow standard CMakeLists.txt for Geode mod.  
The correct function in this case is `setup_geode_mod(${PROJECT_NAME})`. A normal CMakeLists.txt for a Geode mod should look more or less like this (taken from my mod [SwapX](https://github.com/thelazycat2011/SwapX))  
```
cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(SwapX VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
CPMAddPackage("gh:matcool/gd-imgui-cocos#9764333")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

setup_geode_mod(${PROJECT_NAME})

```

# Object 2 : main.cpp
To be continued
