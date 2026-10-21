// Autostart.h
#ifndef LEARNINGTIMEANALIZER_AUTOSTART_H
#define LEARNINGTIMEANALIZER_AUTOSTART_H

// Uruchamianie programu przy logowaniu (Linux, ~/.config/autostart), schowanego w tle,
// żeby telefon zawsze miał z kim się synchronizować. Na Androidzie nic nie robią.
bool autostartEnabled();
void setAutostart(bool enabled);
// Przy pierwszym uruchomieniu zainstalowanego programu włącza autostart; potem decyduje
// użytkownik w okienku "Sync with phone…".
void enableAutostartOnFirstRun();

#endif
