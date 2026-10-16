# Review38 corrections

H1: MetaMake selects BatterySource through the Apple architecture suffix. Its
program rule is outside make conditionals, so textual #MM scanning no longer
creates a non-Apple dependency on a missing rule.

M1: the sole SMC producer now accepts periods250..5000ms (otherwise default5000),
leaving headroom under the ten-second cache staleness limit. No additional SMC
reader or cache ABI change was introduced.

M2: eight consecutive failed sends retire an active subscriber. A successful
send or new SUBSCRIBE resets the count. Retry spacing remains250ms; a retired
slot still cannot be reused while any message ownership is outstanding.

Evidence retained in `aros-build-indicators`:

- `review38-metamake-graph/result.json`: PASS using the actual MetaMake binary
  scanning the current full Indicators metadata. Apple visits Indicator,
  BatterySource and SoundSource; Darwin visits Indicator and SoundSource only.
  Running the original metadata reproduces the unwanted Darwin BatterySource
  dependency. A command recorder replaces compilation in this focused graph test.
- `review38-hosted-configure-02.log`: real darwin-aarch64 configure completes.
- `review38-hosted-system-result.json`: full workbench-system build was attempted
  but bounded at90s during unrelated boost/geninc dependencies, exit-15/timeout.
  This is not a passing full hosted build. The initial linux configure attempt
  separately required a kernel toolchain prefix; its failure log is retained.
- `review38-host-001/result.json`: ASan/UBSan PASS, including seven failures
  without retirement, re-subscription reset, eighth-failure retirement, reuse
  only after borrowed ownership returns, and existing subscription boundaries.
- `review38-battery.log`, `review38-kernel-arch.log`, `review38-kernel.log`:
  focused native BatterySource, Apple kernel object and kernel.resource builds
  exit0. Kernel arch build retains one pre-existing pointer-sign warning.
- `review38-syntax-result.json`: strict cross Wall/Wextra/Werror exit0 with exact
  source hashes and command, supplementing the empty diagnostic log.
- `vm-031/tcg-01/result.json` and `vm-031/verification.json`: actual UP TCG and
  strict replay PASS, eight input phases, network-negative, real Control icons,
  typed commands, subscription bounds/lifecycle and absent native audio source.
  Host/guest-error logs empty; QMP normal exit0 and three-second final observation.

The subscription retirement policy is qualified by the host test; this VM has
no positive physical battery cache, so it does not exercise failed delivery of
hardware battery events. Period timing on Apple hardware, the HIDD facade, CPU
distribution, SMP and full WM15/WM16 remain unqualified.
