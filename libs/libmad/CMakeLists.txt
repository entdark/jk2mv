# We only execute this script if the user wants to use our bundled libmad.
#
# It is built as a static relocatable library, and linked into any target that
# requires it.

# This only has effect in this directory (lib/libmad).
include_directories(include/)

add_definitions(-DHAVE_CONFIG_H -DFPM_DEFAULT)

# we want more accurate FPM logic
if(${Architecture} STREQUAL "x64")
  add_definitions(-DFPM_64BIT)
elseif(${Architecture} STREQUAL "x86")
  add_definitions(-DFPM_INTEL)
elseif(${Architecture} STREQUAL "i386")
  add_definitions(-DFPM_INTEL)
elseif(${Architecture} STREQUAL "x86_64")
  add_definitions(-DFPM_64BIT)
elseif(${Architecture} STREQUAL "arm")
  add_definitions(-DFPM_ARM)
elseif(${Architecture} STREQUAL "amd64")
  add_definitions(-DFPM_64BIT)
else()
  add_definitions(-DFPM_DEFAULT)
endif()

if(MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

add_library(libmad STATIC         mad.h
                                  global.h
                                  bit.h
                                  bit.c
                                  decoder.h
                                  decoder.c
                                  fixed.h
                                  fixed.c
                                  frame.h
                                  frame.c
                                  huffman.h
                                  huffman.c
                                  layer3.h
                                  layer3.c
                                  layer12.h
                                  layer12.c
                                  stream.h
                                  stream.c
                                  synth.h
                                  synth.c
                                  timer.h
                                  timer.c
                                  version.h
                                  version.c
                                  D.dat
                                  imdct_s.dat
                                  qc_table.dat
                                  rq_table.dat
                                  sf_table.dat)

# Let consumers get at our bundled library in the standard CMake way.  These
# variables are not set in the cache, but instead shadow the variables in the
# cache.  (These are the same variables exported by the standard CMake FindMAD
# module).
set(MAD_LIBRARIES libmad PARENT_SCOPE)
set(MAD_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/libmad/include/ PARENT_SCOPE)
set(MAD_FOUND TRUE)

mark_as_advanced(MAD_LIBRARIES MAD_INCLUDE_DIRS)

# Build as position-independent code, since it will probably be linked into a
# dynamic library.
set_property(TARGET libmad PROPERTY POSITION_INDEPENDENT_CODE True)
set_property(TARGET libmad PROPERTY FOLDER "libs")
