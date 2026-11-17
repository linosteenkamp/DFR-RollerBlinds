# CONTEXT — DFR-RollerBlinds Ubiquitous Language

This file is a glossary only. It does not contain implementation details.

## Terms

**Open**: the blind is fully up. The fabric is rolled onto the tube, and
the window is clear. Lift = 0 %. ("Up" always moves the blind toward Open.)

**Closed**: the blind is fully down. The fabric covers the window.
Lift = 100 %. ("Down" always moves the blind toward Closed.)

**Lift**: the position of the blind, as a percentage of the calibrated
travel. 0 % = Open, 100 % = Closed. This is the ZCL Window Covering
convention.

**Tap**: a short key press. On an idle device that is Calibrated, a tap
on Up or Down starts a full travel. A tap on any key during a move stops
the blind at its current position. A tap never moves a device that is
not calibrated.

**Hold (Jog)**: press and hold a key to move the blind. The blind moves
only while you hold the key. When you release the key, the blind stops.
This gesture operates in all device states. On a device that is not
calibrated, it is intentionally the *only* way to move the blind. It is a
deadman control: a person must be at the blind.

**Calibration Mode**: a local-only mode. To enter it, do a long press on
Fn. In this mode, you jog the blind and mark the travel limits. The device
rejects motion commands from the network while this mode is active. If
you enter it from a Calibrated state, you set the two limits again. If
you enter it from Position Unknown, you only set the zero again, and the
Span stays. This is a **Re-home**.

**Mark**: a tap on Fn in Calibration Mode. It records the current
physical position as a limit. The first mark is Open. The second mark is
Closed.

**Span**: the calibrated travel distance between Open and Closed. The
Span stays after a power failure. Only a full calibration changes it. A
Motor Reversed toggle also erases it.

**Position Unknown**: the device has a Span, but it cannot trust its
position. This occurs when the power fails during a move. Remote motion
is locked out until a Re-home. The device also goes to Position Unknown
when you abort a Calibration Mode session in which you jogged the blind.
An abort is a long press on Fn or a timeout.

**Re-home**: the procedure that restores trust in the position after
Position Unknown. Jog the blind to Open, then set one mark. This is
shorter than a full calibration because the Span stays.

**Calibrated**: the device knows the two travel limits and its current
position between them. Only a Calibrated device accepts motion commands
from the network. You can move a device that is not calibrated (or is in
Position Unknown) only at the blind, with hold-to-jog. Calibration
unlocks remote control.

**Motor Reversed**: an installer setting for each unit. It changes which
direction of motor rotation is "Up". The setting is necessary because
the mechanics are different at each window (front roll or back roll, and
the motor orientation). Each device has one value, and z2m sets it.
Before, a keypad gesture also set it (hold Up+Down for 3 s). That gesture
was removed for two reasons:

- It needed a suppression latch, and one stuck key line could lock that
  latch.
- A 3 s hold erased the calibration without a warning.

All the position terms (Open, Closed, Up, Down and Lift) apply *after*
the device applies this setting. On a correctly set unit, "Up" always
moves the blind toward Open.
