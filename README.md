[![Build Status](https://travis-ci.com/sobomax/microsippy.svg?branch=master)](https://travis-ci.com/sobomax/microsippy)

# microsippy
Extremely fast and lean, yet fully functional, SIP (RFC3261) and RTP (RFC3550)
implementation tailored for high-performance SIP processing applications as well
as small and embedded devices.

## About
The microsippy project started with an idea to implement a lightweight framework
to bring SIP UAS/UAC functionality onto high-end MCU-class devices.
The ESP8266 was used as a reference platform.

Resulting SIP implementation not only achieved its original goal, but when
run on "conventional" CPUs has also demonstrated capability to outperform
state-of-the-art SIP engines of 2020, such as OpenSIPS and Kamailio by the
factor of 5-10x.

## Design
[Some notes](doc/DesignNotes).

## References
[IoT in the Age of SIP](https://www.youtube.com/watch?v=4ia9HhMWYDY) Presentation given at the OpenSIPS Developers Summit 2018 with some rationale.
