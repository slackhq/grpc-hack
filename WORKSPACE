load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

GRPC_VERSION = "1.37.0"

http_archive(
    name = "com_github_grpc_grpc",
    sha256 = "c2dc8e876ea12052d6dd16704492fd8921df8c6d38c70c4708da332cf116df22",
    strip_prefix = "grpc-" + GRPC_VERSION,
    urls = ["https://github.com/grpc/grpc/archive/v" + GRPC_VERSION + ".tar.gz"],
)

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()
