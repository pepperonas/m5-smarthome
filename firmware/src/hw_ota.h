// Over-the-air updates.
//
// Deliberately an explicit mode, not a background listener. This device
// sleeps after 30 s and turns its radio off; an OTA server running underneath
// that would either be asleep when you need it or would prevent the sleeping
// that makes the battery last. Pressing 'u' on the home screen parks the
// device awake, shows its address, and waits.
//
// Nothing about updating a remote should be able to happen without somebody
// deciding it should.
#pragma once

namespace ota {

// Blocks until the update finishes or the user presses Esc. Draws its own
// screen. Returns when the device should go back to normal operation
// (a successful update reboots instead of returning).
void runMode();

}  // namespace ota
