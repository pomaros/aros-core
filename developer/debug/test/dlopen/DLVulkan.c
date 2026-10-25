/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: DLVulkan [GPU/S] - load the Vulkan loader as a dlopen module,
          the way wgpu and ash do on Linux

    dlopen("libvulkan.so.1"), dlsym("vkGetInstanceProcAddr"), and the
    loader's own entries through it. Without GPU only that much is checked:
    vkCreateInstance then may fail with a Vulkan error, never a crash. With
    GPU an instance must be made and at least one physical device found.
    The last line is "DLVulkan: result=PASSED" (0) or "result=FAIL" (10).
*/

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *(*gipa_t)(void *, const char *);
typedef int32_t (*version_t)(uint32_t *);
typedef int32_t (*create_t)(const void *, const void *, void **);
typedef void (*destroy_t)(void *, const void *);
typedef int32_t (*enumerate_t)(void *, uint32_t *, void **);

/* The VkInstanceCreateInfo and VkApplicationInfo layouts. */
struct app_info { int32_t sType; const void *pNext; const char *name; uint32_t version;
                  const char *engine; uint32_t engine_version; uint32_t api; };
struct create_info { int32_t sType; const void *pNext; uint32_t flags; const struct app_info *app;
                     uint32_t layers; const char *const *layer_names;
                     uint32_t extensions; const char *const *extension_names; };

static int failures;

static void check(int ok, const char *what)
{
    printf("DLVulkan: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

int main(int argc, char **argv)
{
    int gpu = argc > 1 && strcmp(argv[1], "GPU") == 0;
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    gipa_t gipa;
    version_t version;
    create_t create;
    uint32_t api = 0;

    check(lib != NULL, "dlopen libvulkan.so.1");
    if (lib == NULL)
    {
        printf("DLVulkan: dlerror %s\nDLVulkan: result=FAIL\n", dlerror());
        return 10;
    }
    gipa = (gipa_t)dlsym(lib, "vkGetInstanceProcAddr");
    check(gipa != NULL, "dlsym vkGetInstanceProcAddr");
    version = gipa ? (version_t)gipa(NULL, "vkEnumerateInstanceVersion") : NULL;
    /* Without a driver the loader reports its own version, as Khronos' does:
       never below 1.1, which Dawn and wgpu require before anything else */
    check(version != NULL && version(&api) == 0 && api >= ((1u << 22) | (1u << 12)),
          "vkEnumerateInstanceVersion through the loader");
    printf("DLVulkan: instance API %u.%u\n", api >> 22, (api >> 12) & 0x3ff);
    create = gipa ? (create_t)gipa(NULL, "vkCreateInstance") : NULL;
    check(create != NULL, "vkCreateInstance resolved");
    if (create != NULL)
    {
        struct app_info app = { 0, NULL, "DLVulkan", 1, "none", 1, (1u << 22) | (1u << 12) };
        struct create_info info = { 1, NULL, 0, &app, 0, NULL, 0, NULL };
        void *instance = NULL;
        int32_t r = create(&info, NULL, &instance);

        printf("DLVulkan: vkCreateInstance returned %d\n", (int)r);
        if (gpu)
        {
            enumerate_t enumerate = (enumerate_t)gipa(instance, "vkEnumeratePhysicalDevices");
            uint32_t count = 0;

            check(r == 0 && instance != NULL, "instance made");
            check(enumerate != NULL && enumerate(instance, &count, NULL) == 0 && count > 0,
                  "a physical device");
            printf("DLVulkan: %u physical device(s)\n", count);
        }
        if (r == 0 && instance != NULL)
        {
            destroy_t destroy = (destroy_t)gipa(instance, "vkDestroyInstance");
            if (destroy)
                destroy(instance, NULL);
        }
    }
    check(dlclose(lib) == 0, "dlclose");
    printf("DLVulkan: result=%s\n", failures ? "FAIL" : "PASSED");
    return failures ? 10 : 0;
}
