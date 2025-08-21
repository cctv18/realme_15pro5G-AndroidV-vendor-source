dtbo-$(CONFIG_ARCH_SUN)   := sun-camera.dtbo
# dtbo-$(CONFIG_ARCH_SUN)   += sun-camera-sensor-mtp.dtbo \
# 				sun-camera-sensor-rumi.dtbo \
# 				sun-camera-sensor-cdp.dtbo  \
# 				sun-camera-sensor-hdk.dtbo  \
# 				sun-camera-sensor-qrd.dtbo

dtbo-$(CONFIG_ARCH_SUN)   += oplus/dodge-camera-overlay-evb.dtbo \
			     oplus/dodge-camera-overlay-T0.dtbo \
			     oplus/dodge-camera-overlay-T1.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/zhufeng-camera-overlay-evb.dtbo \
			     oplus/zhufeng-camera-overlay-preT0.dtbo \
			     oplus/zhufeng-camera-overlay-T0.dtbo \
			     oplus/zhufeng-camera-overlay-EVT.dtbo \
			     oplus/zhufeng-camera-overlay-DVT.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/pista-camera-overlay-evb.dtbo \
			     oplus/pista-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/petrel-camera-overlay-evb.dtbo \
			     oplus/petrel-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/erhai-camera-overlay-evb.dtbo \
				 oplus/erhai-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/hummer-camera-overlay-evb.dtbo \
			     oplus/hummer-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/piloti-camera-overlay-T0.dtbo \
			     oplus/piloti-camera-overlay-evb.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/pagani-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_SUN)   += oplus/paganiIn-camera-overlay-T0.dtbo \

dtbo-$(CONFIG_ARCH_TUNA)  += tuna-camera.dtbo
# dtbo-$(CONFIG_ARCH_TUNA)  += tuna-camera-sensor-mtp.dtbo \
# 				tuna-camera-sensor-cdp.dtbo \
# 				tuna-camera-sensor-qrd.dtbo

dtbo-$(CONFIG_ARCH_TUNA)  += oplus/kh-camera-overlay-evb1.dtbo \

dtbo-$(CONFIG_ARCH_KERA)  += kera-camera.dtbo
#dtbo-$(CONFIG_ARCH_KERA)  += kera-camera-sensor-mtp.dtbo \
#				kera-camera-sensor-cdp.dtbo \
#				kera-camera-sensor-qrd.dtbo \
#				kera-camera-sensor-rcm.dtbo

dtbo-$(CONFIG_ARCH_KERA)  += oplus/neutron-camera-overlay-T0.dtbo
