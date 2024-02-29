include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include_directories (
        ${CMAKE_INCLUDE_PATH}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

file(GLOB TEST_SOURCES
        "test*.cpp"
)
list(APPEND TEST_SOURCES "allTests.cpp")
add_executable (rambrain-tests ${TEST_SOURCES})
add_custom_command(TARGET rambrain-tests PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-tests gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-tests  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-tests  pthread)
endif()

file(GLOB TEST_SOURCES
        "test*.cpp"
)
list(APPEND TEST_SOURCES "unitTests.cpp")
add_executable (rambrain-unittests ${TEST_SOURCES})
add_custom_command(TARGET rambrain-unittests PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-unittests gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-unittests  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-unittests  pthread)
endif()


file(GLOB TEST_SOURCES
        "test*.cpp"
)
list(APPEND TEST_SOURCES "integrationTests.cpp")
add_executable (rambrain-integrationtests ${TEST_SOURCES})
add_custom_command(TARGET rambrain-integrationtests PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-integrationtests gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-integrationtests  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-integrationtests  pthread)
endif()

file(GLOB TEST_SOURCES
        "performanceTests.cpp"
        "tester.cpp"
        "performanceTestClasses.cpp"
)
add_executable(rambrain-performancetests ${TEST_SOURCES})
add_custom_command(TARGET rambrain-performancetests PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-performancetests gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-performancetests  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-performancetests  pthread)
endif()


file(GLOB TEST_SOURCES
        "performanceTestsWrapper.cpp"
        "tester.cpp"
        "performanceTestClasses.cpp"
)
add_executable(rambrain-performancetestswrapper ${TEST_SOURCES})
add_custom_command(TARGET rambrain-performancetestswrapper PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-performancetestswrapper gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-performancetestswrapper  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-performancetestswrapper  pthread)
endif()


find_package(MPI)
if(NOT "${MPI_INCLUDE_PATH}" STREQUAL "")
    set(oldccomp ${CMAKE_C_COMPILER})
    set(CMAKE_C_COMPILER mpicc)
    set(oldcxxcomp ${CMAKE_CXX_COMPILER})
    set(CMAKE_CXX_COMPILER mpicxx)

    message(STATUS "Including mpi specific test")
    include_directories(SYSTEM ${MPI_INCLUDE_PATH})
    file(GLOB TEST_SOURCES
            "mpitest.cpp"
    )
    add_executable(rambrain-mpitest ${TEST_SOURCES})
    add_custom_command(TARGET rambrain-mpitest PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    target_link_libraries (rambrain-mpitest pthread rambrain_static ${MPI_C_LIBRARIES})
else()
    message(WARNING "Omitting mpi specific test")
endif()


file(GLOB TEST_SOURCES
        "configTest.cpp"
)
add_executable(rambrain-configtest ${TEST_SOURCES})
add_custom_command(TARGET rambrain-configtest PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_link_libraries (rambrain-configtest gmock gtest rambrain_static)
if(WIN32)
    target_link_libraries(rambrain-configtest  PThreads4W::PThreads4W)
else()
    target_link_libraries(rambrain-configtest  pthread)
endif()


file(GLOB TEST_SOURCES
        "memeater.cpp"
)
add_executable(rambrain-memeater ${TEST_SOURCES})
add_custom_command(TARGET rambrain-memeater PRE_BUILD COMMAND ./scripts/gengit.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
