/*
** xbox_settings_store.cpp -- see xbox_settings_store.h.
**
** File format (little-endian):
**   "RSET" magic, uint32 version (1), uint32 key count, then per key: uint16 length + bytes;
**   uint32 value count, then per value: uint16 key length + key, uint16 name length + name,
**   uint8 type, uint32 data length + data.
** Keys are stored lower-cased; value names keep the case they were first written with.
*/
#include "xbox_settings_store.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace XboxSettings {
namespace {

struct Value {
	std::string name;                 /* as first written */
	Type type;
	std::vector<unsigned char> data;
};

#ifdef NXDK
/* The Xbox kernel caches disk writes and writes them out later; closing a file does not
** force it, and a reset or power cut drops whatever is still cached. These ask the kernel
** to write a file's (or folder's) cached data to the disk now. */
static bool flush_path(const std::string &dos_path, bool directory)
{
	/* "E:\dir\" -> "\??\E:\dir\": nxMountDrive's drive letters are links in \??\ */
	std::string nt_path = std::string("\\??\\") + dos_path;
	ANSI_STRING name;
	RtlInitAnsiString(&name, nt_path.c_str());
	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
	HANDLE handle;
	IO_STATUS_BLOCK io;
	NTSTATUS status = NtOpenFile(&handle, SYNCHRONIZE | FILE_WRITE_DATA, &attributes, &io,
	                             FILE_SHARE_READ | FILE_SHARE_WRITE,
	                             FILE_SYNCHRONOUS_IO_NONALERT |
	                             (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE));
	if (!NT_SUCCESS(status)) return false;
	status = NtFlushBuffersFile(handle, &io);
	NtClose(handle);
	return NT_SUCCESS(status);
}

/* Flush the file, then the folder holding it (the save renames a temporary file, and a
** rename only changes the folder's directory entries). Returns FLUSHED_* bits. */
static int flush_to_disk(const std::string &path)
{
	if (path.size() < 3 || path[1] != ':') return 0;   /* needs a drive letter */
	int result = 0;
	if (flush_path(path, false)) result |= XboxSettings::FLUSHED_FILE;
	size_t slash = path.find_last_of("\\/");
	if (slash != std::string::npos && flush_path(path.substr(0, slash + 1), true)) {
		result |= XboxSettings::FLUSHED_FOLDER;
	}
	return result;
}
#else
static int flush_to_disk(const std::string &) { return 0; }
#endif

struct Store {
	bool loaded;
	bool dirty;
	int last_flush;                               /* Last_Disk_Flush() result */
	std::string path;
	std::set<std::string> keys;                   /* lower-cased key paths */
	std::map<std::string, Value> values;          /* lower(key) + '\n' + lower(name) */
	Store() : loaded(false), dirty(false), last_flush(0), path("D:\\renegade_settings.dat") {}
};

Store &store()
{
	static Store s;
	return s;
}

std::string normalize_key(const char *key)
{
	std::string out = key ? key : "";
	for (size_t i = 0; i < out.size(); ++i) {
		if (out[i] == '/') out[i] = '\\';
		out[i] = (char)tolower((unsigned char)out[i]);
	}
	while (!out.empty() && out[out.size() - 1] == '\\') out.erase(out.size() - 1);
	while (!out.empty() && out[0] == '\\') out.erase(0, 1);
	return out;
}

std::string lower(const char *s)
{
	std::string out = s ? s : "";
	for (size_t i = 0; i < out.size(); ++i) out[i] = (char)tolower((unsigned char)out[i]);
	return out;
}

std::string value_id(const std::string &key, const char *name)
{
	return key + '\n' + lower(name);
}

/* --- File I/O ------------------------------------------------------------------------------ */

void put_u8(std::vector<unsigned char> &b, unsigned v) { b.push_back((unsigned char)v); }
void put_u16(std::vector<unsigned char> &b, unsigned v) { put_u8(b, v & 0xFF); put_u8(b, (v >> 8) & 0xFF); }
void put_u32(std::vector<unsigned char> &b, unsigned long v)
{
	put_u16(b, (unsigned)(v & 0xFFFF)); put_u16(b, (unsigned)((v >> 16) & 0xFFFF));
}
void put_str(std::vector<unsigned char> &b, const std::string &s)
{
	put_u16(b, (unsigned)s.size());
	b.insert(b.end(), s.begin(), s.end());
}

struct Reader {
	const unsigned char *p, *end;
	bool ok;
	bool need(size_t n) { if ((size_t)(end - p) < n) ok = false; return ok; }
	unsigned u8() { if (!need(1)) return 0; return *p++; }
	unsigned u16() { unsigned lo = u8(); return lo | (u8() << 8); }
	unsigned long u32() { unsigned long lo = u16(); return lo | ((unsigned long)u16() << 16); }
	std::string str()
	{
		unsigned n = u16();
		if (!need(n)) return std::string();
		std::string s((const char *)p, n); p += n; return s;
	}
};

void ensure_loaded()
{
	if (!store().loaded) Reload();
}

} /* anonymous namespace */

/* --- Public interface ----------------------------------------------------------------------- */

void Set_File(const char *path)
{
	store().path = path ? path : "";
	store().loaded = false;      /* read the new file on next use */
}

const char *Get_File(void) { return store().path.c_str(); }

bool Reload(void)
{
	Store &s = store();
	s.keys.clear();
	s.values.clear();
	s.loaded = true;
	s.dirty = false;

	FILE *f = fopen(s.path.c_str(), "rb");
	if (!f) {
		/* A power cut between Flush()'s remove() and rename() leaves only the finished .tmp. */
		std::string tmp = s.path + ".tmp";
		if (rename(tmp.c_str(), s.path.c_str()) == 0) f = fopen(s.path.c_str(), "rb");
	}
	if (!f) return false;
	std::vector<unsigned char> buf;
	unsigned char chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
	fclose(f);

	Reader r = { buf.empty() ? NULL : &buf[0], buf.empty() ? NULL : &buf[0] + buf.size(), true };
	if (!r.need(4) || memcmp(r.p, "RSET", 4) != 0) return false;
	r.p += 4;
	if (r.u32() != 1) return false;

	std::set<std::string> keys;
	std::map<std::string, Value> values;
	unsigned long key_count = r.u32();
	for (unsigned long i = 0; i < key_count && r.ok; ++i) keys.insert(r.str());
	unsigned long value_count = r.u32();
	for (unsigned long i = 0; i < value_count && r.ok; ++i) {
		std::string key = r.str();
		Value v;
		v.name = r.str();
		v.type = (Type)r.u8();
		unsigned long len = r.u32();
		if (!r.need(len)) break;
		v.data.assign(r.p, r.p + len);
		r.p += len;
		values[value_id(key, v.name.c_str())] = v;
	}
	if (!r.ok) return false;     /* damaged file: start empty rather than half-loaded */
	s.keys.swap(keys);
	s.values.swap(values);
	return true;
}

bool Flush(void)
{
	Store &s = store();
	if (!s.dirty) return true;

	std::vector<unsigned char> b;
	b.insert(b.end(), "RSET", "RSET" + 4);
	put_u32(b, 1);
	put_u32(b, (unsigned long)s.keys.size());
	for (std::set<std::string>::const_iterator k = s.keys.begin(); k != s.keys.end(); ++k) put_str(b, *k);
	put_u32(b, (unsigned long)s.values.size());
	for (std::map<std::string, Value>::const_iterator v = s.values.begin(); v != s.values.end(); ++v) {
		put_str(b, v->first.substr(0, v->first.find('\n')));
		put_str(b, v->second.name);
		put_u8(b, (unsigned)v->second.type);
		put_u32(b, (unsigned long)v->second.data.size());
		b.insert(b.end(), v->second.data.begin(), v->second.data.end());
	}

	/* Write to a temporary file first so a power cut can't leave a half-written settings file. */
	s.last_flush = 0;
	std::string tmp = s.path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(&b[0], 1, b.size(), f) == b.size();
	ok = (fclose(f) == 0) && ok;
	if (!ok) { remove(tmp.c_str()); return false; }
	remove(s.path.c_str());
	if (rename(tmp.c_str(), s.path.c_str()) != 0) return false;
	s.last_flush = flush_to_disk(s.path);
	s.dirty = false;
	return true;
}

int Last_Disk_Flush(void)
{
	return store().last_flush;
}

bool Key_Exists(const char *key)
{
	ensure_loaded();
	return store().keys.count(normalize_key(key)) != 0;
}

void Create_Key(const char *key)
{
	ensure_loaded();
	std::string k = normalize_key(key);
	if (store().keys.insert(k).second) store().dirty = true;
}

void Delete_Tree(const char *key)
{
	ensure_loaded();
	Store &s = store();
	std::string k = normalize_key(key);
	std::string prefix = k + "\\";
	for (std::set<std::string>::iterator i = s.keys.begin(); i != s.keys.end();) {
		if (*i == k || i->compare(0, prefix.size(), prefix) == 0) { s.keys.erase(i++); s.dirty = true; }
		else ++i;
	}
	for (std::map<std::string, Value>::iterator i = s.values.begin(); i != s.values.end();) {
		std::string vk = i->first.substr(0, i->first.find('\n'));
		if (vk == k || vk.compare(0, prefix.size(), prefix) == 0) { s.values.erase(i++); s.dirty = true; }
		else ++i;
	}
}

bool Get(const char *key, const char *name, Type *type, const void **data, size_t *size)
{
	ensure_loaded();
	std::map<std::string, Value>::const_iterator i = store().values.find(value_id(normalize_key(key), name));
	if (i == store().values.end()) return false;
	if (type) *type = i->second.type;
	if (data) *data = i->second.data.empty() ? (const void *)"" : (const void *)&i->second.data[0];
	if (size) *size = i->second.data.size();
	return true;
}

void Set(const char *key, const char *name, Type type, const void *data, size_t size)
{
	ensure_loaded();
	Store &s = store();
	std::string k = normalize_key(key);
	s.keys.insert(k);
	std::string id = value_id(k, name);
	std::map<std::string, Value>::iterator i = s.values.find(id);
	const unsigned char *bytes = (const unsigned char *)data;
	if (i != s.values.end()) {
		if (i->second.type == type && i->second.data.size() == size &&
		    (size == 0 || memcmp(&i->second.data[0], bytes, size) == 0)) {
			return;    /* unchanged: don't mark dirty */
		}
		i->second.type = type;
		i->second.data.assign(bytes, bytes + size);
	} else {
		Value v;
		v.name = name ? name : "";
		v.type = type;
		v.data.assign(bytes, bytes + size);
		s.values[id] = v;
	}
	s.dirty = true;
}

void Delete_Value(const char *key, const char *name)
{
	ensure_loaded();
	if (store().values.erase(value_id(normalize_key(key), name))) store().dirty = true;
}

void List_Values(const char *key, void (*callback)(const char *name, void *context), void *context)
{
	ensure_loaded();
	std::string prefix = normalize_key(key) + '\n';
	const std::map<std::string, Value> &v = store().values;
	for (std::map<std::string, Value>::const_iterator i = v.lower_bound(prefix);
	     i != v.end() && i->first.compare(0, prefix.size(), prefix) == 0; ++i) {
		callback(i->second.name.c_str(), context);
	}
}

} /* namespace XboxSettings */
