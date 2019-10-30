#
# Project Definition
#
cmake_minimum_required(VERSION 3.8.2)
project(ThreadComms)

#
# Find dependencies
#
find_package(UtilTime 0.1 REQUIRED)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED)

#
# Exported Library (libThreadComms)
#
add_library(ThreadComms STATIC
    include/AggPipePub.h
    include/AggPipePub.hpp
    include/AggPipePub_MessageAdaption.h
    include/AggUpdate.h
    include/IPostable.h
    include/PipePublisher.h
    include/PipePublisher.hpp
    include/PipeSubscriber.h
    include/PipeSubscriber.hpp
    include/WorkerThread.h
    src/WorkerThread.cpp
)

target_link_libraries(ThreadComms PUBLIC
    Threads::Threads
    Boost::boost
)
target_include_directories(ThreadComms PUBLIC
    $<BUILD_INTERFACE:${ThreadComms_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_property(TARGET ThreadComms PROPERTY PUBLIC_HEADER
    ${ThreadComms_SOURCE_DIR}/include/AggPipePub.h
    ${ThreadComms_SOURCE_DIR}/include/AggPipePub.hpp
    ${ThreadComms_SOURCE_DIR}/include/AggUpdate.h
    ${ThreadComms_SOURCE_DIR}/include/AggPipePub_MessageAdaption.h
    ${ThreadComms_SOURCE_DIR}/include/IPostable.h
    ${ThreadComms_SOURCE_DIR}/include/PipePublisher.h
    ${ThreadComms_SOURCE_DIR}/include/PipePublisher.hpp
    ${ThreadComms_SOURCE_DIR}/include/PipeSubscriber.h
    ${ThreadComms_SOURCE_DIR}/include/PipeSubscriber.hpp
    ${ThreadComms_SOURCE_DIR}/include/WorkerThread.h
)

add_executable(publisherSpeed src/publisherSpeed.cpp src/dummy.cpp)
target_link_libraries(publisherSpeed ThreadComms UtilTime::Time)
target_compile_features(publisherSpeed PRIVATE cxx_std_17)

#
# Test Configuration
#
find_package(GTest REQUIRED)

add_executable(pipe test/pipe.cpp)
target_link_libraries(pipe ThreadComms GTest::GTest GTest::Main)
target_compile_features(pipe PRIVATE cxx_std_17)

add_executable(pipe_state_model test/pipe_state_model.cpp)
target_link_libraries(pipe_state_model ThreadComms GTest::GTest GTest::Main)
target_compile_features(pipe_state_model PRIVATE cxx_std_17)

add_executable(agg test/agg.cpp include/AggPipePub.h)
target_link_libraries(agg ThreadComms UtilTime::Time GTest::GTest GTest::Main)
target_compile_features(agg PRIVATE cxx_std_17)

add_executable(worker test/worker.cpp)
target_link_libraries(worker PRIVATE ThreadComms UtilTime::Time GTest::GTest GTest::Main)
target_compile_features(worker PRIVATE cxx_std_17)

#
# NOTE: Valgrind must be configured *before* testing is imported
#
set(MEMORYCHECK_COMMAND_OPTIONS "--gen-suppressions=all --track-origins=yes --freelist-vol=2000000000 --error-exitcode=1 --track-fds=yes --num-callers=50 --fullpath-after= --trace-children=yes --leak-check=full" )
find_program(MEMORYCHECK_COMMAND valgrind )
include (CTest)

enable_testing()
add_test(pipe pipe)
add_test(pipe_state_model pipe_state_model)
add_test(agg agg)
add_test(worker worker)

#
# Installation instructions
#
install(TARGETS ThreadComms EXPORT ThreadCommsTargets
    ARCHIVE  DESTINATION lib
    INCLUDES DESTINATION include
    PUBLIC_HEADER DESTINATION include
)

install (EXPORT ThreadCommsTargets
    FILE         ThreadCommsTargets.cmake
    NAMESPACE    ThreadComms::
    DESTINATION  lib/cmake/ThreadComms
)

#
# Define our package version
#
include(CMakePackageConfigHelpers)
write_basic_package_version_file("ThreadCommsConfigVersion.cmake"
    VERSION       0.1
    COMPATIBILITY SameMajorVersion
)
install (
    FILES       ${ThreadComms_BINARY_DIR}/ThreadCommsConfigVersion.cmake
    DESTINATION lib/cmake/ThreadComms
)

#
# Finally export the Config which CMake will use
# when looking for matching libraries to link against.
#
install (
    FILES       ThreadCommsConfig.cmake
    DESTINATION lib/cmake/ThreadComms
)

#
# Configure Coverage Build
#
SET(CMAKE_CXX_FLAGS_COVERAGE
    "${GCC_DEBUG_FLAGS} -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C++ compiler during coverage builds."
    FORCE )
SET(CMAKE_C_FLAGS_COVERAGE
    "${GCC_DEBUG_FLAGS} -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C compiler during coverage builds."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_COVERAGE
    CMAKE_C_FLAGS_COVERAGE)
