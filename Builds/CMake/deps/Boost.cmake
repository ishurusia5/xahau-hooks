#[===================================================================[
   NIH dep: boost
#]===================================================================]
if((NOT DEFINED BOOST_ROOT) AND(DEFINED ENV{BOOST_ROOT}))
  set(BOOST_ROOT $ENV{BOOST_ROOT})
endif()
if((NOT DEFINED BOOST_LIBRARYDIR) AND(DEFINED ENV{BOOST_LIBRARYDIR}))
  set(BOOST_LIBRARYDIR $ENV{BOOST_LIBRARYDIR})
endif()
file(TO_CMAKE_PATH "${BOOST_ROOT}" BOOST_ROOT)
if(WIN32 OR CYGWIN)
  # Workaround for MSVC having two boost versions - x86 and x64 on same PC in stage folders
  if((NOT DEFINED BOOST_LIBRARYDIR) AND (DEFINED BOOST_ROOT))
    if(IS_DIRECTORY ${BOOST_ROOT}/stage64/lib)
      set(BOOST_LIBRARYDIR ${BOOST_ROOT}/stage64/lib)
    elseif(IS_DIRECTORY ${BOOST_ROOT}/stage/lib)
      set(BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
    elseif(IS_DIRECTORY ${BOOST_ROOT}/lib)
      set(BOOST_LIBRARYDIR ${BOOST_ROOT}/lib)
    else()
      message(WARNING "Did not find expected boost library dir. "
        "Defaulting to ${BOOST_ROOT}")
      set(BOOST_LIBRARYDIR ${BOOST_ROOT})
    endif()
  endif()
endif()
message(STATUS "BOOST_ROOT: ${BOOST_ROOT}")
message(STATUS "BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")

# uncomment the following as needed to debug FindBoost issues:
#set(Boost_DEBUG ON)

#[=========================================================[
   boost dynamic libraries don't trivially support @rpath
   linking right now (cmake's default), so just force
   static linking for macos, or if requested on linux by flag
#]=========================================================]
if(static)
  set(Boost_USE_STATIC_LIBS ON)
endif()
set(Boost_USE_MULTITHREADED ON)
if(static AND NOT APPLE)
  set(Boost_USE_STATIC_RUNTIME ON)
else()
  set(Boost_USE_STATIC_RUNTIME OFF)
endif()
# TBD:
# Boost_USE_DEBUG_RUNTIME:  When ON, uses Boost libraries linked against the
find_package(Boost 1.86 REQUIRED
  COMPONENTS
    chrono
    container
    context
    coroutine
    date_time
    filesystem
    program_options
    regex
    system
    iostreams
    thread)

add_library(ripple_boost INTERFACE)
add_library(Ripple::boost ALIAS ripple_boost)
if(is_xcode)
  target_include_directories(ripple_boost BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
  target_compile_options(ripple_boost INTERFACE --system-header-prefix="boost/")
else()
  target_include_directories(ripple_boost SYSTEM BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
endif()

target_link_libraries(ripple_boost
  INTERFACE
    Boost::boost
    Boost::chrono
    Boost::container
    Boost::coroutine
    Boost::date_time
    Boost::filesystem
    Boost::iostreams
    Boost::program_options
    Boost::regex
    Boost::system
    Boost::thread)
if(Boost_COMPILER)
  target_link_libraries(ripple_boost INTERFACE Boost::disable_autolinking)
endif()
if(san AND is_clang)
  # TODO: gcc does not support -fsanitize-blacklist...can we do something else
  # for gcc ?
  if(NOT Boost_INCLUDE_DIRS AND TARGET Boost::headers)
    get_target_property(Boost_INCLUDE_DIRS Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  message(STATUS "Adding [${Boost_INCLUDE_DIRS}] to sanitizer blacklist")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt "src:${Boost_INCLUDE_DIRS}/*")
  target_compile_options(opts
    INTERFACE
      # ignore boost headers for sanitizing
      -fsanitize-blacklist=${CMAKE_CURRENT_BINARY_DIR}/san_bl.txt)
endif()
