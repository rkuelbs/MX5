# Shared Vehicle Interfaces

## CAN database

`miata.dbc` is the authoritative vehicle-wide CAN definition. Dash/logger
canonical signal names are formed as `Sender.signal`; CAN message names are not
part of the canonical name.

## Wheel control module

The WCM transmits a periodic status message and an event message on every button
edge. The periodic message is authoritative for current state and health; the
event message provides low-latency edges and completed press duration.

| Button ID / input bit | Function | Consumer |
|---:|---|---|
| 0 | Down | Dash |
| 1 | Up | Dash |
| 2 | Menu | Dash |
| 3 | Ack/Enter | Dash |
| 4 | Left turn | PDM |
| 5 | Right turn | PDM |
| 6 | Wiper | PDM |
| 7 | Flash | PDM |

`WCM.inputs` is the eight-bit button-state bitmask. `WCM.counter` increments
modulo 256 on status messages. A counter delta of one, including `255 -> 0`, is
normal; a larger delta indicates missed status messages.

Event messages contain `WCM.event_id` using the table above and
`WCM.event_type`, where 0 is a falling/released edge and 1 is a rising/pressed
edge. `WCM.event_length` is an unscaled integer duration in milliseconds and is
valid only for falling edges.
