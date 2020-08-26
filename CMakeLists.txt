project(CuraEngine)
cmake_minimum_required(VERSION 2.8)

if(NOT ${CMAKE_VERSION} VERSION_LESS 3.1)
    set(CMAKE_CXX_STANDARD 11)
else()
    set(CMAKE_CXX_FLAGS "-std=c++11")
endif()

set(ENGINE_VERSION 1.0)

set(CMAKE_BUILD_TYPE RELEASE) #DEBUG or RELEASE

# Add warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wold-style-cast -Wno-old-style-cast -Woverloaded-virtual -Wno-class-memaccess")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -ggdb ")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -fomit-frame-pointer ")

if(NOT APPLE AND NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++")
endif()

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --static")
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR} libs)

#link_directories(${PROJECT_SOURCE_DIR}/libs)

add_library(clipper STATIC libs/clipper/clipper.cpp)

set(ENGINE_SRCS # Except main.cpp.
    src/bridge.cpp
    src/comb.cpp
    #    src/fffProcessor.cpp
    src/gcodeExport.cpp
    src/infill.cpp
    src/inset.cpp
    src/layerPart.cpp
    #    src/multiVolumes.cpp
    src/optimizedModel.cpp
    src/pathOrderOptimizer.cpp
    src/polygonOptimizer.cpp
    src/raft.cpp
    src/settings.cpp
    src/skin.cpp
    src/skirt.cpp
    #    src/sliceDataStorage.cpp
    src/slicer.cpp
    src/support.cpp
    src/timeEstimate.cpp

    src/utils/gettime.cpp
    src/utils/logoutput.cpp
    #    src/utils/polygon.cpp
    src/utils/socket.cpp
    #    src/utils/intpoint.cpp

    src/modelFile/modelFile.cpp
    )

# Compiling Engine itself.
add_library(Engine ${ENGINE_SRCS})

target_link_libraries(Engine clipper)

#set_target_properties(Engine PROPERTIES COMPILE_DEFINITIONS "VERSION=\"${ENGINE_VERSION}\"")
target_compile_definitions(Engine PUBLIC "VERSION=\"${ENGINE_VERSION}\"")

if (UNIX)
    target_link_libraries(Engine pthread)
endif()

if(WIN32)
    target_link_libraries(Engine ws2_32 wsock32)
endif()

# Then compile main.cpp as separate executable, and link the library to it.
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} Engine)

# Installing Engine.
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
include(CPackConfig.cmake)
