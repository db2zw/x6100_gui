add_library(FT8 STATIC qso.cpp worker.c utils.c gfsk.c)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../qth")

