# Pdp-11 KM11 Maintenance Module with LCD screen

This is supposed to become a KM11 maintenance module. It is a 2 slot card which should be placed in both KM11 slots of a PDP 11/05 or 11/10. It uses an ATTINY1616 (or 3216 if I cannot make the code fit) and a set of i2c I/O adapters to read the data, and writes those to an LCD screen, offset on a cable.

The main PCB must be placed in the PDP 11/05. The LCD display and the switches are connected to a ribbon cable to move outside the pdp/11.

The board uses:

* An ATTINY1616 (or ATTINY3216) as the controller
* Six PCF8574T I2C extenders to connect to both KM11 slots
* A 2.25" 76x284 pixel color LCD SPI display to display the signals

