load("//:def.bzl", "copts", "torch_deps")

test_copts = [
    "-fno-access-control",
] + copts()

test_linkopts = [
    "-lpython3.10",
    "-ltorch",
]

test_deps = [
    "//src/fastertransformer/devices/cpu_impl:cpu_impl",
    "//src/fastertransformer/devices/testing:device_test_utils",
    "//src/fastertransformer/devices/base_tests:base_tests",
] + torch_deps()


cc_test(
    name = "cpu_ffn_op_test",
    srcs = [
        "ops/CpuFFnOpTest.cc",
    ],
    data = [],
    copts = test_copts,
    linkopts = test_linkopts,
    deps = test_deps,
)

cc_test(
    name = "cpu_ops_test",
    srcs = [
        "CpuOpsTest.cc",
    ],
    data = [],
    copts = test_copts,
    linkopts = test_linkopts,
    deps = test_deps,
)
