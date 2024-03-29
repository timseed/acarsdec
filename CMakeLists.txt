cmake_minimum_required (VERSION 3.2)
project (acarsdec C)

#add_compile_options(-Ofast -march=native )

add_executable(acarsdec acars.c  acarsdec.c acarsdec.h  cJSON.c  label.c  msk.c  output.c rtl.c rtl.h)

find_library(LIBACARS acars)
if(LIBACARS)
message ( STATUS "Using libacars")
add_definitions(-DHAVE_LIBACARS )
target_link_libraries( acarsdec acars)
else()
message ( STATUS "Not using libacars")
endif()

option(rtl "Compiling for rtl sdr" )
if(rtl)
  find_library(LIBRTL rtlsdr)
  if(LIBRTL)
	add_definitions(-DWITH_RTL )
	include_directories(/usr/local/Cellar/librtlsdr/0.6.0/include/)
	target_link_libraries( acarsdec ${LIBRTL})
  else()
	message (FATAL_ERROR "librtlsdr path not found")
  endif()
endif()


option(airspy "Compiling for airspy sdr" )
if(airspy)
find_library(LIBAIR airspy)
if(NOT LIBAIR)
message ( FATAL_ERROR "libairspy path not found")
endif()
add_definitions(-DWITH_AIR )
target_sources( acarsdec PRIVATE air.c)
target_link_libraries( acarsdec ${LIBAIR})
endif()

option(sdrplay "Compiling for sdrplay sdr" )
if(sdrplay)
find_library(LIBPLAY mirsdrapi-rsp)
if(NOT LIBPLAY)
message ( FATAL_ERROR "libmirsdrapi-rsp path not found")
endif()
add_definitions(-DWITH_SDRPLAY )
target_sources( acarsdec PRIVATE sdrplay.c)
target_link_libraries( acarsdec ${LIBPLAY})
endif()

if(NOT rtl AND NOT airspy AND NOT sdrplay)
message ("No sdr option set ! are you sure ?")
endif()

target_link_libraries( acarsdec pthread m )

install(TARGETS acarsdec
	RUNTIME DESTINATION bin
)
