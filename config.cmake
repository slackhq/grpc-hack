option(FORCE_TP_JEMALLOC "Always build and statically link jemalloc instead of using system version" OFF)
option(JEMALLOC_INCLUDE_DIR "The include directory for built & statically linked jemalloc")
option(DOUBLE_CONVERSION_INCLUDE_DIR "The include directory for built & statically linked double-conversion")
option(BOOST_INCLUDE_DIR "The include directory for built & statically linked boost")
option(PROXYGEN_INCLUDE_DIR "The include directory for built & statically linked proxygen")

if (FORCE_TP_JEMALLOC)
    # need to include statically-built third-party/jemalloc since the system version is <5 on 18.04
    include_directories(${JEMALLOC_INCLUDE_DIR})
endif()

if (NOT "$DOUBLE_CONVERSION_INCLUDE_DIR" STREQUAL "")
        include_directories(${DOUBLE_CONVERSION_INCLUDE_DIR})
endif()

if (NOT "$BOOST_INCLUDE_DIR" STREQUAL "")
        include_directories(${BOOST_INCLUDE_DIR})
endif()

if (NOT "$PROXYGEN_INCLUDE_DIR" STREQUAL "")
        include_directories(${PROXYGEN_INCLUDE_DIR})
endif()

HHVM_ADD_INCLUDES(grpc_hack ./)
HHVM_EXTENSION(grpc_hack ext_grpc.cpp)
HHVM_SYSTEMLIB(grpc_hack ext_grpc.php)
HHVM_LINK_LIBRARIES(grpc_hack ${GRPC_CLIENT_LIB})
