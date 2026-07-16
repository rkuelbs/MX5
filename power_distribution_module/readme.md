Power Distribution Module (PDM) 
===============================
Hardware Description
====================
Inputs
------
- acc_in
    - Discrete input with pullup resistor
    - Pulled down by ignition switch in ACC or IGN position
- ign_in
    - discrete input with pullup resistor
    - Pulled down by ignition siwtch in IGN position
- unlock
    - discrete input with pullup resistor
    - pulled low for short pulse by keyless entry module when door unlock is commanded by remote fob
- lock
    - discrete input with pullup resistor
    - pulled low for short pulse by keyless entry module when door lock is commanded by remote fob
- door
    - discrete input with pullup resistor
    - pulled low when either door is open
- window_left_cmd
    - adc input connected to resistor ladder to multiplex window switches
    - resistors connected to Left UP, Left DOWN
- window_right_cmd
    - adc input connected to resistor ladder to multiplex window switches
    - resistors connected to Right UP, Right DOWN
- blower_cmd
    - adc input connected to resistor ladder to multiplex hvac options
    - OFF, LOW, MED, HIGH
- ac command
    - discrete input with pullup resistor
    - pulled low to request ac
- oat
    - adc input for thermistor for ambient temp sensing
- hvac_psi
    - adc input for ac compressor discharge pressure sensor
- battery_volt
    - adc measuring input voltage on battery terminal
- sensor_5v_ref
    - adc to measure 5V sensor reference power
- rpi_halt
    - discrete input from RPI GPIO to detect when safe to power down
- key_detect
    - discrete input to detect key inserted
- service_detect
    - discrete input to detect diagnostics cable connected
- fuel_level
    - adc input for reading 7-95 ohm fuel level sender

Onboard Sensors
---------------
- map
    - absolute pressure sensor onboard for boost control.  Analog, SPI or I2C
- temp
    - 3x I2C temp sensors across board to verify PCBA component / enclosure temps
- window_current_left
    - adc for left window current sense
- window_current_right
    - adc for right window current sense

Outputs
-------
- acc_out
    - low-side driver to enable external relay
    - enabled in Accessory or Ignition states
- ign_out
    - low side driver to enable external relay
    - enabled in Ignition state
- dash_rpi
    - high-side driver to power dashboard raspberry pi
    - 12VDC high-side driver followed by 5VDC buck converter
    - enabled in Wake, Accessory, Ignition, KeyNotDetected, Shutdown states
- dash_lcd
    - high-side driver to power dashboard LCD screen
    - enabled in Wake, Accessory, Ignition, KeyNotDetected states
- boost
    - low-side driver to drive boost solenoid with PWM
- fan_left
    - low-side driver to enable external relay
- fan_right
    - low-side driver to enable external relay
- compressor
    - low-side driver to power relay for AC compressor clutch coil
- blower_a
    - low-side driver to enable external relay for blower a
    - blower control AB relays - 00 = off, 01 = low, 10 = medium, 11 = high
- blower_b
    - low-side driver to enable external relay for blower b
    - blower control AB relays - 00 = off, 01 = low, 10 = medium, 11 = high
- alt_ctrl
    - pwm output to control Miata NB alternator
- headlights
    - low-side driver to enable external relay
- high_beam
    - low-side driver to enable external relay
- park
    - high-side driver to power parking lights
- turn_left
    - high-side driver to power left turn signals
- turn_right
    - high-side driver to power right turn signals
- courtesy
    - pwm low-side driver to power interior lamps
- window_left
    - full h-bridge for left window up/down control
- window_right
    - full h-bridge for right window up/down control
- rpi_shutdown_req
    - discrete output to request RPI shutdown
- sensor_5v_en
    - enable 5V reference output for sensors
- can_standby
    - output to enable can transceiver standby in low power mode
- key_power_en
    - enable output power to I2C key circuit

Serial Communications
---------------------
- CAN Bus
    - CAN bus is the main communications bus with the dash raspberry pi (rpi), engine control module (ecm), wheel control module (wcm), and data acquisition module (dam).
- key
    - I2C bus for a removable serial number IC to be used as a keycode

Behavior Description
====================

Power States
------------

### Idle
Idle state is a low-power state where all outputs are disabled.  PDM MCU sleeps in a low-power state waiting to be awakened.

### Wake
Wake state brings the PDM MCU into a normal power state and enables the hihg-side driver to power-up the dash_rpi and allow it to boot.  In the Wake state courtesy light control, key reading, and CAN communications become enabled.

### Shutdown
Shutdown state is a state between Wake and Idle, where the PDM requests a shutdown of the rpi and waits for a halt confirmation before cutting power to the rpi and proceeding to idle.

### KeyNotDetected
KeyNotDetected is a state reached when accessory or ignition power is requested, but the proper key has not been detected.  The dash_lcd enables to display a key not detected message.

### Accessory
Accessory state enables dash_rpi, dash_lcd, and acc_out (as well as all items enabled in Wake).  Accessory also allows window control.

### Ignition
Ignition state enables ign_out (as well as all items enabled in Wake and Accessory).  Ignition state also enables boost control and hvac control.

Power State FSM Description
---------------------------

```
stateDiagram-v2
    [*] --> Idle : !ignition
    [*] --> IgnitionProvisional : ignition
    Idle --> Wake : unlock
    Idle --> Wake : door
    Idle --> Wake : accessory
    Idle --> Wake : keyDetected
    Wake --> Shutdown : timeout
    Wake --> Shutdown : lock
    Wake --> Accessory : accessory && keyValid
    Wake --> KeyNotDetected : accessory && !keyValid
    KeyNotDetected --> Wake : timeout
    KeyNotDetected --> Wake : !accessory
    Accessory --> Wake : !accessory
    Accessory --> Ignition : ignition
    Ignition --> Accessory : !ignition
    Shutdown --> Idle : timeout
    Shutdown --> Idle : halt
    IgnitionProvisional --> Ignition : ignition && keyValid
    IgnitionProvisional --> KeyNotDetected : ignition && !keyValid
    IgnitionProvisional --> Wake : !ignition
```

HVAC Control
------------

HVAC controls utilize an binary-weighted resistor DAC for conversion of 4 controls to an analog signal.  The ADC value may be decoded to ascertain commanded blower speed and AC status.  Resistors should be chosen so that the 4 input switches control the 4 most significant bits of the ADC.

### Blower Control
Blower control is active only when ignition is active (in Ignition or IgnitionProvisional states), and the blower motor is commanded by activating blower relays A and B.
- For AB:
    - 00 = off, 01 = low, 10 = medium, 11 = high

### Compressor Control
Compressor control is active only when engine is running (RPM value received from ECM via CAN).  When AC demand is active, compressor will activate.  Compressor activation should be gated by configurable variables for:
- Maximum RPM for compressor engagement (from not engaged - to protect compressor clutch)
- Compressor disengages above x% tps value (via CAN)
- minimum pressure value for engagement (to prevent AC compressor running with insufficient charge)
- maximum pressure value for engagement (high-pressure safety functionality)

Window Control
--------------

Power windows are controlled with full H-bridges.  A configurable time of button hold should activate an auto-down or auto-up feature that can be accomplished either with a configurable timer, or by setting a configurable current limit on the driver.

Boost Control
-------------

Boost control operates a solenoid using PWM to raise turbo boost level to a target map.  The solenoid vents wastegate pressure when energized, so a de-energized solenoid causes boost to be controlled by wastegate spring pressure only.  A low-side driver drives the solenoid.
