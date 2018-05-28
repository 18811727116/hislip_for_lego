cmake_minimum_required(VERSION 2.8.11)

project(lego)

# echo compile commadline
set(CMAKE_VERBOSE_MAKEFILE ON)


# x86 simulation
set(CMAKE_SYSTEM_NAME Linux)
set(Qt5_DIR /home/sailing/Qt5.7.1/5.7/gcc_64/)
set(OE_QMAKE_PATH_EXTERNAL_HOST_BINS /home/sailing/Qt5.7.1/5.7/gcc_64/bin)
set(Qt5Core_DIR ${Qt5_DIR}/lib/cmake/Qt5Core)
set(Qt5Widgets_DIR ${Qt5_DIR}/lib/cmake/Qt5Widgets)
set(Qt5Network_DIR ${Qt5_DIR}/lib/cmake/Qt5Network)


add_compile_options(-Wall)
add_compile_options(-g)
add_compile_options(-O0)


set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${EXECUTABLE_OUTPUT_PATH}/lib)


# C++11
if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
endif(CMAKE_COMPILER_IS_GNUCXX)


# qt
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)


# c++ boost
add_compile_options(-DBOOST_LOG_DYN_LINK)
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.54 REQUIRED)
find_package(Boost COMPONENTS log_setup log thread date_time  system REQUIRED)

include_directories( ${Boost_INCLUDE_DIRS} ${Qt5Core_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS} ${Qt5Network_INCLUDE_DIRS} )
LINK_DIRECTORIES(
  ${Boost_LIBRARY_DIR}
  ${LIBRARY_OUTPUT_PATH}
  )

# thread
find_package( Threads )


# source
ADD_EXECUTABLE(server
  ./main.cpp
)


TARGET_LINK_LIBRARIES(server ${Boost_LIBRARIES}  Qt5::Core Qt5::Widgets Qt5::Core Qt5::Network ${CMAKE_THREAD_LIBS_INIT} "-lrt -ldl -Wl,-rpath,./lib")


