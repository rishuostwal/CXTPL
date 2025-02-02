﻿cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)

set(PROJECT_NAME CXTPL)
set(PROJECT_VERSION 1.0.1)

# Building as separate project.
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/") # for conan

# Setup conan and include everything.
#include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
#conan_basic_setup()
include(conanbuildinfo)
conan_basic_setup(TARGETS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

# NOTE: always FALSE here (change below)
set(TARGET_EMSCRIPTEN FALSE)
set(TARGET_LINUX FALSE)
if(EMSCRIPTEN)
  set(TARGET_EMSCRIPTEN TRUE)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(TARGET_LINUX TRUE)
else()
  set(TARGET_UNKNOWN TRUE)
  message(FATAL_ERROR "platform not supported")
endif()

option(SINGLE_THREADED "Enable single threaded mode" OFF) # TODO

set(CHROMIUM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromium/)

set(COMMON_THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/)

set(USE_G3LOG
  TRUE CACHE BOOL
  "Use g3log logger")

set(USE_FOLLY
  TRUE CACHE BOOL
  "Use facebook/folly library (Apache License 2.0)")

set(USE_RANG
  FALSE CACHE BOOL
  "Use RANG for coloring terminal")

set(BUILD_EXAMPLES
  FALSE CACHE BOOL
  "Use built-in examples")

set(ENABLE_TESTS
  FALSE CACHE BOOL
  "Use unit tests")

set(ENABLE_CHROMIUM_BASE
  TRUE CACHE BOOL
  "ENABLE CHROMIUM BASE")

set(USE_ICU TRUE CACHE BOOL "USE_ICU")

set(ENABLE_HARFBUZZ TRUE CACHE BOOL "ENABLE_HARFBUZZ")
if(ENABLE_HARFBUZZ AND NOT USE_ICU)
  message(FATAL_ERROR "HARFBUZZ requires ICU")
endif()

if(TARGET_LINUX)
  set(tcmalloc_LIB tcmalloc)
  set(libevent_LIB libevent)
endif(TARGET_LINUX)

include(Dependencies.cmake)

if(USE_ICU)
  set(COMMON_FLAGS "${COMMON_FLAGS} -DHAS_ICU=1")
  set(USE_CUSTOM_ICU TRUE)
  if(USE_CUSTOM_ICU)
    set(COMMON_FLAGS "${COMMON_FLAGS} -DUSE_CUSTOM_ICU=1")
    #
    #
    #
    #set(COMMON_FLAGS "${COMMON_FLAGS} -DSK_USING_THIRD_PARTY_ICU=1")
    # https://github.com/google/skia/blob/master/third_party/icu/BUILD.gn#L15
    #set(COMMON_FLAGS "${COMMON_FLAGS} -DU_USING_ICU_NAMESPACE=0")
    #
    set(ICU_PARENT_FULL_DIR
      #third_party/icu/
      #../../thirdparty/skia/third_party/externals/icu/
      ${COMMON_THIRDPARTY_DIR}/icu_wrapper/
    )

    set(ICU_FULL_DIR
      #third_party/icu/
      #../../thirdparty/skia/third_party/externals/icu/
      ${ICU_PARENT_FULL_DIR}third_party/icu/
    )

    set(OWN_ICU_INCLUDE_DIRS
      ${ICU_PARENT_FULL_DIR}
      ${ICU_FULL_DIR}
      ${ICU_FULL_DIR}source/common/unicode
      ${ICU_FULL_DIR}source/common
      ${ICU_FULL_DIR}source/i18n
    )
    set(CUSTOM_ICU_LIB icu) # see icu.cmake
  else(USE_CUSTOM_ICU)
    set(CUSTOM_ICU_LIB icu) # platform specific
  endif(USE_CUSTOM_ICU)
endif(USE_ICU)

# Mandatory dependencies
find_program(PYTHON_FOUND python)
if(NOT PYTHON_FOUND)
  message(FATAL_ERROR "\"python\" is required, please install it !")
else()
  message(STATUS "\"python\" found...")
endif(NOT PYTHON_FOUND)

# harfbuzz
#option(ENABLE_HARFBUZZ "ENABLE_HARFBUZZ" ON)
#if((ENABLE_BLINK OR ENABLE_COBALT) AND NOT ENABLE_HARFBUZZ)
#  message(WARNING "AUTO ENABLED HARFBUZZ")
#  option(ENABLE_HARFBUZZ "ENABLE_HARFBUZZ" ON)
#endif()
if(ENABLE_HARFBUZZ)
  set(USE_CUSTOM_HARFBUZZ TRUE)
  set(FORCE_USE_SKIA_HARFBUZZ TRUE)
  #
  # custom flags
  set(COMMON_FLAGS "${COMMON_FLAGS} -DENABLE_HARFBUZZ=1")
  # https://github.com/klzgrad/naiveproxy/blob/master/src/third_party/skia/modules/skshaper/BUILD.gn#L11
  # SK_SHAPER_HARFBUZZ_AVAILABLE # < TODO if skia_use_icu
  #
  set(harfbuzz_FULL_DIR
    ${CHROMIUM_DIR}third_party/skia/third_party/externals/harfbuzz/
  )
  #
  if(USE_CUSTOM_HARFBUZZ)
    #
    set(HARFBUZZ_LIBRARIES harfbuzz)
    set(HARFBUZZ_INCLUDE_DIRS ${harfbuzz_FULL_DIR}/src)
  else(USE_CUSTOM_HARFBUZZ)
    set(EMCC_COMMON "${EMCC_COMMON} -s USE_HARFBUZZ=1")
    # see find_package below for linux
  endif(USE_CUSTOM_HARFBUZZ)
endif(ENABLE_HARFBUZZ)

if(ENABLE_HARFBUZZ AND NOT USE_CUSTOM_HARFBUZZ)
  # harfbuzz
  set(USE_SHARED_HARFBUZZ "true")
  if(USE_SHARED_HARFBUZZ)
    find_package(HarfBuzz REQUIRED)
  else()
    set(HARFBUZZ_LIBRARIES harfbuzz)
    set(HARFBUZZ_INCLUDE_DIRS ${harfbuzz_FULL_DIR}/src)
  endif()
  message(STATUS "HARFBUZZ_LIBRARIES=${HARFBUZZ_LIBRARIES}")
  message(STATUS "HARFBUZZ_INCLUDE_DIRS=${HARFBUZZ_INCLUDE_DIRS}")
  # custom flags
  set(COMMON_FLAGS "${COMMON_FLAGS} -DENABLE_HARFBUZZ=1")
endif(ENABLE_HARFBUZZ AND NOT USE_CUSTOM_HARFBUZZ)

# NVALGRIND used in tcmalloc and third_party/valgrind.h
set(COMMON_FLAGS "${COMMON_FLAGS} -DNVALGRIND=1")

# https://github.com/emscripten-core/emscripten/issues/4932
# https://github.com/kosh04/newlisp/blob/master/newlisp.h#L164
# TODO
set(COMMON_FLAGS "${COMMON_FLAGS} -D_FILE_OFFSET_BITS=64")
set(COMMON_FLAGS "${COMMON_FLAGS} -D_LARGEFILE_SOURCE=1")
if(TARGET_EMSCRIPTEN)
  # TODO: -mnontrapping-fptoint
elseif(TARGET_LINUX)
  set(COMMON_FLAGS "${COMMON_FLAGS} -D_LARGEFILE64_SOURCE=1")
  set(COMMON_FLAGS "${COMMON_FLAGS} -DHAVE_PTHREAD=1")
  # Annotations useful when implementing condition variables such as CondVar,
  # using conditional critical sections (Await/LockWhen) and when constructing
  # user-defined synchronization mechanisms.
  set(COMMON_FLAGS "${COMMON_FLAGS} -DDYNAMIC_ANNOTATIONS_ENABLED=1")
else()
  message(FATAL_ERROR "platform not supported")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

if(ENABLE_CHROMIUM_BASE)
  add_subdirectory(third_party/chromium)
  set(CHROMIUM_BASE_LIB base)
  list(APPEND CHROMIUM_BASE_DEFINITIONS CHROMIUM_BASE_IS_ON=1)
  #
  list(APPEND APP_DEFINITIONS ENABLE_HARFBUZZ=1)
  list(APPEND APP_DEFINITIONS HARFBUZZ_UNICODE=1)
  list(APPEND APP_DEFINITIONS ENABLE_BASE=1)
  list(APPEND APP_DEFINITIONS ENABLE_BASE_PREALLOC=1)
  list(APPEND APP_DEFINITIONS DYNAMIC_ANNOTATIONS_ENABLED=1)
  list(APPEND APP_DEFINITIONS HAVE_PTHREAD=1)
  list(APPEND APP_DEFINITIONS _LARGEFILE64_SOURCE=1)
  list(APPEND APP_DEFINITIONS _FILE_OFFSET_BITS=64)
  list(APPEND APP_DEFINITIONS _LARGEFILE_SOURCE=1)
  list(APPEND APP_DEFINITIONS HAS_ICU=1)
  list(APPEND APP_DEFINITIONS USE_CUSTOM_ICU=1)
endif(ENABLE_CHROMIUM_BASE)

set(USED_SYSTEM_LIBS
    Threads::Threads # pthread, https://cmake.org/cmake/help/v3.13/module/FindThreads.html
    ${X11_LIBRARIES} # https://cmake.org/cmake/help/v3.13/module/FindX11.html
    ${CMAKE_DL_LIBS} # https://cmake.org/cmake/help/v3.13/variable/CMAKE_DL_LIBS.html
    EXPAT::EXPAT # https://cmake.org/cmake/help/v3.13/module/FindEXPAT.html
    ZLIB::ZLIB # https://cmake.org/cmake/help/v3.13/module/FindZLIB.html
               # Custom libs
    stdc++fs # C++17 std::filesystem
    CACHE INTERNAL "USED_SYSTEM_LIBS")

set(USED_BOOST_LIBS
    Boost::boost
    Boost::coroutine
    Boost::fiber
    Boost::context
    Boost::filesystem
    Boost::program_options
    Boost::regex
    Boost::system
    CACHE INTERNAL "USED_BOOST_LIBS")

add_library(CXTPL_core STATIC
            # sources
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/CXTPL.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/defaults/defaults.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp/cpp_codegen.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/errors/errors_GeneratorError.cpp
            # includes
            ${CMAKE_CURRENT_SOURCE_DIR}/include/core/CXTPL.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/core/defaults/defaults.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/core/tags.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/core/errors/errors.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/codegen/cpp/cpp_codegen.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/integrations/outcome/error_utils.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/integrations/outcome/error_macros.hpp)

target_link_libraries(
  CXTPL_core
  PUBLIC ${FOLLY_LIBRARIES}
         dynamic_annotations
         ${CHROMIUM_BASE_LIB}
         ${HARFBUZZ_LIBRARIES}
         ${CUSTOM_ICU_LIB}
         #${libDL_LIB}
         # tcmalloc only for posix/linux/e.t.c.
         ${tcmalloc_LIB}
         # libevent only for posix/linux/e.t.c.
         ${libevent_LIB}
         # system libs
         ${USED_SYSTEM_LIBS}
         # @note: Order matters https://stackoverflow.com/a/10269201/10904212
         ${LIBIBERTY_LIBRARY} # used by folly
         ${DOUBLE_CONVERSION_LIBRARY} # used by folly
         ${LIBEVENT_LIB} # used by folly
         ${LZ4_LIBRARY} # used by folly
         ${LIBUNWIND_LIBRARIES} # used by folly
         ${LIBLZMA_LIBRARIES} # used by folly
         ${LIBGFLAGS_LIBRARY} # used by folly
         ${G3LOG_LIBRARIES} # used by folly
         glog
         gflags
         # boost libs
         ${USED_BOOST_LIBS}
         boost_outcome
         microsoft_gsl
         #${CONAN_LIBS}
         #CONAN_PKG::catch2
  )

target_include_directories(
  CXTPL_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/submodules/boost.outcome/include/outcome/quickcpplib/include/ # TODO
    # fatal error: folly/executors/CPUThreadPoolExecutor.h: No such file or directory
    #${CMAKE_CURRENT_SOURCE_DIR}/submodules/folly # TODO
    ${LIBIBERTY_INCLUDE_DIR} # used by folly
    ${G3LOG_INCLUDE_DIR} # used by folly
    ${LIBEVENT_INCLUDE_DIR} # used by folly
    ${LZ4_INCLUDE_DIR} # used by folly
    ${LIBGFLAGS_INCLUDE_DIR} # used by folly
    ${FOLLY_INCLUDE_DIR}
    ${CHROMIUM_DIR} # to ./base/
    )

target_include_directories(CXTPL_core SYSTEM PRIVATE
  ${HARFBUZZ_INCLUDE_DIRS}
)

target_include_directories(CXTPL_core
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_include_directories(CXTPL_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# POSITION_INDEPENDENT_CODE for -fPIC
set_property(TARGET CXTPL_core PROPERTY POSITION_INDEPENDENT_CODE ON)

#target_compile_options(CXTPL_core PRIVATE
#  -fno-rtti)

target_compile_definitions( CXTPL_core PUBLIC
  ${CHROMIUM_BASE_DEFINITIONS}
  ${APP_DEFINITIONS}
  #${COMMON_FLAGS}
)

#target_compile_definitions( CXTPL_core PRIVATE
#  # https://stackoverflow.com/q/14557313/12073498
#  BOOST_NO_RTTI
#  BOOST_NO_TYPEID
#)

target_compile_definitions(CXTPL_core
                           PUBLIC
                           GOOGLE_GLOG_DLL_DECL=
                           # https://stackoverflow.com/a/30877725
                           #BOOST_SYSTEM_NO_DEPRECATED
                           #BOOST_ERROR_CODE_HEADER_ONLY
                           )

set_target_properties(CXTPL_core PROPERTIES ENABLE_EXPORTS 1)

install(TARGETS CXTPL_core DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")

add_subdirectory(tool)

if(ENABLE_TESTS)
  # Now enable our tests.
  enable_testing()
  add_subdirectory(tests)
endif(ENABLE_TESTS)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif(BUILD_EXAMPLES)
