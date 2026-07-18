# CONTEXT — DFR-RollerBlinds Ubiquitous Language

Glossary only. No implementation details.

## Terms

**Open** — The blind fully raised: fabric rolled up onto the tube, window clear.
Lift = 0 %. ("Up" always moves toward Open.)

**Closed** — The blind fully lowered: fabric covering the window. Lift = 100 %.
("Down" always moves toward Closed.)

**Lift** — The blind's position expressed as a percentage of calibrated travel,
0 % = Open, 100 % = Closed (ZCL Window Covering convention).

**Tap** — Short key press on a Calibrated, idle device: Up/Down commands full
travel; any tap during motion stops in place. Taps never move an uncalibrated
device.

**Hold (Jog)** — Press-and-hold moves the blind only while held; release stops.
The one gesture that works in every device state. On an uncalibrated device it
is deliberately the *only* way to move — a deadman control requiring a person
at the blind.

**Calibration Mode** — Local-only mode (entered by long-pressing Fn) where
travel limits are set by jogging and marking. Network motion is rejected while
active. Entered from a Calibrated state it redoes both limits; entered from
Position Unknown it only re-zeros (span kept) — a **Re-home**.

**Mark** — An Fn-tap inside Calibration Mode recording the current physical
position as a limit: first mark = Open, second mark = Closed.

**Span** — The calibrated travel distance between Open and Closed. Survives
power loss; only a full recalibration changes it.

**Position Unknown** — The device has a Span but lost trust in its position
(power died mid-move). Remote motion locks out until a Re-home.

**Re-home** — Restoring position trust after Position Unknown: jog to Open,
set one mark. Lighter than recalibration because the Span is kept.

**Calibrated** — The device knows both travel limits and its current position
within them. Only a Calibrated device accepts motion commands from the network;
an uncalibrated (or position-unknown) device can only be moved locally, hands-on,
via hold-to-jog. Remote control is a privilege calibration unlocks.

**Motor Reversed** — Per-unit installer setting that flips which motor rotation
direction counts as "Up". Exists because mechanics (front-roll vs back-roll,
motor orientation) differ per window. One authoritative value per device;
editable both locally (keypad) and remotely (z2m), and every change is visible
in both places. All position vocabulary (Open/Closed/Up/Down/Lift) is defined
*after* this setting is applied — a correctly configured unit always has "Up"
moving toward Open.
