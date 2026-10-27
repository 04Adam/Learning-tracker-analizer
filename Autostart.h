// Autostart.h
#ifndef LEARNINGTIMEANALIZER_AUTOSTART_H
#define LEARNINGTIMEANALIZER_AUTOSTART_H

// Starts the program at login (Linux, ~/.config/autostart), hidden in the background,
// so the phone always has something to sync with. No-ops on Android.
bool autostartEnabled();
void setAutostart(bool enabled);
// Enables autostart on the first run of the installed program; after that the user
// decides in the "Sync with phone…" dialog.
void enableAutostartOnFirstRun();

#endif
