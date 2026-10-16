# Run on a Colab GPU runtime (colab exec -s <name> -f tools/colab/vulkan_setup.py).
# Colab ships the NVIDIA userspace in /usr/lib64-nvidia but no Vulkan ICD JSON, and the
# container only grants compute,utility caps, so libGLX_nvidia fails as ICD. libEGL_nvidia works.
# Verified 2026-11-07 on Tesla T4, driver 580.82.07: deviceType DISCRETE_GPU, Vulkan 1.4.312.
import json, os, subprocess as s
s.run("apt-get -qq update && DEBIAN_FRONTEND=noninteractive apt-get -qq install -y vulkan-tools", shell=True)
os.makedirs("/etc/vulkan/icd.d", exist_ok=True)
json.dump({"file_format_version": "1.0.1", "ICD": {"library_path": "/usr/lib64-nvidia/libEGL_nvidia.so.0", "api_version": "1.3.0"}},
          open("/etc/vulkan/icd.d/nvidia_icd.json", "w"))
print(s.run("LD_LIBRARY_PATH=/usr/lib64-nvidia vulkaninfo --summary 2>&1 | grep -E 'deviceName|deviceType|apiVersion'",
            shell=True, capture_output=True, text=True).stdout)
