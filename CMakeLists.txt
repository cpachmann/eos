# ----------------------------------------------------------------------
# File: CMakeLists.txt
# Author: Andreas-Joachim Peters - CERN
# ----------------------------------------------------------------------

# ************************************************************************
# * EOS - the CERN Disk Storage System                                   *
# * Copyright (C) 2011 CERN/Switzerland                                  *
# *                                                                      *
# * This program is free software: you can redistribute it and/or modify *
# * it under the terms of the GNU General Public License as published by *
# * the Free Software Foundation, either version 3 of the License, or    *
# * (at your option) any later version.                                  *
# *                                                                      *
# * This program is distributed in the hope that it will be useful,      *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
# * GNU General Public License for more details.                         *
# *                                                                      *
# * You should have received a copy of the GNU General Public License    *
# * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
# ************************************************************************

#-------------------------------------------------------------------------------
# Main section
#-------------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.3)

# Set the default CMAKE_BUILD_TYPE to RelWithDebInfo before the project command
# as it can set CMAKE_BUILD_TYPE itself.
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build options: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release \
     RelWithDebInfo MinSizeRel.")
endif ()

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
endif ()

project (eos)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#-------------------------------------------------------------------------------
# Make gtest / gmock available for all downstream CMakeLists.txt that need it
#-------------------------------------------------------------------------------
set(GTEST "${CMAKE_SOURCE_DIR}/unit_tests/googletest/")
set(GTEST_BINARY_DIR "${CMAKE_BINARY_DIR}/unit_tests/googletest/")
add_subdirectory("${GTEST}" "${GTEST_BINARY_DIR}" EXCLUDE_FROM_ALL)

#-------------------------------------------------------------------------------
# Activate include-what-you-use
#-------------------------------------------------------------------------------
option(ENABLE_IWYU "Enable include-what-you-use tool" OFF)

if(ENABLE_IWYU)
  find_program(IWYU_PATH NAMES include-what-you-use iwyu)
  if(NOT IWYU_PATH)
    message(FATAL_ERROR "Could not find include-what-you-use")
  endif()

  set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${IWYU_PATH})
  set(CMAKE_C_INCLUDE_WHAT_YOU_USE ${IWYU_PATH})
endif()

#-------------------------------------------------------------------------------
# Check if we're generating a test coverage report
#-------------------------------------------------------------------------------
option(TESTCOVERAGE "Build with test coverage reporting" OFF)

if(TESTCOVERAGE)
  set(GCOV_CFLAGS "-fprofile-arcs -ftest-coverage --coverage")
  set(GCOV_LIBS "gcov")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCOV_CFLAGS}")

  add_custom_target(
    raw-test-trace
    COMMAND lcov --capture --base-directory ${CMAKE_CURRENT_SOURCE_DIR} --directory ${CMAKE_BINARY_DIR} --output-file ${CMAKE_BINARY_DIR}/raw-trace.info
  )

  add_custom_target(
    filtered-test-trace
    COMMAND lcov --remove ${CMAKE_BINARY_DIR}/raw-trace.info "/usr/include/\\*" "/opt/eos-folly/\\*" "${CMAKE_BINARY_DIR}/\\*" "namespace/ns_quarkdb/tests/\\*" --output-file ${CMAKE_BINARY_DIR}/filtered-trace.info
    DEPENDS raw-test-trace
  )

  add_custom_target(
    coverage-report
    COMMAND genhtml ${CMAKE_BINARY_DIR}/filtered-trace.info --output-directory ${CMAKE_BINARY_DIR}/coverage-report
    DEPENDS filtered-test-trace
  )
endif()


#-------------------------------------------------------------------------------
# Include some generic functions and compiler definition parameters
#-------------------------------------------------------------------------------
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Default install prefix: /usr" FORCE)
endif ()

include(EosUtils)
EOS_CheckOutOfSourceBuild()
EOS_GetUidGid("daemon" "DAEMONUID" "DAEMONGID")
EOS_GetVersion("${VERSION_MAJOR}" "${VERSION_MINOR}" "${VERSION_PATCH}" "${RELEASE}")
include(EosOSDefaults)

if (CLIENT)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCLIENT_ONLY=1")
endif ()

set(EOS_CXX_DEFINE "-DEOSCITRINE -DVERSION=\\\"${VERSION}\\\" -DRELEASE=\\\"${RELEASE}\\\"")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EOS_CXX_DEFINE} ${CPP_VERSION} -msse4.2 -Wall -Wno-error=parentheses")

# Set Address Sanitizer flag
if (ASAN)
  include(CheckCXXCompilerFlag)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
  check_cxx_compiler_flag(-fsanitize=address HAVE_FLAG_ASAN)
  unset(CMAKE_REQUIRED_FLAGS)

  if (NOT HAVE_FLAG_ASAN)
    message(FATAL_ERROR "A compiler with '-fsanitize=address' support is required.")
  endif()

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

# Temporary fix for clang 3.4 compile errors
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.5)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=1024")
  endif ()
endif ()

set (CMAKE_INSTALL_SYSCONFDIR /etc)
include (EosFindLibs)

#-------------------------------------------------------------------------------
# Generate documentation
#-------------------------------------------------------------------------------
if (PYTHONINTERP_FOUND AND SPHINX_FOUND)
  add_custom_target(doc
    COMMAND ${PYTHON_EXECUTABLE} generate_docs.py
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/doc"
    COMMENT "Build HTML documentation with Sphinx ...")
endif ()


#-------------------------------------------------------------------------------
# Generate man pages
#-------------------------------------------------------------------------------
if (BUILD_MANPAGES AND HELP2MAN_FOUND)
  add_subdirectory(man)
endif()

#-------------------------------------------------------------------------------
# Build the libfmt for fast convertions
#-------------------------------------------------------------------------------
if (NOT TARGET fmt)
  include_directories(common/fmt/include)
  add_subdirectory(common/fmt)
  set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
endif ()

add_subdirectory(common)
add_subdirectory(proto)
add_subdirectory(misc)
add_subdirectory(authz)
add_subdirectory(fst)
add_subdirectory(mq)
add_subdirectory(fuse)
add_subdirectory(console)
add_subdirectory(fusex)
add_subdirectory(test)

if (NOT CLIENT)
  add_subdirectory(mgm)
  add_subdirectory(namespace)
  add_subdirectory(sync)
  add_subdirectory(srm)
  add_subdirectory(utils)
  add_subdirectory(archive)
  add_subdirectory(auth_plugin)
  add_subdirectory(unit_tests)
endif ()

#-------------------------------------------------------------------------------
# Uninstall target
#-------------------------------------------------------------------------------
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(
   uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake/cmake_uninstall.cmake")

#-------------------------------------------------------------------------------
# Packaging
#-------------------------------------------------------------------------------
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
set(CPACK_PACKAGE_RELEASE "${RELEASE}")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}")
set(CPACK_SOURCE_IGNORE_FILES
"${CMAKE_CURRENT_BINARY_DIR};/ApMon/;/git/;/gitlab-ci/;/ccache/;/xrootd-dsi/;/nginx/;/dsi/;\
/microhttpd/;/.deps/;~$;'.'o$;/lib/;/.git/;eos.spec.in;elrepopackage.spec;.tar.gz$;\
.tar.bz2$;${CPACK_SOURCE_IGNORE_FILES};")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config_spec.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/config_spec.cmake" @ONLY IMMEDIATE)

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/eos.spec"
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake/config_spec.cmake"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cmake/config_spec.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/eos.spec.in")

add_custom_target(
  dist
  COMMAND ${CMAKE_MAKE_PROGRAM} package_source
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/eos.spec")

add_custom_command(
  TARGET dist POST_BUILD
  COMMAND rm ARGS -rf "${CMAKE_CURRENT_SOURCE_DIR}/eos.spec"
  COMMENT "Clean generated spec file")

include(CPack)

#-------------------------------------------------------------------------------
# Source and binary rpms
#-------------------------------------------------------------------------------
set (RPM_DEFINE --define "_source_filedigest_algorithm md5" --define "_binary_filedigest_algorithm md5")
set (EOS_ARCHIVE         "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.tar.gz")
set (EOS_CLIENTS_ARCHIVE "${CMAKE_PROJECT_NAME}-clients-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.tar.gz")

add_custom_target(
  srpm
  COMMAND rpmbuild ${RPM_DEFINE} -ts ${EOS_ARCHIVE} --define "_topdir ${CMAKE_BINARY_DIR}" --with server --with asan)

add_custom_target(
  rpm-client
  COMMAND rpmbuild -tb ${EOS_ARCHIVE} --define "_topdir ${CMAKE_BINARY_DIR}")

add_custom_target(
  rpm
  COMMAND rpmbuild -tb ${EOS_ARCHIVE} --define "_topdir ${CMAKE_BINARY_DIR}" --with server)

add_custom_target(
  rpm-asan
  COMMAND rpmbuild -tb ${EOS_ARCHIVE} --define "_topdir ${CMAKE_BINARY_DIR}" --with server --with asan)

add_dependencies(srpm dist)
add_dependencies(rpm dist)
add_dependencies(rpm-asan dist)
add_dependencies(rpm-client dist)

#-------------------------------------------------------------------------------
# Custom target to build on OSX
#-------------------------------------------------------------------------------
add_custom_target(
  osx
  COMMAND sudo ../utils/eos-osx-package.sh ${CPACK_PACKAGE_VERSION})

#-------------------------------------------------------------------------------
# Print project summary
#-------------------------------------------------------------------------------
include(EosSummary)
