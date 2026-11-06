# Host GPU probe (M1) — 2026-11-06, Linux 7.0.0-42-generic aarch64

VERDICT: no hardware GPU acceleration. virtio-gpu is 2D-only (3D_FEATURES=0, no blob, no capsets): no virgl, no Venus. Only llvmpipe (GL) / lavapipe (VK) = SOFTWARE.

```
## vulkaninfo --summary
'DISPLAY' environment variable not set... skipping surface info
TU: error: ../src/freedreno/vulkan/tu_knl.cc:369: failed to open device /dev/dri/renderD128 (VK_ERROR_INCOMPATIBLE_DRIVER)
WARNING: [Loader Message] Code 0 : ICD for selected physical device does not export vkGetPhysicalDeviceDisplayPlanePropertiesKHR!
WARNING: [Loader Message] Code 0 : ICD for selected physical device does not export vkGetPhysicalDeviceDisplayPropertiesKHR!
==========
VULKANINFO
==========

Vulkan Instance Version: 1.4.341


Instance Extensions: count = 26
-------------------------------
VK_EXT_acquire_drm_display             : extension revision 1
VK_EXT_acquire_xlib_display            : extension revision 1
VK_EXT_debug_report                    : extension revision 10
VK_EXT_debug_utils                     : extension revision 2
VK_EXT_direct_mode_display             : extension revision 1
VK_EXT_display_surface_counter         : extension revision 1
VK_EXT_headless_surface                : extension revision 1
VK_EXT_layer_settings                  : extension revision 2
VK_EXT_surface_maintenance1            : extension revision 1
VK_EXT_swapchain_colorspace            : extension revision 5
VK_KHR_device_group_creation           : extension revision 1
VK_KHR_display                         : extension revision 23
VK_KHR_external_fence_capabilities     : extension revision 1
VK_KHR_external_memory_capabilities    : extension revision 1
VK_KHR_external_semaphore_capabilities : extension revision 1
VK_KHR_get_display_properties2         : extension revision 1
VK_KHR_get_physical_device_properties2 : extension revision 2
VK_KHR_get_surface_capabilities2       : extension revision 1
VK_KHR_portability_enumeration         : extension revision 1
VK_KHR_surface                         : extension revision 25
VK_KHR_surface_maintenance1            : extension revision 1
VK_KHR_surface_protected_capabilities  : extension revision 1
VK_KHR_wayland_surface                 : extension revision 6
VK_KHR_xcb_surface                     : extension revision 6
VK_KHR_xlib_surface                    : extension revision 6
VK_LUNARG_direct_driver_loading        : extension revision 1

Instance Layers: count = 5
--------------------------
VK_LAYER_INTEL_nullhw       INTEL NULL HW                                                1.1.73   version 1
VK_LAYER_MESA_anti_lag      Open-source implementation of the VK_AMD_anti_lag extension. 1.4.303  version 1
VK_LAYER_MESA_device_select Linux device selection layer                                 1.4.303  version 1
VK_LAYER_MESA_overlay       Mesa Overlay layer                                           1.4.303  version 1
VK_LAYER_MESA_screenshot    Mesa Screenshot layer                                        1.4.303  version 1

Devices:
========
GPU0:
	apiVersion         = 1.4.335
	driverVersion      = 26.0.8
	vendorID           = 0x10005
	deviceID           = 0x0000
	deviceType         = PHYSICAL_DEVICE_TYPE_CPU
	deviceName         = llvmpipe (LLVM 21.1.8, 128 bits)
	driverID           = DRIVER_ID_MESA_LLVMPIPE
	driverName         = llvmpipe
	driverInfo         = Mesa 26.0.8-1ubuntu0.3 (LLVM 21.1.8)
	conformanceVersion = 1.3.1.1
	deviceUUID         = 6d657361-3236-2e30-2e38-2d3175627500
	driverUUID         = 6c6c766d-7069-7065-5555-494400000000

## ls /usr/share/vulkan/icd.d
asahi_icd.json
broadcom_icd.json
freedreno_icd.json
gfxstream_vk_icd.json
intel_icd.json
lvp_icd.json
nouveau_icd.json
panfrost_icd.json
powervr_mesa_icd.json
radeon_icd.json
virtio_icd.json
## drm
DRIVER=virtio-pci
PCI_CLASS=38000
PCI_ID=1AF4:1050
PCI_SUBSYS_ID=1AF4:1100
PCI_SLOT_NAME=0000:07:00.0
MODALIAS=pci:v00001AF4d00001050sv00001AF4sd00001100bc03sc80i00
ls: cannot open file '/sys/kernel/debug/dri': Permission denied

## virtgpu GETPARAM (as ubuntu + render group; /dev/dri/renderD128 is root:render, user ubuntu is NOT in render, so default Mesa TU/virtio ICDs fail to open it)
3D_FEATURES r=0 val=0x0
CAPSET_QUERY_FIX r=0 val=0x1
RESOURCE_BLOB r=0 val=0x0
HOST_VISIBLE r=0 val=0x0
CROSS_DEVICE r=0 val=0x0
CONTEXT_INIT r=0 val=0x0
SUPPORTED_CAPSET_IDs r=0 val=0x0

## Venus forced (VK_DRIVER_FILES=virtio_icd.json), render group:
'DISPLAY' environment variable not set... skipping surface info
ERROR: [Loader Message] Code 0 : setup_loader_term_phys_devs:  Failed to detect any valid GPUs in the current config
ERROR at ./vulkaninfo/./vulkaninfo.h:249:vkEnumeratePhysicalDevices failed with ERROR_INITIALIZATION_FAILED

## virgl forced (MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu), Xvfb :99, render group:
name of display: :99
display: :99  screen: 0
direct rendering: Yes
Extended renderer info (GLX_MESA_query_renderer):
    Vendor: Mesa (0xffffffff)
    Device: llvmpipe (LLVM 21.1.8, 128 bits) (0xffffffff)
    Version: 26.0.8
    Accelerated: no
    Preferred profile: core (0x1)
    Max core profile version: 4.5
    Max compat profile version: 4.5
    Max GLES1 profile version: 1.1
    Max GLES[23] profile version: 3.2
OpenGL vendor string: Mesa
OpenGL renderer string: llvmpipe (LLVM 21.1.8, 128 bits)
OpenGL core profile version string: 4.5 (Core Profile) Mesa 26.0.8-1ubuntu0.3
OpenGL core profile shading language version string: 4.50
OpenGL core profile context flags: (none)
OpenGL core profile profile mask: core profile

OpenGL version string: 4.5 (Compatibility Profile) Mesa 26.0.8-1ubuntu0.3
OpenGL shading language version string: 4.50
OpenGL context flags: (none)
OpenGL profile mask: compatibility profile

OpenGL ES profile version string: OpenGL ES 3.2 Mesa 26.0.8-1ubuntu0.3
OpenGL ES profile shading language version string: OpenGL ES GLSL ES 3.20


## glxinfo -B (Xvfb :99 default)
name of display: :99
display: :99  screen: 0
```
