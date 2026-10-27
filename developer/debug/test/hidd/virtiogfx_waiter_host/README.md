# Virtio IRQ waiter lifetime

Run `python3 run.py --output NEW_DIRECTORY`. Add `--tsan` for ThreadSanitizer;
the default uses AddressSanitizer and UndefinedBehaviorSanitizer.

The fixture executes the production waiter-publication/join helpers and IRQ
callback with concurrent host threads. Two IRQs pause inside Signal while the
requester detaches. A later IRQ cannot borrow that requester, and detachment
must join both original producers before its Task or signal bit may be reused.
A subsequent requester can use the same bit without receiving the old signal.

Exec's public lock resource, IRQ masking and Signal are modeled. The separate
Exec signal-delivery regression verifies Signal's synchronous-bit/delayed-wakeup
contract. Neither fixture exercises real IRQ routing, device reset, interrupt
removal, DMA or guest scheduling; full module and VM tests are separate gates.
