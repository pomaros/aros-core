# Protected tile overlay ordering

`python3 run.py --output /tmp/overlay-fresh` extracts current production ordering
bodies and checks ASan/UBSan execution with recording layer-list operations.
Normal front/back creation uses priority20, backdrop10 and protected overlay30.
UpfrontLayer stops at another priority class; BehindLayer stops at the bottom of
its own class; explicit MoveLayerInFrontOf cannot cross a protected boundary.
Window validation rejects activation, non-toolbox/non-borderless, parent, GZZ and
backdrop combinations. No public Window struct changes or global window registry
are introduced. CloseWindow retains its normal layer deletion ownership.

This host fixture does not execute allocation/compositor clip recomputation or
prove actual pixels/focus. A guest must open and front a foreign window during lift,
check layer order and pixels, then close lift and verify ordinary ordering resumes.
