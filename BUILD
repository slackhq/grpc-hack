load("//:cc_library_static.bzl", "cc_static_library")

cc_library(
    name = "grpc_client_lib",
    srcs = ["grpc_client.cc"],
    hdrs = ["grpc_client.h"],
    linkstatic = True,
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_base_c", # channelz/json etc.
     ],
    # linkopts=['-static'],
    # linkshared = True,

    # linkopts = ['-ldl'],
)

cc_static_library(
    name = "grpc_client_static",
    dep = ":grpc_client_lib",
)
