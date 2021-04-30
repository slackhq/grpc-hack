load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

proto_library(
    name = "proto",
    srcs = ["helloworld.proto"],
)

cc_proto_library(
    name = "cc_proto",
    deps = [":proto"],
)

cc_grpc_library(
    name = "grpc",
    srcs = [":proto"],
    grpc_only = True,
    deps = [":cc_proto"],
)

cc_binary(
    name = "server",
    srcs = ["server.cc"],
    deps = [
        ":grpc",
        '@com_github_grpc_grpc//:grpc++',
        ],
)
