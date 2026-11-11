# package/rknpu — RKNPU board-side packages

OpenWrt packages for bringing up the Rockchip NPU (RK3566 LubanCat 1N)
from user space: the runtime, the adb link, the toolkit server, and
example programs.

| Package | Installs | Purpose |
|---|---|---|
| `rknn-runtime` | `/usr/lib/librknnrt.so`, `/usr/lib/libgcompat.so`, `/lib/{ld-linux-aarch64.so.1,libc.so.6,libm.so.6,libpthread.so.0,libdl.so.2}` | NPU runtime + musl compat layer |
| `adbd` | `/opt/adbd/*` (glibc bundle), `/etc/init.d/adbd` | adb-over-network on tcp:5555 |
| `rknn-server` | `/usr/lib/rknn_server`, `/usr/bin/{rknn_server,nohup,transfer_proxy_relay}` | PC rknn-toolkit2 "connect to board" |
| `rknn-examples` | `/usr/bin/{rknn_smoke_test,rknn_test,rknn_bench}` | NPU bring-up + benchmark |

All four are binary-only: their prebuilt payloads live under `files/`
(sourced from `../../rknpu/`).  `rknn-examples` compiles the C sources
under `src/` with the OpenWrt musl toolchain and links against the
runtime shipped by `rknn-runtime`.

Dependency graph:

```
rknn-examples ─┐
               ├─> rknn-runtime ─> libstdcpp, libgcc
rknn-server ───┘
     └─> adbd
```

## Build

```sh
make package/rknpu/rknn-runtime/compile V=s
make package/rknpu/adbd/compile V=s
make package/rknpu/rknn-server/compile V=s
make package/rknpu/rknn-examples/compile V=s
```

## Install / use on the board

```sh
apk add rknn-runtime adbd rknn-server rknn-examples
rknn_smoke_test /root/resnet18_for_rk3566_rk3568.rknn 1
rknn_bench /root/resnet18_for_rk3566_rk3568.rknn /root/mobilenet_v1.rknn 200
```

## musl compatibility

`librknnrt.so` and `rknn_server` are Rockchip's glibc builds.  On the
musl target they load via `libgcompat.so` (glibc-internal symbol aliases:
`*_finite`, `mmap64`, `__pthread_key_create`, `__strdup`) plus the
`libc.so.6` / `libm.so.6` / `libpthread.so.0` / `libdl.so.2` soname
aliases provided by musl's loader.  `librknnrt.so` is pre-patched with
`patchelf --add-needed libgcompat.so`, so any program linked with
`-lrknnrt` loads the shim automatically.

Do **not** `patchelf` the pristine `rknn_server` binary — it corrupts its
ELF; the `LD_PRELOAD` wrapper at `/usr/bin/rknn_server` is the intended
entry point.
