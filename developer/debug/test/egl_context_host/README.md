# EGL presentation context regression

This HOST test extracts the actual AROS backend MakeCurrent/SwapBuffers callbacks
and native GLA current-context getter. Two pthreads share a display and bind
distinct contexts/window surfaces in a controlled order. After the second bind,
the first task must present its own context; unbinding that task must leave the
other task's presentation intact.

Mesa's internal GL context, its state tracker and the native AROS wrapper have
distinct addresses in the fixture. Native context creation registers the wrapper
in the tracker's frontend owner field. The actual getter must recover that owner,
including NULL after unbinding. The fixture uses the production registration
statement, but does not execute the complete context-creation function.

Select the production tree and its exact commit, with a fresh output directory
outside that tree:

```sh
python3 /ABS/AROS/developer/debug/test/egl_context_host/run.py \
  --source /ABS/AROS --ref COMMIT --output /ABS/NEW-OUTPUT
```

Dirty source requires `--allow-dirty-diagnostic` and cannot produce final PASS.
The actual callbacks pass strict C11 warnings and ASan/UBSan. Two compiled source
mutations must abort at their exact semantic assertions: selecting the display's
last context prevents the first task from presenting its own surface, and
returning the internal Mesa context fails the native wrapper identity check.
An unrelated nonzero exit is insufficient. The suite requires no historical
Git objects. LeakSanitizer is disabled on Darwin, where it is unsupported.

Mesa resource calls, current-context storage and native GL calls are shimmed.
This test does not establish the native calling ABI, real AROS task-local storage,
GPU rendering/presentation, surface persistence or allocation-failure recovery.
Native qualification requires two actual tasks and windows with controlled binds,
framebuffer and presented-pixel checks, and an independent unbind case. Pbuffer
backing ownership and checked target changes are separate contracts.
