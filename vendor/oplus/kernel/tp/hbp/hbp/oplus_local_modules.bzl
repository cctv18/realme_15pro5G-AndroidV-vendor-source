load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    define_oplus_ddk_module(
        name = "oplus_hbp_core",
        srcs = native.glob([
            "**/*.h",
            "hbp_core.c",
            "hbp_notify.c",
            "hbp_tui.c",
            "hbp_frame.c",
            "utils/debug.c",
            "utils/platform.c",
            "chips/touch_custom.c",
            "hbp_device.c",
            "hbp_power.c",
            "hbp_spi.c",
            "hbp_sysfs.c",
            "hbp_exception.c"
        ]),
        includes = ["."],
        ko_deps = [],
        local_defines = [
                 "BUILD_BY_BAZEL",
        ],
        conditional_defines = {
            "mtk":  ["CONFIG_TOUCHPANEL_MTK_PLATFORM", "CONFIG_DRM_MEDIATEK"],
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
        },
    )

    define_oplus_ddk_module(
        name = "oplus_ft3683g",
        srcs = native.glob([
            "**/*.h",
            "chips/focal/ft3683g/fhp_core.c",
        ]),
        includes = ["."],
        ko_deps = [
            "//vendor/oplus/kernel/tp/hbp/hbp:oplus_hbp_core",
        ],
        local_defines = [
                 "BUILD_BY_BAZEL",
        ],
        conditional_defines = {
            "mtk":  ["CONFIG_TOUCHPANEL_MTK_PLATFORM", "CONFIG_DRM_MEDIATEK"],
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
        },

    )

    define_oplus_ddk_module(
        name = "oplus_bsp_tp_hbp_syna_s3910",
        srcs = native.glob([
            "**/*.h",
            "chips/synaptics/s3910/syna_tcm2.c",
            "chips/synaptics/s3910/syna_tcm2_sysfs.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_core_v1.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_core_v2.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_func_base.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_func_touch.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_func_reflash.c",
            "chips/synaptics/s3910/tcm/synaptics_touchcom_func_romboot.c",
        ]),
        includes = ["."],
        ko_deps = [
            "//vendor/oplus/kernel/tp/hbp/hbp:oplus_hbp_core",
        ],
        local_defines = [
            "BUILD_BY_BAZEL",
        ],
        conditional_defines = {
            "mtk":  ["CONFIG_TOUCHPANEL_MTK_PLATFORM"],
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
        },

    )

    ddk_headers(
        name = "config_headers",
        hdrs  = native.glob([
            "**/*.h",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_hbp_core",
        module_list = [
            "oplus_hbp_core",
            "oplus_ft3683g",
            "oplus_bsp_tp_hbp_syna_s3910"
        ],
    )
