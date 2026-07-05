#include "util_shared_res.h"
#include "log/log.h"

#ifdef _WIN32
#include <winioctl.h>
#include <cstdio>
#endif

namespace dxvk {

#ifdef _WIN32
  #define IOCTL_SHARED_GPU_RESOURCE_OPEN             CTL_CODE(FILE_DEVICE_VIDEO, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

  HANDLE openKmtHandle(HANDLE kmt_handle) {
    HANDLE handle = ::CreateFileA("\\\\.\\SharedGpuResource", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
      return handle;

    struct
    {
        unsigned int kmt_handle;
        WCHAR name[1];
    } shared_resource_open = {0};
    shared_resource_open.kmt_handle = reinterpret_cast<uintptr_t>(kmt_handle);

    bool succeed = ::DeviceIoControl(handle, IOCTL_SHARED_GPU_RESOURCE_OPEN, &shared_resource_open, sizeof(shared_resource_open), NULL, 0, NULL, NULL);
    if (!succeed) {
      ::CloseHandle(handle);
      return INVALID_HANDLE_VALUE;
    }
    return handle; 
  }

  #define IOCTL_SHARED_GPU_RESOURCE_SET_METADATA           CTL_CODE(FILE_DEVICE_VIDEO, 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
  #define IOCTL_SHARED_GPU_RESOURCE_GET_METADATA           CTL_CODE(FILE_DEVICE_VIDEO, 5, METHOD_BUFFERED, FILE_READ_ACCESS)

  // native-windows fallback transport for the dxvk<->vkd3d shared-texture
  // metadata. \\.\SharedGpuResource only exists under wine, so on native
  // windows the ioctls fail and the decoded-video texture metadata never
  // reaches vkd3d (audio-only video in msfs). stash it in a temp file keyed by
  // the raw shared-handle value; vkd3d-proton uses the identical scheme.
  static void sharedMetadataNativePath(HANDLE handle, char *path, size_t size) {
    char tmp[MAX_PATH];
    DWORD n = ::GetTempPathA(sizeof(tmp), tmp);
    if (!n || n >= sizeof(tmp)) { path[0] = '\0'; return; }
    snprintf(path, size, "%svkd3d-dxvk-shared-%llx.bin", tmp, (unsigned long long)(uintptr_t)handle);
  }

  static bool setSharedMetadataNative(HANDLE handle, void *buf, uint32_t bufSize) {
    char path[MAX_PATH];
    sharedMetadataNativePath(handle, path, sizeof(path));
    if (!path[0])
      return false;
    HANDLE file = ::CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE)
      return false;
    DWORD written = 0;
    bool ret = ::WriteFile(file, buf, bufSize, &written, NULL) && written == bufSize;
    ::CloseHandle(file);
    return ret;
  }

  static bool getSharedMetadataNative(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    char path[MAX_PATH];
    sharedMetadataNativePath(handle, path, sizeof(path));
    if (!path[0])
      return false;
    HANDLE file = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
      return false;
    DWORD readBytes = 0;
    bool ret = ::ReadFile(file, buf, bufSize, &readBytes, NULL) && readBytes > 0;
    ::CloseHandle(file);
    if (metadataSize)
      *metadataSize = readBytes;
    return ret;
  }

  bool setSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize) {
    DWORD retSize;
    if (::DeviceIoControl(handle, IOCTL_SHARED_GPU_RESOURCE_SET_METADATA, buf, bufSize, NULL, 0, &retSize, NULL))
      return true;
    // wine device absent -> native windows fallback
    return setSharedMetadataNative(handle, buf, bufSize);
  }

  bool getSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    DWORD retSize;
    bool ret = ::DeviceIoControl(handle, IOCTL_SHARED_GPU_RESOURCE_GET_METADATA, NULL, 0, buf, bufSize, &retSize, NULL);
    if (ret) {
      if (metadataSize)
        *metadataSize = retSize;
      return true;
    }
    // wine device absent -> native windows fallback
    return getSharedMetadataNative(handle, buf, bufSize, metadataSize);
  }
#else
  HANDLE openKmtHandle(HANDLE kmt_handle) {
    Logger::warn("openKmtHandle: Shared resources not available on this platform.");
    return INVALID_HANDLE_VALUE;
  }

  bool setSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize) {
    Logger::warn("setSharedMetadata: Shared resources not available on this platform.");
    return false;
  }

  bool getSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    Logger::warn("getSharedMetadata: Shared resources not available on this platform.");
    return false;
  }
#endif

}
