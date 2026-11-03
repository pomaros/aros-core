#ifndef MESA_ICD_H
#define MESA_ICD_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <hidd/vulkan_hidd.h>

/* vulkan.hidd callbacks for a Mesa Vulkan driver linked into the module */
extern const struct VulkanDriverCallbacks mesa_icd_callbacks;

#endif
