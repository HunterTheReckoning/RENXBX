/*
** registry_xbox.cpp -- RegistryClass for the original Xbox (nxdk builds only).
**
** The PC version (registry.cpp) uses the Windows registry under HKEY_LOCAL_MACHINE. The Xbox
** has no registry, so the same interface is provided on top of XboxSettings, which keeps the
** values in memory and saves them to one file. Behaviour copied from registry.cpp:
**   - Floats are stored as their raw 32-bit pattern in a DWORD value.
**   - Bools are stored as DWORD 0/1 and read back as "non-zero".
**   - Narrow and wide strings are both REG_SZ on Windows, which converts between them when
**     read the other way; this does the same (Latin-1, as the rest of the port).
**   - Getters return the default when the value is missing, has the wrong type, or (for the
**     char buffer version) doesn't fit, like RegQueryValueEx failing.
**   - Set_Read_Only(true) makes every write a no-op and stops keys being created.
** Changes are written to the settings file when a RegistryClass object is destroyed.
*/
#include "always.h"
#include "registry.h"

#ifdef NXDK

#include "wwstring.h"
#include "widestring.h"
#include "xbox_settings_store.h"
#include "wwdebug.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

bool RegistryClass::IsLocked = false;

namespace {

/* Key handles: RegistryClass stores an int, so keep a table of key paths and store the index. */
std::vector<std::string> &key_table()
{
	static std::vector<std::string> table;
	return table;
}

int key_handle(const char *path)
{
	std::vector<std::string> &t = key_table();
	std::string p = path ? path : "";
	for (size_t i = 0; i < t.size(); ++i) {
		if (t[i] == p) return (int)i;
	}
	t.push_back(p);
	return (int)t.size() - 1;
}

const char *key_path(int handle)
{
	std::vector<std::string> &t = key_table();
	return (handle >= 0 && (size_t)handle < t.size()) ? t[handle].c_str() : "";
}

/* Wide value names are stored as narrow (Latin-1) names. */
std::string narrow_name(const WCHAR *name)
{
	std::string out;
	for (const WCHAR *p = name; p && *p; ++p) out += (char)((*p <= 0xFF) ? *p : '?');
	return out;
}

bool get_dword(int key, const char *name, uint32_t *out)
{
	XboxSettings::Type type;
	const void *data;
	size_t size;
	if (!XboxSettings::Get(key_path(key), name, &type, &data, &size)) return false;
	if (type != XboxSettings::TYPE_DWORD || size != 4) return false;
	memcpy(out, data, 4);
	return true;
}

void collect_name(const char *name, void *context)
{
	((DynamicVectorClass<StringClass> *)context)->Add(StringClass(name));
}

} /* anonymous namespace */

bool RegistryClass::Exists(const char *sub_key)
{
	return XboxSettings::Key_Exists(sub_key);
}

RegistryClass::RegistryClass(const char *sub_key, bool create) :
	Key(-1),
	IsValid(false)
{
	if (create && !IsLocked) {
		XboxSettings::Create_Key(sub_key);
	}
	if (XboxSettings::Key_Exists(sub_key)) {
		Key = key_handle(sub_key);
		IsValid = true;
	}
}

RegistryClass::~RegistryClass(void)
{
	XboxSettings::Flush();
}

int RegistryClass::Get_Int(const char *name, int def_value)
{
	assert(IsValid);
	uint32_t v;
	return get_dword(Key, name, &v) ? (int)v : def_value;
}

void RegistryClass::Set_Int(const char *name, int value)
{
	assert(IsValid);
	if (IsLocked) return;
	XboxSettings::Set(key_path(Key), name, XboxSettings::TYPE_DWORD, &value, 4);
}

bool RegistryClass::Get_Bool(const char *name, bool def_value)
{
	return (Get_Int(name, def_value) != 0);
}

void RegistryClass::Set_Bool(const char *name, bool value)
{
	Set_Int(name, value ? 1 : 0);
}

float RegistryClass::Get_Float(const char *name, float def_value)
{
	assert(IsValid);
	uint32_t bits;
	if (!get_dword(Key, name, &bits)) return def_value;
	float f;
	memcpy(&f, &bits, 4);
	return f;
}

void RegistryClass::Set_Float(const char *name, float value)
{
	assert(IsValid);
	if (IsLocked) return;
	XboxSettings::Set(key_path(Key), name, XboxSettings::TYPE_DWORD, &value, 4);
}

/* Fetch any string value as narrow text (wide values converted). */
static bool get_narrow_string(int key, const char *name, std::string *out)
{
	XboxSettings::Type type;
	const void *data;
	size_t size;
	if (!XboxSettings::Get(key_path(key), name, &type, &data, &size)) return false;
	if (type == XboxSettings::TYPE_STRING) {
		const char *s = (const char *)data;
		size_t n = 0;
		while (n < size && s[n]) ++n;
		out->assign(s, n);
		return true;
	}
	if (type == XboxSettings::TYPE_WSTRING) {
		const unsigned short *w = (const unsigned short *)data;
		out->clear();
		for (size_t i = 0; i < size / 2 && w[i]; ++i) *out += (char)((w[i] <= 0xFF) ? w[i] : '?');
		return true;
	}
	return false;
}

/* Fetch any string value as 16-bit wide text (narrow values widened). */
static bool get_wide_string(int key, const char *name, std::vector<WCHAR> *out)
{
	XboxSettings::Type type;
	const void *data;
	size_t size;
	if (!XboxSettings::Get(key_path(key), name, &type, &data, &size)) return false;
	out->clear();
	if (type == XboxSettings::TYPE_WSTRING) {
		const unsigned short *w = (const unsigned short *)data;
		for (size_t i = 0; i < size / 2 && w[i]; ++i) out->push_back((WCHAR)w[i]);
	} else if (type == XboxSettings::TYPE_STRING) {
		const char *s = (const char *)data;
		for (size_t i = 0; i < size && s[i]; ++i) out->push_back((WCHAR)(unsigned char)s[i]);
	} else {
		return false;
	}
	out->push_back(0);
	return true;
}

char *RegistryClass::Get_String(const char *name, char *value, int value_size, const char *default_string)
{
	assert(IsValid);
	std::string s;
	if (get_narrow_string(Key, name, &s) && (int)s.size() + 1 <= value_size) {
		memcpy(value, s.c_str(), s.size() + 1);
	} else if (default_string == NULL) {
		*value = 0;
	} else {
		assert(strlen(default_string) < (unsigned int)value_size);
		strcpy(value, default_string);
	}
	return value;
}

void RegistryClass::Get_String(const char *name, StringClass &string, const char *default_string)
{
	assert(IsValid);
	string = (default_string == NULL) ? "" : default_string;
	std::string s;
	if (get_narrow_string(Key, name, &s)) {
		string = s.c_str();
	}
}

void RegistryClass::Set_String(const char *name, const char *value)
{
	assert(IsValid);
	if (IsLocked) return;
	XboxSettings::Set(key_path(Key), name, XboxSettings::TYPE_STRING, value, strlen(value) + 1);
}

void RegistryClass::Get_String(const WCHAR *name, WideStringClass &string, const WCHAR *default_string)
{
	assert(IsValid);
	string = (default_string == NULL) ? L"" : default_string;
	std::vector<WCHAR> w;
	if (get_wide_string(Key, narrow_name(name).c_str(), &w)) {
		string = &w[0];
	}
}

void RegistryClass::Set_String(const WCHAR *name, const WCHAR *value)
{
	assert(IsValid);
	if (IsLocked) return;
	std::vector<unsigned short> w;
	for (const WCHAR *p = value; *p; ++p) w.push_back((unsigned short)*p);
	w.push_back(0);
	XboxSettings::Set(key_path(Key), narrow_name(name).c_str(), XboxSettings::TYPE_WSTRING,
	                  &w[0], w.size() * 2);
}

void RegistryClass::Get_Bin(const char *name, void *buffer, int buffer_size)
{
	assert(IsValid);
	XboxSettings::Type type;
	const void *data;
	size_t size;
	/* Like RegQueryValueEx: copy only if the whole value fits, otherwise leave the buffer alone. */
	if (XboxSettings::Get(key_path(Key), name, &type, &data, &size) && (int)size <= buffer_size) {
		memcpy(buffer, data, size);
	}
}

int RegistryClass::Get_Bin_Size(const char *name)
{
	assert(IsValid);
	size_t size = 0;
	if (!XboxSettings::Get(key_path(Key), name, NULL, NULL, &size)) return 0;
	return (int)size;
}

void RegistryClass::Set_Bin(const char *name, const void *buffer, int buffer_size)
{
	assert(IsValid);
	if (IsLocked) return;
	XboxSettings::Set(key_path(Key), name, XboxSettings::TYPE_BINARY, buffer, (size_t)buffer_size);
}

void RegistryClass::Get_Value_List(DynamicVectorClass<StringClass> &list)
{
	XboxSettings::List_Values(key_path(Key), collect_name, &list);
}

void RegistryClass::Delete_Value(const char *name)
{
	if (IsLocked) return;
	XboxSettings::Delete_Value(key_path(Key), name);
}

void RegistryClass::Deleta_All_Values(void)
{
	if (IsLocked) return;
	DynamicVectorClass<StringClass> value_list;
	Get_Value_List(value_list);
	for (int index = 0; index < value_list.Count(); index++) {
		Delete_Value(value_list[index]);
	}
}

void RegistryClass::Delete_Registry_Tree(char *path)
{
	if (IsLocked) return;
	XboxSettings::Delete_Tree(path);
	XboxSettings::Flush();
}

/*
** Exporting and importing whole registry trees to .ini files is only used by the multiplayer
** dedicated server (slavemaster.cpp). Not supported on the Xbox build.
*/
void RegistryClass::Load_Registry(const char *, char *, char *)
{
	WWDEBUG_SAY(("RegistryClass::Load_Registry is not supported on Xbox\n"));
}

void RegistryClass::Save_Registry(const char *, char *)
{
	WWDEBUG_SAY(("RegistryClass::Save_Registry is not supported on Xbox\n"));
}

#endif /* NXDK */
