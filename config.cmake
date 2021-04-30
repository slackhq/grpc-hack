option(FORCE_TP_JEMALLOC "Always build and statically link jemalloc instead of using system version" OFF)
option(JEMALLOC_INCLUDE_DIR "The include directory for built & statically linked jemalloc")

if (FORCE_TP_JEMALLOC)
    # need to include statically-built third-party/jemalloc since the system version is <5 on 18.04
    include_directories(${JEMALLOC_INCLUDE_DIR})
endif()

HHVM_ADD_INCLUDES(grpc_hack ./)
HHVM_EXTENSION(grpc_hack ext_grpc.cpp)
HHVM_SYSTEMLIB(grpc_hack ext_grpc.php)
HHVM_LINK_LIBRARIES(grpc_hack ${GRPC_CLIENT_LIB})
