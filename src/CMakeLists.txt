add_library(liborc SHARED IMPORTED)
add_library(libhdfspp SHARED IMPORTED)
add_library(liblz4 SHARED IMPORTED)
add_library(libprotobuf SHARED IMPORTED)
add_library(libprotoc SHARED IMPORTED)
add_library(libsnappy SHARED IMPORTED)
add_library(libz SHARED IMPORTED)
add_library(libzstd SHARED IMPORTED)

add_library(orc_impl SHARED
                orc_impl.cpp)

target_link_libraries(orc_impl orc)
