/*
** xbox_settings_store.h -- a small persistent key/value store that stands in for the Windows
** registry on the original Xbox. RegistryClass (registry_xbox.cpp) is built on top of it.
**
** Keys are paths like "Software\\Westwood\\Renegade\\Options"; names and keys are matched
** case-insensitively, as in the Windows registry. Everything lives in memory and is written
** to one file when Flush() is called (RegistryClass does this when a key object goes away).
**
** No engine dependencies, so it can be unit-tested on any machine.
*/
#ifndef XBOX_SETTINGS_STORE_H
#define XBOX_SETTINGS_STORE_H

#include <stddef.h>

namespace XboxSettings {

enum Type {
	TYPE_DWORD   = 1,   /* 4 bytes; also used for floats (raw bits), as RegistryClass did on PC */
	TYPE_STRING  = 2,   /* narrow string, stored with its terminator */
	TYPE_WSTRING = 3,   /* 16-bit wide string, stored with its terminator */
	TYPE_BINARY  = 4
};

/* Where the settings file lives. Default "D:\\renegade_settings.dat" (next to the XBE). */
void        Set_File(const char *path);
const char *Get_File(void);

bool Key_Exists(const char *key);
void Create_Key(const char *key);
void Delete_Tree(const char *key);          /* the key, its values and every key below it */

/* Returns false if the value does not exist. Data stays valid until the value is changed. */
bool Get(const char *key, const char *name, Type *type, const void **data, size_t *size);
void Set(const char *key, const char *name, Type type, const void *data, size_t size);
void Delete_Value(const char *key, const char *name);

/* Calls back once per value under the key (not sub-keys), with the name as first stored. */
void List_Values(const char *key, void (*callback)(const char *name, void *context), void *context);

bool Flush(void);    /* write the file if anything changed; returns false if writing failed */
bool Reload(void);   /* discard memory and read the file again; returns false if no file */

} /* namespace XboxSettings */

#endif /* XBOX_SETTINGS_STORE_H */
