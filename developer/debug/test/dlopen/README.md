<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# dlopen qualification

`dl-host.py --output /absolute/new/output` compiles the production
`__dl.c`, `dlopen.c`, `dlsym.c`, `dlclose.c`, `dlerror.c` and `dladdr.c`
against modeled exec and DOS with AddressSanitizer and
UndefinedBehaviorSanitizer. It covers the lookup order and the version
fallback, refusal of an unmarked program without calling it, of a wrong
version, a missing descriptor and a failing Init, the modes (RTLD_GLOBAL and
unknown bits refused, NOLOAD, NODELETE), dlsym and the global search,
dladdr, a module whose code sits behind LoadSeg's FullJumpVec, per-task
dlerror, the exit-list release and eight tasks loading and releasing one
module concurrently. Exec and DOS are modeled; it is not runtime evidence.

`dlopen-check.py` builds, against a `posix-check.py` output, `DLTest`, its
module `dltest.so` and the matching posixc.library. In a guest, with that
library as the boot component (`tools/rebundle-components` in the integration
repository), `DLTest MODULE` checks the C interface from end to end:
constructors, data and functions, the C library and dos.library through the
module's own bases, reference counts, reloading, and that a module run as a
command does nothing. With `KEEP` it leaves the module loaded at exit; its
destructor then writes `RAM:DLTest.fini`.

`DLVulkan [GPU]` loads the Vulkan loader the way wgpu and ash do on Linux:
`dlopen("libvulkan.so.1")` finds `LIBS:dl/libvulkan.so`, built from
`workbench/hidds/vulkan/vulkan_loader.c` with `--export-prefix vk`, then
`vkGetInstanceProcAddr` and the loader's entries. The loader opens
`vulkan.hidd` itself, so the test passes with no Vulkan module resident
(`Avail FLUSH` first), and reports at least Vulkan 1.1 with or without a
driver. Without `GPU`, a failing `vkCreateInstance` (no driver registered) is
accepted; with `GPU` an instance and a physical device are required.
