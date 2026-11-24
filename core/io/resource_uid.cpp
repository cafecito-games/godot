/**************************************************************************/
/*  resource_uid.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "resource_uid.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#ifdef TOOLS_ENABLED
#include "core/io/resource_importer.h"
#endif
#include "core/io/resource_loader.h"
#include "core/io/stream_peer.h"
#include "core/math/random_pcg.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"

// These constants are off by 1, causing the 'z' and '9' characters never to be used.
// This cannot be fixed without breaking compatibility; see GH-83843.
static constexpr uint32_t char_count = ('z' - 'a');
static constexpr uint32_t base = char_count + ('9' - '0');

String ResourceUID::get_cache_file() {
	return ProjectSettings::get_singleton()->get_project_data_path().path_join("uid_cache.bin");
}

static constexpr uint8_t uuid_characters[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', '0', '1', '2', '3', '4', '5', '6', '7', '8' };
static constexpr uint32_t uuid_characters_element_count = std_size(uuid_characters);
static constexpr uint8_t max_uuid_number_length = 13; // Max 0x7FFFFFFFFFFFFFFF (uid://d4n4ub6itg400) size is 13 characters.

String ResourceUID::id_to_text(ID p_id) const {
	if (p_id < 0) {
		return "uid://<invalid>";
	}

	char32_t tmp[max_uuid_number_length];
	uint32_t tmp_size = 0;
	do {
		uint32_t c = p_id % uuid_characters_element_count;
		tmp[tmp_size] = uuid_characters[c];
		p_id /= uuid_characters_element_count;
		++tmp_size;
	} while (p_id);

	// tmp_size + uid:// (6) + 1 for null.
	String txt;
	txt.resize_uninitialized(tmp_size + 7);

	char32_t *p = txt.ptrw();
	p[0] = 'u';
	p[1] = 'i';
	p[2] = 'd';
	p[3] = ':';
	p[4] = '/';
	p[5] = '/';
	uint32_t size = 6;

	// The above loop give the number backward, recopy it in the string in the correct order.
	for (uint32_t i = 0; i < tmp_size; ++i) {
		p[size++] = tmp[tmp_size - i - 1];
	}

	p[size] = 0;

	return txt;
}

ResourceUID::ID ResourceUID::text_to_id(const String &p_text) const {
	if (!p_text.begins_with("uid://") || p_text == "uid://<invalid>") {
		return INVALID_ID;
	}

	uint32_t l = p_text.length();
	uint64_t uid = 0;
	for (uint32_t i = 6; i < l; i++) {
		uid *= base;
		uint32_t c = p_text[i];
		if (is_ascii_lower_case(c)) {
			uid += c - 'a';
		} else if (is_digit(c)) {
			uid += c - '0' + char_count;
		} else {
			return INVALID_ID;
		}
	}
	return ID(uid & 0x7FFFFFFFFFFFFFFF);
}

ResourceUID::ID ResourceUID::create_id() {
	// mbedTLS may not be fully initialized when the ResourceUID is created, so we
	// need to lazily instantiate the random number generator.
	if (crypto == nullptr) {
		crypto = memnew(CryptoCore::RandomGenerator);
		((CryptoCore::RandomGenerator *)crypto)->init();
	}

	while (true) {
		ID id = INVALID_ID;
		MutexLock lock(mutex);
		Error err = ((CryptoCore::RandomGenerator *)crypto)->get_random_bytes((uint8_t *)&id, sizeof(id));
		ERR_FAIL_COND_V(err != OK, INVALID_ID);
		id &= 0x7FFFFFFFFFFFFFFF;
		bool exists = unique_ids.has(id);
		if (!exists) {
			return id;
		}
	}
}

ResourceUID::ID ResourceUID::create_id_for_path(const String &p_path) {
	ID id = INVALID_ID;
	RandomPCG rng;

	const String project_name = GLOBAL_GET("application/config/name");
	// Use lowercase file name as random seed.
	// This ensures that case differences don't cause UIDs to shift on case-insensitive filesystems. The downside is that identical files with different case
	// (but otherwise identical name) will run into a hash collision, but this is a very rare scenario.
	rng.seed(project_name.hash64() * p_path.to_lower().hash64() * FileAccess::get_md5(p_path).hash64());

	while (true) {
		int64_t num1 = rng.rand();
		int64_t num2 = ((int64_t)rng.rand()) << 32;
		id = (num1 | num2) & 0x7FFFFFFFFFFFFFFF;

		MutexLock lock(mutex);
		if (!unique_ids.has(id)) {
			break;
		}
	}
	return id;
}

#ifdef TOOLS_ENABLED
const ResourceUID::Cache *ResourceUID::_get_cache_entry_with_startup_scan(ID p_id) const {
	const Cache *cache = unique_ids.getptr(p_id);
	if (p_id == INVALID_ID || !scan_for_uid_on_startup) {
		return cache;
	}

	// On startup, scan_for_uid_on_startup scans loose project files on disk when
	// a UID is missing or points to a stale path that no longer exists.
	// The callback clears scan_for_uid_on_startup before scanning so it runs at most once.
	bool needs_scan = !cache;
	if (!needs_scan) {
		const ProjectSettings *project_settings = ProjectSettings::get_singleton();
		if (project_settings && !project_settings->is_using_datapack() && !project_settings->get_resource_path().is_empty()) {
			const String cached_path = String::utf8(cache->cs.ptr());
			if (cached_path.begins_with("res://") && !FileAccess::exists(cached_path)) {
				needs_scan = true;
			}
		}
	}
	if (needs_scan && scan_for_uid_on_startup) {
		scan_for_uid_on_startup();
		cache = unique_ids.getptr(p_id);
	}
	return cache;
}
#endif // TOOLS_ENABLED

bool ResourceUID::has_id(ID p_id) const {
	MutexLock l(mutex);
#ifdef TOOLS_ENABLED
	return _get_cache_entry_with_startup_scan(p_id) != nullptr;
#else
	return unique_ids.has(p_id);
#endif
}

void ResourceUID::add_id(ID p_id, const String &p_path) {
	MutexLock l(mutex);
	ERR_FAIL_COND(unique_ids.has(p_id));
	Cache c;
	c.cs = p_path.utf8();
	unique_ids[p_id] = c;
	if (use_reverse_cache) {
		reverse_cache[c.cs] = p_id;
	}
	changed = true;
	// The cache was never loaded (probably does not exist), so assume that first ID initializes it.
	cache_initialized = true;
}

void ResourceUID::set_id(ID p_id, const String &p_path) {
	MutexLock l(mutex);
	ERR_FAIL_COND(!unique_ids.has(p_id));
	CharString cs = p_path.utf8();
	const char *update_ptr = cs.ptr();
	const char *cached_ptr = unique_ids[p_id].cs.ptr();
	if (update_ptr == nullptr && cached_ptr == nullptr) {
		return; // Both are empty strings.
	}
	if ((update_ptr == nullptr) != (cached_ptr == nullptr) || strcmp(update_ptr, cached_ptr) != 0) {
		if (use_reverse_cache) {
			const ID *existing_reverse = reverse_cache.getptr(unique_ids[p_id].cs);
			if (existing_reverse && *existing_reverse == p_id) {
				reverse_cache.erase(unique_ids[p_id].cs);
			}
		}
		unique_ids[p_id].cs = cs;
		unique_ids[p_id].saved_to_cache = false; //changed
		if (use_reverse_cache) {
			reverse_cache[cs] = p_id;
		}
		changed = true;
	}
}

String ResourceUID::get_id_path(ID p_id) const {
	ERR_FAIL_COND_V_MSG(p_id == INVALID_ID, String(), "Invalid UID.");
	MutexLock l(mutex);
#ifdef TOOLS_ENABLED
	const ResourceUID::Cache *cache = _get_cache_entry_with_startup_scan(p_id);
#else
	const ResourceUID::Cache *cache = unique_ids.getptr(p_id);
#endif

	if (unlikely(!cache)) {
		if (cache_initialized) {
			ERR_PRINT(vformat("Unrecognized UID: \"%s\".", id_to_text(p_id)));
		}
		return String();
	}
	const CharString &cs = cache->cs;
	return String::utf8(cs.ptr());
}

ResourceUID::ID ResourceUID::get_path_id(const String &p_path) const {
	MutexLock lock(mutex);
	const CharString utf8_path = p_path.utf8();
	const ID *id = reverse_cache.getptr(utf8_path);
#ifdef TOOLS_ENABLED
	if (!id && use_reverse_cache && scan_for_uid_on_startup) {
		const ProjectSettings *project_settings = ProjectSettings::get_singleton();
		if (project_settings && !project_settings->is_using_datapack() && !project_settings->get_resource_path().is_empty()) {
			scan_for_uid_on_startup();
			id = reverse_cache.getptr(utf8_path);
		}
	}
#endif
	if (id) {
		return *id;
	}
	return INVALID_ID;
}

void ResourceUID::remove_id(ID p_id) {
	MutexLock l(mutex);
	ERR_FAIL_COND(!unique_ids.has(p_id));
	if (use_reverse_cache) {
		reverse_cache.erase(unique_ids[p_id].cs);
	}
	unique_ids.erase(p_id);
	changed = true;
}

String ResourceUID::uid_to_path(const String &p_uid) {
	return singleton->get_id_path(singleton->text_to_id(p_uid));
}

String ResourceUID::path_to_uid(const String &p_path) {
	const ID id = ResourceLoader::get_resource_uid(p_path);
	if (id == INVALID_ID) {
		return p_path;
	} else {
		return singleton->id_to_text(id);
	}
}

String ResourceUID::ensure_path(const String &p_uid_or_path) {
	if (p_uid_or_path.begins_with("uid://")) {
		return uid_to_path(p_uid_or_path);
	}
	return p_uid_or_path;
}

Vector<uint8_t> ResourceUID::encode_binary_cache(const Vector<Pair<ID, String>> &p_entries) {
	Ref<StreamPeerBuffer> buffer;
	buffer.instantiate();
	buffer->put_u32(p_entries.size());

	for (const Pair<ID, String> &entry : p_entries) {
		buffer->put_u64(uint64_t(entry.first));
		CharString cs = entry.second.utf8();
		buffer->put_u32(cs.length());
		buffer->put_data((const uint8_t *)cs.ptr(), cs.length());
	}

	return buffer->get_data_array();
}

Error ResourceUID::save_to_cache() {
	MutexLock lock(mutex);

	const String cache_file = get_cache_file();
	const String cache_directory = cache_file.get_base_dir();
	Ref<DirAccess> directory_access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (directory_access.is_null()) {
		return ERR_CANT_OPEN;
	}
	if (!directory_access->dir_exists(cache_directory)) {
		const Error mkdir_error = directory_access->make_dir_recursive(cache_directory);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
	}

	Vector<Pair<ID, String>> entries;
	entries.reserve(unique_ids.size());
	for (const KeyValue<ID, Cache> &entry : unique_ids) {
		entries.push_back(Pair<ID, String>(entry.key, String::utf8(entry.value.cs.ptr(), entry.value.cs.length())));
	}

	const Vector<uint8_t> data = encode_binary_cache(entries);

	static SafeNumeric<uint64_t> temporary_file_counter{ 0 };
	const uint64_t unique_counter = temporary_file_counter.increment();
	const int64_t process_id = OS::get_singleton() ? OS::get_singleton()->get_process_id() : 0;
	const String temporary_path = cache_file + "." + String::num_int64(process_id) + "." + String::num_uint64(unique_counter) + ".tmp";

	Error open_error = OK;
	Ref<FileAccess> file_access = FileAccess::open(temporary_path, FileAccess::WRITE, &open_error);
	if (file_access.is_null() || open_error != OK) {
		return open_error != OK ? open_error : ERR_CANT_OPEN;
	}

	const bool stored = file_access->store_buffer(data.ptr(), data.size());
	const Error write_error = file_access->get_error();
	file_access.unref();

	if (!stored || write_error != OK) {
		directory_access->remove(temporary_path);
		return write_error != OK ? write_error : ERR_FILE_CANT_WRITE;
	}

	const Error rename_error = directory_access->rename(temporary_path, cache_file);
	if (rename_error != OK) {
		directory_access->remove(temporary_path);
		return rename_error;
	}

	for (KeyValue<ID, Cache> &entry : unique_ids) {
		entry.value.saved_to_cache = true;
	}
	cache_entries = entries.size();
	changed = false;
	return OK;
}

Error ResourceUID::load_from_cache(bool p_reset) {
	Ref<FileAccess> f = FileAccess::open(get_cache_file(), FileAccess::READ);
	if (f.is_null()) {
		return ERR_CANT_OPEN;
	}

	MutexLock l(mutex);
	if (p_reset) {
		if (use_reverse_cache) {
			reverse_cache.clear();
		}
		unique_ids.clear();
	}

	uint32_t entry_count = f->get_32();
	for (uint32_t i = 0; i < entry_count; i++) {
		int64_t id = f->get_64();
		int32_t len = f->get_32();
		Cache c;
		c.cs.resize_uninitialized(len + 1);
		ERR_FAIL_COND_V(c.cs.size() != len + 1, ERR_FILE_CORRUPT); // Out of memory.
		c.cs[len] = 0;
		int32_t rl = f->get_buffer((uint8_t *)c.cs.ptrw(), len);
		ERR_FAIL_COND_V(rl != len, ERR_FILE_CORRUPT);

		c.saved_to_cache = true;
		unique_ids[id] = c;
		if (use_reverse_cache) {
			reverse_cache[c.cs] = id;
		}
	}

	cache_entries = entry_count;
	changed = false;
	cache_initialized = true;
	return OK;
}

Error ResourceUID::update_cache() {
	MutexLock lock(mutex);
	if (!changed) {
		return OK;
	}
	return save_to_cache();
}

#ifdef TOOLS_ENABLED
namespace {

static bool should_skip_uid_scan_directory(const String &p_directory_path, const String &p_project_data_path) {
	if (p_directory_path == p_project_data_path || p_directory_path.begins_with(p_project_data_path + "/")) {
		return true;
	}
	if (FileAccess::exists(p_directory_path.path_join("project.godot"))) {
		return true;
	}
	if (FileAccess::exists(p_directory_path.path_join(".gdignore"))) {
		return true;
	}
	return false;
}

static void scan_directory_for_uids(Ref<DirAccess> &p_directory_access, const String &p_project_data_path, const HashSet<String> &p_import_extensions) {
	List<String> subdirectories;
	List<String> files;

	const String current_directory = p_directory_access->get_current_dir();

	p_directory_access->list_dir_begin();
	while (true) {
		const String entry_name = p_directory_access->get_next();
		if (entry_name.is_empty()) {
			break;
		}
		if (p_directory_access->current_is_hidden()) {
			continue;
		}

		if (p_directory_access->current_is_dir()) {
			if (entry_name.begins_with(".")) {
				continue;
			}
			if (should_skip_uid_scan_directory(current_directory.path_join(entry_name), p_project_data_path)) {
				continue;
			}
			subdirectories.push_back(entry_name);
		} else {
			files.push_back(entry_name);
		}
	}
	p_directory_access->list_dir_end();

	subdirectories.sort_custom<FileNoCaseComparator>();
	files.sort_custom<FileNoCaseComparator>();

	for (const String &subdirectory : subdirectories) {
		if (p_directory_access->change_dir(subdirectory) == OK) {
			const String next_directory = p_directory_access->get_current_dir();
			const bool is_descendant = next_directory != current_directory && (current_directory == "res://" ? next_directory.begins_with("res://") : next_directory.begins_with(current_directory + "/"));
			if (is_descendant) {
				scan_directory_for_uids(p_directory_access, p_project_data_path, p_import_extensions);
			}
			p_directory_access->change_dir(current_directory);
		}
	}

	ResourceUID *resource_uid = ResourceUID::get_singleton();
	ResourceFormatImporter *importer = ResourceFormatImporter::get_singleton();
	for (const String &file_name : files) {
		const String extension = file_name.get_extension().to_lower();
		if (extension == "uid" || extension == "import") {
			continue;
		}

		const String full_path = current_directory.path_join(file_name);
		ResourceUID::ID uid = ResourceUID::INVALID_ID;
		if (p_import_extensions.has(extension)) {
			if (importer && FileAccess::exists(full_path + ".import")) {
				uid = importer->get_resource_uid(full_path);
			}
		} else {
			uid = ResourceLoader::get_resource_uid_from_disk(full_path);
		}

		if (uid != ResourceUID::INVALID_ID) {
			if (!resource_uid->has_id(uid)) {
				resource_uid->add_id(uid, full_path);
			} else {
				const String existing_path = resource_uid->get_id_path(uid);
				if (existing_path != full_path && !FileAccess::exists(existing_path)) {
					resource_uid->set_id(uid, full_path);
				}
			}
		}
	}
}

} // namespace

void ResourceUID::scan_project_uids_from_disk() {
	ResourceUID *resource_uid = get_singleton();
	if (!resource_uid) {
		scan_for_uid_on_startup = nullptr;
		return;
	}

	MutexLock lock(resource_uid->mutex);
	// Clear the startup hook under the mutex before scanning so neither another thread
	// nor recursive UID lookups during scanning can re-enter.
	scan_for_uid_on_startup = nullptr;

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings || project_settings->is_using_datapack() || project_settings->get_resource_path().is_empty()) {
		return;
	}

	Ref<DirAccess> directory_access = DirAccess::open("res://");
	if (directory_access.is_null()) {
		return;
	}

	HashSet<String> import_extensions;
	if (ResourceFormatImporter::get_singleton()) {
		List<String> extension_list;
		ResourceFormatImporter::get_singleton()->get_recognized_extensions(&extension_list);
		for (const String &extension : extension_list) {
			import_extensions.insert(extension);
		}
	}

	scan_directory_for_uids(directory_access, project_settings->get_project_data_path(), import_extensions);

	if (resource_uid->changed) {
		resource_uid->save_to_cache();
	}
}
#endif // TOOLS_ENABLED

String ResourceUID::get_path_from_cache(Ref<FileAccess> &p_cache_file, const String &p_uid_string) {
	const int64_t uid_from_string = singleton->text_to_id(p_uid_string);
	if (uid_from_string != INVALID_ID) {
		const uint32_t entry_count = p_cache_file->get_32();
		for (uint32_t i = 0; i < entry_count; i++) {
			int64_t id = p_cache_file->get_64();
			int32_t len = p_cache_file->get_32();

			if (id == uid_from_string) {
				CharString cs;
				cs.resize_uninitialized(len + 1);
				ERR_FAIL_COND_V(cs.size() != len + 1, String());
				cs[len] = 0;
				int32_t rl = p_cache_file->get_buffer((uint8_t *)cs.ptrw(), len);
				ERR_FAIL_COND_V(rl != len, String());
				return String::utf8(cs.get_data());
			} else {
				p_cache_file->seek(p_cache_file->get_position() + len);
				ERR_FAIL_COND_V(p_cache_file->eof_reached(), String());
			}
		}
	}
	return String();
}

void ResourceUID::clear() {
	MutexLock lock(mutex);
	cache_entries = 0;
	if (use_reverse_cache) {
		reverse_cache.clear();
	}
	unique_ids.clear();
	changed = false;
}

void ResourceUID::_bind_methods() {
	ClassDB::bind_method(D_METHOD("id_to_text", "id"), &ResourceUID::id_to_text);
	ClassDB::bind_method(D_METHOD("text_to_id", "text_id"), &ResourceUID::text_to_id);

	ClassDB::bind_method(D_METHOD("create_id"), &ResourceUID::create_id);
	ClassDB::bind_method(D_METHOD("create_id_for_path", "path"), &ResourceUID::create_id_for_path);

	ClassDB::bind_method(D_METHOD("has_id", "id"), &ResourceUID::has_id);
	ClassDB::bind_method(D_METHOD("add_id", "id", "path"), &ResourceUID::add_id);
	ClassDB::bind_method(D_METHOD("set_id", "id", "path"), &ResourceUID::set_id);
	ClassDB::bind_method(D_METHOD("get_id_path", "id"), &ResourceUID::get_id_path);
	ClassDB::bind_method(D_METHOD("remove_id", "id"), &ResourceUID::remove_id);

	ClassDB::bind_static_method("ResourceUID", D_METHOD("uid_to_path", "uid"), &ResourceUID::uid_to_path);
	ClassDB::bind_static_method("ResourceUID", D_METHOD("path_to_uid", "path"), &ResourceUID::path_to_uid);
	ClassDB::bind_static_method("ResourceUID", D_METHOD("ensure_path", "path_or_uid"), &ResourceUID::ensure_path);

	BIND_CONSTANT(INVALID_ID)
}
ResourceUID *ResourceUID::singleton = nullptr;
ResourceUID::ResourceUID() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
}
ResourceUID::~ResourceUID() {
	if (crypto != nullptr) {
		memdelete((CryptoCore::RandomGenerator *)crypto);
	}
}
