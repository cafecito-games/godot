/**************************************************************************/
/*  test_file_access.cpp                                                  */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_file_access)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#ifdef THREADS_ENABLED
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#endif
#include "tests/test_utils.h"

namespace TestFileAccess {

TEST_CASE("[FileAccess] CSV read") {
	Ref<FileAccess> f = FileAccess::open(TestUtils::get_data_path("testdata.csv"), FileAccess::READ);
	REQUIRE(f.is_valid());

	Vector<String> header = f->get_csv_line(); // Default delimiter: ",".
	REQUIRE(header.size() == 4);

	Vector<String> row1 = f->get_csv_line(","); // Explicit delimiter, should be the same.
	REQUIRE(row1.size() == 4);
	CHECK(row1[0] == "GOOD_MORNING");
	CHECK(row1[1] == "Good Morning");
	CHECK(row1[2] == "Guten Morgen");
	CHECK(row1[3] == "Bonjour");

	Vector<String> row2 = f->get_csv_line();
	REQUIRE(row2.size() == 4);
	CHECK(row2[0] == "GOOD_EVENING");
	CHECK(row2[1] == "Good Evening");
	CHECK(row2[2].is_empty()); // Use case: not yet translated!
	// https://github.com/godotengine/godot/issues/44269
	CHECK_MESSAGE(row2[2] != "\"", "Should not parse empty string as a single double quote.");
	CHECK(row2[3] == "\"\""); // Intentionally testing only escaped double quotes.

	Vector<String> row3 = f->get_csv_line();
	REQUIRE(row3.size() == 6);
	CHECK(row3[0] == "Without quotes");
	CHECK(row3[1] == "With, comma");
	CHECK(row3[2] == "With \"inner\" quotes");
	CHECK(row3[3] == "With \"inner\", quotes\",\" and comma");
	CHECK(row3[4] == "With \"inner\nsplit\" quotes and\nline breaks");
	CHECK(row3[5] == "With \\nnewline chars"); // Escaped, not an actual newline.

	Vector<String> row4 = f->get_csv_line("~"); // Custom delimiter, makes inline commas easier.
	REQUIRE(row4.size() == 3);
	CHECK(row4[0] == "Some other");
	CHECK(row4[1] == "delimiter");
	CHECK(row4[2] == "should still work, shouldn't it?");

	Vector<String> row5 = f->get_csv_line("\t"); // Tab separated variables.
	REQUIRE(row5.size() == 3);
	CHECK(row5[0] == "What about");
	CHECK(row5[1] == "tab separated");
	CHECK(row5[2] == "lines, good?");
}

TEST_CASE("[FileAccess] Get as UTF-8 String") {
	SUBCASE("Newline == \\n (Unix)") {
		Ref<FileAccess> f_lf = FileAccess::open(TestUtils::get_data_path("line_endings_lf.test.txt"), FileAccess::READ);
		REQUIRE(f_lf.is_valid());
		String s_lf = f_lf->get_as_utf8_string();
		CHECK(s_lf == "Hello darkness\nMy old friend\nI've come to talk\nWith you again\n");
		f_lf->seek(0);
		CHECK(f_lf->get_line() == "Hello darkness");
		CHECK(f_lf->get_line() == "My old friend");
		CHECK(f_lf->get_line() == "I've come to talk");
		CHECK(f_lf->get_line() == "With you again");
		CHECK(f_lf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r\\n (Windows)") {
		Ref<FileAccess> f_crlf = FileAccess::open(TestUtils::get_data_path("line_endings_crlf.test.txt"), FileAccess::READ);
		REQUIRE(f_crlf.is_valid());
		String s_crlf = f_crlf->get_as_utf8_string();
		CHECK(s_crlf == "Hello darkness\r\nMy old friend\r\nI've come to talk\r\nWith you again\r\n");
		f_crlf->seek(0);
		CHECK(f_crlf->get_line() == "Hello darkness");
		CHECK(f_crlf->get_line() == "My old friend");
		CHECK(f_crlf->get_line() == "I've come to talk");
		CHECK(f_crlf->get_line() == "With you again");
		CHECK(f_crlf->get_error() == Error::OK);
	}

	SUBCASE("Newline == \\r (Legacy macOS)") {
		Ref<FileAccess> f_cr = FileAccess::open(TestUtils::get_data_path("line_endings_cr.test.txt"), FileAccess::READ);
		REQUIRE(f_cr.is_valid());
		String s_cr = f_cr->get_as_utf8_string();
		CHECK(s_cr == "Hello darkness\rMy old friend\rI've come to talk\rWith you again\r");
		f_cr->seek(0);
		CHECK(f_cr->get_line() == "Hello darkness");
		CHECK(f_cr->get_line() == "My old friend");
		CHECK(f_cr->get_line() == "I've come to talk");
		CHECK(f_cr->get_line() == "With you again");
		CHECK(f_cr->get_error() == Error::OK);
	}

	SUBCASE("Newline == Mixed") {
		Ref<FileAccess> f_mix = FileAccess::open(TestUtils::get_data_path("line_endings_mixed.test.txt"), FileAccess::READ);
		REQUIRE(f_mix.is_valid());
		String s_mix = f_mix->get_as_utf8_string();
		CHECK(s_mix == "Hello darkness\nMy old friend\r\nI've come to talk\rWith you again");
		f_mix->seek(0);
		CHECK(f_mix->get_line() == "Hello darkness");
		CHECK(f_mix->get_line() == "My old friend");
		CHECK(f_mix->get_line() == "I've come to talk");
		CHECK(f_mix->get_line() == "With you again");
		CHECK(f_mix->get_error() == Error::ERR_FILE_EOF); // Not a bug; the file lacks a final newline.
	}
}

TEST_CASE("[FileAccess] Get/Store floating point values") {
	// BigEndian Hex: 0x40490E56
	// LittleEndian Hex: 0x560E4940
	float value = 3.1415f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_float(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_float(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}
}

TEST_CASE("[FileAccess] Get/Store floating point half precision values") {
	// IEEE 754 half-precision binary floating-point format:
	// sign exponent (5 bits)    fraction (10 bits)
	//  0        01101               0101010101
	// BigEndian Hex: 0x3555
	// LittleEndian Hex: 0x5535
	float value = 0.33325195f;

	SUBCASE("Little Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_little_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_little_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("Big Endian") {
		const String file_path = TestUtils::get_data_path("half_precision_floating_point_big_endian.bin");
		const String file_path_new = TestUtils::get_data_path("half_precision_floating_point_big_endian_new.bin");

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
		REQUIRE(f.is_valid());
		f->set_big_endian(true);
		CHECK_EQ(f->get_half(), value);

		Ref<FileAccess> fw = FileAccess::open(file_path_new, FileAccess::WRITE);
		REQUIRE(fw.is_valid());
		fw->set_big_endian(true);
		fw->store_half(value);
		fw->close();

		CHECK_EQ(FileAccess::get_sha256(file_path_new), FileAccess::get_sha256(file_path));

		DirAccess::remove_file_or_error(file_path_new);
	}

	SUBCASE("4096 bytes fastlz compressed") {
		const String file_path = TestUtils::get_data_path("exactly_4096_bytes_fastlz.bin");

		Ref<FileAccess> f = FileAccess::open_compressed(file_path, FileAccess::READ, FileAccess::COMPRESSION_FASTLZ);
		const Vector<uint8_t> full_data = f->get_buffer(4096 * 2);
		CHECK(full_data.size() == 4096);
		CHECK(f->eof_reached());

		// Data should be empty.
		PackedByteArray reference;
		reference.resize_initialized(4096);
		CHECK(reference == full_data);

		f->seek(0);
		const Vector<uint8_t> partial_data = f->get_buffer(4095);
		CHECK(partial_data.size() == 4095);
		CHECK(!f->eof_reached());

		reference.resize_initialized(4095);
		CHECK(reference == partial_data);
	}
}

TEST_CASE("[FileAccess] Cursor positioning") {
	Ref<FileAccess> f = FileAccess::open(TestUtils::get_data_path("line_endings_lf.test.txt"), FileAccess::READ);
	REQUIRE(f.is_valid());

	String full = f->get_as_utf8_string();
	int64_t len = full.length();

	SUBCASE("Initial position is zero") {
		f->seek(0);
		CHECK(f->get_position() == 0);
	}

	SUBCASE("seek() moves cursor to absolute position") {
		f->seek(5);
		CHECK(f->get_position() == 5);
	}

	SUBCASE("seek() moves cursor beyond file size") {
		f->seek(len + 10);
		CHECK(f->get_position() == len + 10);
	}

	SUBCASE("seek_end() moves cursor to end of file") {
		f->seek_end(0);
		CHECK(f->get_position() == len);
	}

	SUBCASE("seek_end() with positive offset") {
		f->seek_end(1);
		CHECK(f->get_position() == len + 1);
	}

	SUBCASE("seek_end() with negative offset") {
		f->seek_end(-1);
		CHECK(f->get_position() == len - 1);

		char last_char = full[full.length() - 1];
		CHECK(f->get_8() == last_char);
	}

	SUBCASE("seek_end() beyond file size") {
		f->seek(5);
		f->seek_end(-len - 10); // seeking to a position below 0; ignored.
		CHECK(f->get_position() == 5);
	}
}

namespace {

void _remove_directory_tree_if_exists(const String &p_directory_path) {
	if (p_directory_path.is_empty() || !DirAccess::dir_exists_absolute(p_directory_path)) {
		return;
	}

	Ref<DirAccess> filesystem_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem_access.is_valid() && filesystem_access->is_link(p_directory_path)) {
		filesystem_access->remove(p_directory_path);
		return;
	}

	Ref<DirAccess> directory_access = DirAccess::open(p_directory_path);
	if (directory_access.is_valid()) {
		directory_access->erase_contents_recursive();
	}
	DirAccess::remove_absolute(p_directory_path);
}

struct ScopedUserDataTestSetup {
	String previous_working_directory;
	String previous_resource_path;
	Variant previous_application_name;
	bool had_application_name = false;
	Variant previous_use_custom_user_directory;
	bool had_use_custom_user_directory = false;
	String user_data_directory;
	String sibling_directory;
	Vector<String> extra_cleanup_paths;

	ScopedUserDataTestSetup(const String &p_application_name) {
		previous_working_directory = OS::get_singleton()->get_cwd();
		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();

		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		had_application_name = project_settings->has_setting("application/config/name");
		if (had_application_name) {
			previous_application_name = project_settings->get_setting("application/config/name");
		}
		had_use_custom_user_directory = project_settings->has_setting("application/config/use_custom_user_dir");
		if (had_use_custom_user_directory) {
			previous_use_custom_user_directory = project_settings->get_setting("application/config/use_custom_user_dir");
		}

		project_settings->set_setting("application/config/use_custom_user_dir", false);
		project_settings->set_setting("application/config/name", p_application_name);

		user_data_directory = OS::get_singleton()->get_user_data_dir();
		sibling_directory = user_data_directory + "_sibling";
	}

	~ScopedUserDataTestSetup() {
		if (!previous_working_directory.is_empty()) {
			OS::get_singleton()->set_cwd(previous_working_directory);
		}
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;

		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		if (had_application_name) {
			project_settings->set_setting("application/config/name", previous_application_name);
		} else {
			project_settings->set_setting("application/config/name", Variant());
		}
		if (had_use_custom_user_directory) {
			project_settings->set_setting("application/config/use_custom_user_dir", previous_use_custom_user_directory);
		} else {
			project_settings->set_setting("application/config/use_custom_user_dir", Variant());
		}

		for (int index = extra_cleanup_paths.size() - 1; index >= 0; index--) {
			_remove_directory_tree_if_exists(extra_cleanup_paths[index]);
		}
		_remove_directory_tree_if_exists(sibling_directory);
		_remove_directory_tree_if_exists(user_data_directory);
	}
};

#if defined(THREADS_ENABLED)
struct WorkingDirectoryStressSharedState {
	static constexpr uint32_t ITERATION_COUNT = 2500;
	String relative_probe_path;
	String expected_text;
	SafeFlag navigator_ready{ false };
	SafeFlag reader_ready{ false };
	SafeFlag start_signal{ false };
	SafeNumeric<uint32_t> failed_open_count{ 0 };
	SafeNumeric<uint32_t> payload_mismatch_count{ 0 };
	SafeNumeric<uint32_t> completed_reads{ 0 };

	static void navigate_user_directories(void *p_userdata) {
		WorkingDirectoryStressSharedState *state = static_cast<WorkingDirectoryStressSharedState *>(p_userdata);
		state->navigator_ready.set();
		while (!state->start_signal.is_set()) {
			Thread::yield();
		}

		for (uint32_t iteration_index = 0; iteration_index < ITERATION_COUNT; iteration_index++) {
			Ref<DirAccess> user_directory = DirAccess::open("user://");
			if (user_directory.is_valid()) {
				user_directory->change_dir("branch_alpha");
				user_directory->change_dir("..");
				user_directory->change_dir("branch_beta");
				user_directory->change_dir("../..");
			}
			DirAccess::dir_exists_absolute("user://branch_alpha");
		}
	}

	static void read_relative_file(void *p_userdata) {
		WorkingDirectoryStressSharedState *state = static_cast<WorkingDirectoryStressSharedState *>(p_userdata);
		state->reader_ready.set();
		while (!state->start_signal.is_set()) {
			Thread::yield();
		}

		for (uint32_t iteration_index = 0; iteration_index < ITERATION_COUNT; iteration_index++) {
			Error open_error = FAILED;
			Ref<FileAccess> probe_reader = FileAccess::open(state->relative_probe_path, FileAccess::READ, &open_error);
			if (open_error != OK || probe_reader.is_null()) {
				state->failed_open_count.increment();
				continue;
			}
			if (probe_reader->get_as_utf8_string() != state->expected_text) {
				state->payload_mismatch_count.increment();
			}
			state->completed_reads.increment();
		}
	}
};
#endif // THREADS_ENABLED

} // namespace

TEST_CASE("[FileAccess] DirAccess::change_dir preserves CWD and clamps at root") {
	ScopedUserDataTestSetup setup("godot_dir_access_cwd_clamp_test");

	const String initial_working_directory = OS::get_singleton()->get_cwd();
	REQUIRE(!initial_working_directory.is_empty());

	REQUIRE(DirAccess::make_dir_recursive_absolute(setup.user_data_directory.path_join("nested")) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(setup.sibling_directory) == OK);
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	Error open_error = FAILED;
	Ref<DirAccess> user_directory = DirAccess::open("user://", &open_error);
	REQUIRE(open_error == OK);
	REQUIRE(user_directory.is_valid());
	CHECK(user_directory->get_current_dir() == "user://");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	CHECK(DirAccess::dir_exists_absolute("user://nested"));
	CHECK_FALSE(DirAccess::dir_exists_absolute("user://missing_directory"));
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	CHECK(user_directory->change_dir("nested") == OK);
	CHECK(user_directory->get_current_dir() == "user://nested");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	CHECK(user_directory->change_dir("missing_directory") == ERR_INVALID_PARAMETER);
	CHECK(user_directory->get_current_dir() == "user://nested");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	CHECK(user_directory->change_dir("..") == OK);
	CHECK(user_directory->get_current_dir() == "user://");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	CHECK(user_directory->change_dir("..") == OK);
	CHECK(user_directory->get_current_dir() == "user://");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

	const String sibling_escape = String("../") + setup.sibling_directory.get_file();
	CHECK(user_directory->change_dir(sibling_escape) == OK);
	CHECK(user_directory->get_current_dir() == "user://");
	CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);

#if defined(UNIX_ENABLED)
	const String real_resource_directory = TestUtils::get_temp_path("dir_access_real_resource_root");
	const String symlink_resource_directory = TestUtils::get_temp_path("dir_access_symlink_resource_root");
	setup.extra_cleanup_paths.push_back(real_resource_directory);
	setup.extra_cleanup_paths.push_back(symlink_resource_directory);

	_remove_directory_tree_if_exists(symlink_resource_directory);
	_remove_directory_tree_if_exists(real_resource_directory);

	REQUIRE(DirAccess::make_dir_recursive_absolute(real_resource_directory.path_join("subdir")) == OK);
	Ref<DirAccess> filesystem_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem_access.is_valid());
	if (filesystem_access->create_link(real_resource_directory, symlink_resource_directory) == OK) {
		TestProjectSettingsInternalsAccessor::resource_path() = symlink_resource_directory;

		Ref<DirAccess> resource_directory = DirAccess::open("res://", &open_error);
		REQUIRE(open_error == OK);
		REQUIRE(resource_directory.is_valid());
		CHECK(resource_directory->get_current_dir() == "res://");

		CHECK(resource_directory->change_dir(real_resource_directory.path_join("subdir")) == OK);
		CHECK(resource_directory->get_current_dir() == "res://subdir");
		CHECK(OS::get_singleton()->get_cwd() == initial_working_directory);
	}
#endif // UNIX_ENABLED
}

#if defined(THREADS_ENABLED)
TEST_CASE("[FileAccess] Concurrent DirAccess::open(user://) preserves relative FileAccess::open") {
	ScopedUserDataTestSetup setup("godot_dir_access_cwd_thread_stress_test");

	const String working_directory = TestUtils::get_temp_path("dir_access_cwd_race_working_dir");
	setup.extra_cleanup_paths.push_back(working_directory);
	_remove_directory_tree_if_exists(working_directory);
	REQUIRE(DirAccess::make_dir_recursive_absolute(working_directory) == OK);

	REQUIRE(DirAccess::make_dir_recursive_absolute(setup.user_data_directory.path_join("branch_alpha")) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(setup.user_data_directory.path_join("branch_beta")) == OK);

	const String probe_file_name = "relative_cwd_probe.txt";
	const String expected_payload = "cwd-thread-safety-payload";
	{
		Ref<FileAccess> probe_writer = FileAccess::open(working_directory.path_join(probe_file_name), FileAccess::WRITE);
		REQUIRE(probe_writer.is_valid());
		probe_writer->store_string(expected_payload);
		probe_writer->close();
	}

	REQUIRE(OS::get_singleton()->set_cwd(working_directory) == OK);

	WorkingDirectoryStressSharedState shared_state;
	shared_state.relative_probe_path = String("./") + probe_file_name;
	shared_state.expected_text = expected_payload;

	Thread navigator_thread;
	Thread reader_thread;
	navigator_thread.start(&WorkingDirectoryStressSharedState::navigate_user_directories, &shared_state);
	reader_thread.start(&WorkingDirectoryStressSharedState::read_relative_file, &shared_state);

	while (!shared_state.navigator_ready.is_set() || !shared_state.reader_ready.is_set()) {
		Thread::yield();
	}
	shared_state.start_signal.set();

	navigator_thread.wait_to_finish();
	reader_thread.wait_to_finish();

	CHECK(shared_state.failed_open_count.get() == 0);
	CHECK(shared_state.payload_mismatch_count.get() == 0);
	CHECK(shared_state.completed_reads.get() == WorkingDirectoryStressSharedState::ITERATION_COUNT);
}
#endif // THREADS_ENABLED

} // namespace TestFileAccess
