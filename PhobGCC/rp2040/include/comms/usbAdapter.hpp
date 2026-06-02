#ifndef __USB_ADAPTER_HPP
#define __USB_ADAPTER_HPP

#include "pico/stdlib.h"
#include "gcReport.hpp"

#include <functional>

/**
 * @short Enters USB GameCube-adapter mode (WUP-028 emulation).
 *
 * Drop-in replacement for enterMode() (joybus): instead of talking the
 * GameCube joybus protocol on a data pin, the controller enumerates over USB
 * as the official Nintendo GameCube controller adapter with a single
 * controller plugged into port 1. Used for connecting directly to a Switch
 * (Smash) over USB-C.
 *
 * Reuses the exact same GCReport produced by buttonsToGCReport() (so all
 * stick processing, remapping, etc. behave identically) and emits on the
 * fixed 8.333 ms "consistency mode" cadence ported from NaxGCC.
 *
 * @param rumblePin   GPIO of the rumble motor PWM output
 * @param brakePin    GPIO of the rumble brake PWM output
 * @param rumblePower current rumble strength (0..255)
 * @param func        callback returning the GCReport to send to the console
 */
void enterUsbMode(const int rumblePin, const int brakePin, int &rumblePower,
                  std::function<GCReport()> func);

#endif
