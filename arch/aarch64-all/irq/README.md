# AArch64 GIC bootstrap binding

`gic.c` implements GICv2 and GICv3 initialization, enable, disable, acknowledge,
end-of-interrupt and a self-target SGI. GICv2 register operations are adapted
from `arch/aarch64-native/kernel/platform_bcm2711.c`; physical addresses and the
Pi's fixed timer INTID have been replaced by the generic FDT reader's results.
GICv3 follows Arm IHI 0069 and the Arm GICv3/v4 Software Overview.

The caller supplies mapped MMIO windows and enters with CPU interrupts masked.
The bootstrap owns the distributor and its current CPU interface. This is a
single-CPU initialization contract. CPU topology admission, cross-CPU IPIs,
multiple redistributor regions, ITS/MSI, interrupt affinity changes and hotplug
require further implementation. Unsupported configurations fail explicitly.

Supported bindings are `arm,gic-400`, `arm,cortex-a15-gic` and `arm,gic-v3`.
`reg` contains distributor followed by CPU-interface (v2) or redistributor (v3)
windows. The v3 redistributor is selected by MPIDR affinity, with the optional
`redistributor-stride` respected. Reset and power-state polling are bounded.

GIC interrupt cells describe SPI/PPI, source index and trigger. Partitioned PPIs
are refused. GICv2's fixed-polarity PPI encoding accepts the Pi's level-low
flag; the controller's configurable edge/level bit is set accordingly.
Acknowledgement tokens are preserved through EOI, including GICv2's SGI sender.
The caller must classify INTIDs 1020..1023 as special/spurious and must not pass
those tokens to `AROSGICEnd`. In GICv3, initialization leaves disabled SPIs in
group 0; enabling an SPI explicitly assigns it to non-secure group 1 before
unmasking it.

The qualification guest uses the architectural virtual timer (CNTV), available
both to a guest and to bare metal. QEMU/HVF leaves CNTP_TVAL undefined; this is
not evidence about native Apple timer delivery. Its `arm,armv8-timer` IRQ is
resolved from entry 2, rather than a fixed INTID. The OS timer/scheduler adapter
is a separate integration step.
