cmake_minimum_required (VERSION 2.6)
project (Mesh)

set(CMAKE_BUILD_TYPE "Debug") # Release

# find the config directory
if(DEFINED ENV{INSILICOROOT})
  set(CONFDIR "$ENV{INSILICOROOT}/config")
else()
  set(CONFDIR "../../../config")
endif()

include( ${CONFDIR}/insilico.cmake )


set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSPACEDIM=2" )

# add the executable (name  sourceFile)
add_executable(unstructured ../unstructured.cpp)
add_executable(structured ../structured.cpp)