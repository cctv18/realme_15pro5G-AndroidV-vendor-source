load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_targets", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    ko_deps = []
    header_deps = []

    bazel_support_targets = oplus_ddk_get_targets()
    for target in bazel_support_targets:
        if target == "sun":
            ko_deps.append("//vendor/oplus/kernel/cpu/geas:oplus_bsp_geas")
            print("append deps oplus_bsp_sched_ext")
            break

    define_oplus_ddk_module(
        name = "oplus_bsp_game_opt",
        srcs = native.glob([
            "**/*.h",
            "cpu_load.c",
            "cpufreq_limits.c",
            "debug.c",
            "dsu_freq.c",
            "early_detect.c",
            "fake_cpufreq.c",
            "game_ctrl.c",
            "rt_info.c",
            "task_util.c",
            "yield_opt.c",
            "frame_load.c",
            "frame_sync.c",
            "task_boost/heavy_task_boost.c",
            "task_boost/boost_proc.c",
            "critical_task_boost.c",
        ]),

        includes = ["."],
        ko_deps = ko_deps,
        header_deps = header_deps,
        copts = select({
            "//build/kernel/kleaf:kocov_is_true": ["-fprofile-arcs", "-ftest-coverage"],
            "//conditions:default": [],
        }),
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_game",
        module_list = [
            "oplus_bsp_game_opt",
        ],
    )
