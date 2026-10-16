/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_FDT_MAP_H
#define AROS_AARCH64_APPLE_STAGE2_FDT_MAP_H

#include <stdint.h>

#include <aros/apple/bootinfo.h>
#include <aros/apple/startup.h>

#define AROS_APPLE_DEVICE_MAP_VERSION       UINT32_C(6)
#define AROS_APPLE_DEVICE_MAP_PROFILE_NONE  UINT32_C(0)
#define AROS_APPLE_DEVICE_MAP_PROFILE_APPLE UINT32_C(1)
#define AROS_APPLE_DEVICE_MAP_PROFILE_VIRT UINT32_C(2)
#define AROS_APPLE_DEVICE_MAP_MAX_RANGES    128U
#define AROS_APPLE_DEVICE_MAP_MAX_ROLES     16U

#define AROS_APPLE_DEVICE_ROLE_CONSOLE      UINT32_C(1)
#define AROS_APPLE_DEVICE_ROLE_AIC          UINT32_C(2)
#define AROS_APPLE_DEVICE_ROLE_PMGR         UINT32_C(3)
#define AROS_APPLE_DEVICE_ROLE_EXCLUDED      UINT32_C(4)
#define AROS_APPLE_DEVICE_ROLE_EVIDENCE      UINT32_C(5)
/* A PCI host memory aperture: mapped posted (Device-nGnRE). */
#define AROS_APPLE_DEVICE_ROLE_PCI_WINDOW    UINT32_C(6)
#define AROS_APPLE_DEVICE_ROLE_GIC           UINT32_C(7)
#define AROS_APPLE_DEVICE_ROLE_PCI_CONFIG    UINT32_C(8)
#define AROS_APPLE_DEVICE_ROLE_MSI_FRAME     UINT32_C(9)

struct AROSAppleDeviceRange {
    uint64_t start;
    uint64_t end;
};

struct AROSAppleDeviceRole {
    uint32_t kind;
    uint32_t instance;
    uint64_t start;
    uint64_t end;
};

struct AROSAppleDeviceMap {
    uint32_t version;
    uint32_t size;
    uint32_t profile;
    uint32_t entry_count;
    uint32_t source_count;
    uint32_t enabled_node_count;
    uint32_t disabled_node_count;
    uint32_t role_count;
    uint64_t mapped_bytes;
    struct AROSAppleDeviceRole roles[AROS_APPLE_DEVICE_MAP_MAX_ROLES];
    struct AROSAppleDeviceRange ranges[AROS_APPLE_DEVICE_MAP_MAX_RANGES];
};

_Static_assert(sizeof(struct AROSAppleDeviceRange) == 16,
    "AROSAppleDeviceRange size");
_Static_assert(sizeof(struct AROSAppleDeviceRole) == 24,
    "AROSAppleDeviceRole size");
_Static_assert(sizeof(struct AROSAppleDeviceMap) == 2472,
    "AROSAppleDeviceMap size");

const struct AROSAppleDeviceRole *AROSAppleStage2FindDeviceRole(
    const struct AROSAppleDeviceMap *device_map,
    uint32_t kind,
    uint32_t instance);

int AROSAppleStage2BuildDeviceMap(
    const struct AROSBootInfo *boot_info,
    const struct AROSAppleMemoryPlan *memory_plan,
    struct AROSAppleDeviceMap *device_map);

int AROSAppleStage2DeviceMapValidate(
    const struct AROSAppleDeviceMap *device_map,
    const struct AROSAppleMemoryPlan *memory_plan);

#endif /* AROS_AARCH64_APPLE_STAGE2_FDT_MAP_H */
