cmake_minimum_required(VERSION 3.21.1)

option(ENABLE_VCPKG "Enable the vcpkg package manager" ON)

if (ENABLE_VCPKG)
	message("Setting Dynamic C/CXX linking")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	message("Using vcpkg")
	if (CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
		# Force static MD triplet on WindowsStore
		set(VCPKG_TARGET_TRIPLET "x64-uwp-static-md" CACHE STRING "" FORCE)
	else()
		set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "" FORCE)
	endif()
	set(VCPKG_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../vcpkg CACHE STRING "vcpkg configuration directory to use" FORCE)
	set(VCPKG_INSTALLED_DIR  ${CMAKE_CURRENT_BINARY_DIR}/../vcpkg CACHE STRING "vcpkg configuration directory to use" FORCE)
	message("VCPKG_ROOT " ${VCPKG_ROOT})
	message("VCPKG_INSTALLED_DIR " ${VCPKG_INSTALLED_DIR})
	include(cmake/automate-vcpkg.cmake)
	vcpkg_bootstrap()
	# Set this so that all the various find_package() calls don't need an explicit
	# CONFIG option
	set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
	set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_BINARY_DIR}/vcpkg/scripts/buildsystems/../vcpkg.cmake CACHE STRING "Vcpkg toolchain file")
	# Set this so that all the various find_package() calls don't need an explicit
	# CONFIG option
	set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
endif()

project (rambrain)

option (BUILD_TESTS "Build tests" OFF)
option (PTEST_CHECKS "Include correctness checks in performance tests" OFF)
option (SWAPSTATS "Output swap stats on signal SIGUSR1" ON)
option (LOGSTATS "Log stats instead of outputting them to stdout" OFF)
option (PARENTAL_CONTROL "Hook up chunks in a tree" OFF)
option (CAN_IGNORE_WARNINGS "Enable warning ignore macros to be used to suppress unnescessary stuff" ON)
option (OPTIMISE_COMPILATION "Enable O3 optimisation instead O0" ON)
option (USE_XPRESSIVE "Use boost xpressive for regular expressions" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(OpenMP)
if(${OPENMP_FOUND})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    if(BUILD_TESTS)
        message(WARNING "Disabling build of some tests since OpenMP is missing")
        add_definitions(-DOpenMP_NOT_FOUND)
    endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_definitions(-DUSERAMBRAINLIBRARY)
add_definitions(-DRAMBRAINLIBRARY_EXPORTS)

if(SWAPSTATS)
    add_definitions(-DSWAPSTATS)
endif()

if(LOGSTATS)
    if(SWAPSTATS)
        add_definitions(-DLOGSTATS)
    else(SWAPSTATS)
        message(FATAL_ERROR "If you want to log stats you also have to enable SWAPSTATS. Exiting cmake.")
    endif(SWAPSTATS)
endif()

if(PARENTAL_CONTROL)
    add_definitions(-DPARENTAL_CONTROL)
endif()

if(CAN_IGNORE_WARNINGS)
    add_definitions(-DCAN_IGNORE_WARNINGS)
endif()

if(OPTIMISE_COMPILATION)
    set(OPTIMISATION -O3)
else()
    set(OPTIMISATION -O0)
endif()

if(BUILD_TESTS)
    add_definitions(-DBUILD_TESTS)
endif()

if(PTEST_CHECKS)
    add_definitions(-DPTEST_CHECKS)
endif()


if(USE_XPRESSIVE)
    add_definitions(-DUSE_XPRESSIVE)
    find_package(Boost REQUIRED)
    if(${Boost_FOUND})
       include_directories(${Boost_INCLUDE_DIRS})
    endif()
endif()

if (WIN32)
    find_package(PThreads4W REQUIRED)
else()
    find_package (Threads REQUIRED)
    if(NOT CMAKE_USE_PTHREADS_INIT)
        error ("Could NOT find pthreads.")
    endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_definitions(-D__STRICT_ANSI__)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/lib)

if (UNIX)
    find_package(AIO REQUIRED)
    include_directories(${AIO_INCLUDE_DIR})
endif()

set(CMAKE_BUILD_TYPE DEBUG)

if (UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -Wall -g ${OPTIMISATION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Compiling with ${CMAKE_CXX_FLAGS}")

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    add_definitions (-fcolor-diagnostics -fansi-escape-codes)
endif()

execute_process(COMMAND git log -1 COMMAND head -1 COMMAND cut -d " " -f2 OUTPUT_VARIABLE GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
message("Git version is ${GIT_VERSION}")
add_definitions(-D__GIT_VERSION="${GIT_VERSION}")

add_subdirectory (src)

if(BUILD_TESTS)
    add_subdirectory (tests)
endif()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
	add_custom_target(rambrain_doc doxygen ${CMAKE_SOURCE_DIR}/doc/doxyfile ${CMAKE_SOURCE_DIR} COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif()

execute_process(COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
