/**************************************************************************/
/*  test_pck_packer.cpp                                                   */
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

TEST_FORCE_LINK(test_pck_packer)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#ifdef MINIZIP_ENABLED
#include "core/io/file_access_zip.h"
#include "core/io/zip_io.h"
#endif
#include "core/io/pck_packer.h"
#include "core/os/os.h"
#include "tests/test_utils.h"

namespace TestPCKPacker {

TEST_CASE("[PCKPacker] Pack an empty PCK file") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	CHECK_MESSAGE(
			pck_packer.pck_start(output_pck_path) == OK,
			"Starting a PCK file should return an OK error code.");

	CHECK_MESSAGE(
			pck_packer.flush() == OK,
			"Flushing the PCK should return an OK error code.");

	Error err;
	Ref<FileAccess> f = FileAccess::open(output_pck_path, FileAccess::READ, &err);
	CHECK_MESSAGE(
			err == OK,
			"The generated empty PCK file should be opened successfully.");
	CHECK_MESSAGE(
			f->get_length() >= 100,
			"The generated empty PCK file shouldn't be too small (it should have the PCK header).");
	CHECK_MESSAGE(
			f->get_length() <= 500,
			"The generated empty PCK file shouldn't be too large.");
}

TEST_CASE("[PCKPacker] Pack empty with zero alignment invalid") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	ERR_PRINT_OFF;
	CHECK_MESSAGE(pck_packer.pck_start(output_pck_path, 0) != OK, "PCK with zero alignment should fail.");
	ERR_PRINT_ON;
}

TEST_CASE("[PCKPacker] Pack empty with invalid key") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	ERR_PRINT_OFF;
	CHECK_MESSAGE(pck_packer.pck_start(output_pck_path, 32, "") != OK, "PCK with invalid key should fail.");
	ERR_PRINT_ON;
}

TEST_CASE("[PCKPacker] Pack a PCK file with some files and directories") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_with_files.pck");
	CHECK_MESSAGE(
			pck_packer.pck_start(output_pck_path) == OK,
			"Starting a PCK file should return an OK error code.");

	const String base_dir = OS::get_singleton()->get_executable_path().get_base_dir();

	CHECK_MESSAGE(
			pck_packer.add_file("version.py", base_dir.path_join("../version.py"), "version.py") == OK,
			"Adding a file to the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.png", base_dir.path_join("../misc/logo/icon.png")) == OK,
			"Adding a file to a new subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.svg", base_dir.path_join("../misc/logo/icon.svg")) == OK,
			"Adding a file to an existing subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.png", base_dir.path_join("../misc/logo/logo.png")) == OK,
			"Overriding a non-flushed file to an existing subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file_from_buffer("buffer/new.txt", String("Hello world!").to_utf8_buffer()) == OK,
			"Adding a file from a buffer to the PCK in a new subdirectory should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.flush() == OK,
			"Flushing the PCK should return an OK error code.");

	Error err;
	Ref<FileAccess> f = FileAccess::open(output_pck_path, FileAccess::READ, &err);
	CHECK_MESSAGE(
			err == OK,
			"The generated non-empty PCK file should be opened successfully.");
	CHECK_MESSAGE(
			f->get_length() >= 18000,
			"The generated non-empty PCK file should be large enough to actually hold the contents specified above.");
	CHECK_MESSAGE(
			f->get_length() <= 27000,
			"The generated non-empty PCK file shouldn't be too large.");
}

namespace {

void _remove_test_directory_if_exists(const String &p_directory_path) {
	if (p_directory_path.is_empty() || !DirAccess::dir_exists_absolute(p_directory_path)) {
		return;
	}
	Ref<DirAccess> directory_access = DirAccess::open(p_directory_path);
	if (directory_access.is_valid()) {
		directory_access->erase_contents_recursive();
	}
	DirAccess::remove_absolute(p_directory_path);
}

struct ScopedPackAndWorkingDirectoryCleanup {
	String original_working_directory;
	bool was_packed_data_disabled = false;
	Vector<String> registered_pack_files;
	Vector<String> temporary_directories;

	ScopedPackAndWorkingDirectoryCleanup() {
		original_working_directory = OS::get_singleton()->get_cwd();
		PackedData *packed_data = PackedData::get_singleton();
		if (packed_data != nullptr) {
			was_packed_data_disabled = packed_data->is_disabled();
			packed_data->set_disabled(false);
		}
	}

	~ScopedPackAndWorkingDirectoryCleanup() {
		if (!original_working_directory.is_empty()) {
			OS::get_singleton()->set_cwd(original_working_directory);
		}
		PackedData *packed_data = PackedData::get_singleton();
		if (packed_data != nullptr) {
			for (const String &registered_path : registered_pack_files) {
				packed_data->remove_path(registered_path);
			}
			packed_data->set_disabled(was_packed_data_disabled);
		}
		for (int index = temporary_directories.size() - 1; index >= 0; index--) {
			_remove_test_directory_if_exists(temporary_directories[index]);
		}
	}
};

#ifdef MINIZIP_ENABLED
Error _write_single_entry_zip_archive(const String &p_zip_path, const String &p_entry_path, const CharString &p_payload) {
	Ref<FileAccess> archive_file;
	zlib_filefunc_def io_functions = zipio_create_io(&archive_file);
	zipFile zip_handle = zipOpen2(p_zip_path.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io_functions);
	if (zip_handle == nullptr) {
		return FAILED;
	}

	zip_fileinfo file_information = {};
	if (zipOpenNewFileInZip(zip_handle, p_entry_path.utf8().get_data(), &file_information, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK) {
		zipClose(zip_handle, nullptr);
		return FAILED;
	}

	if (zipWriteInFileInZip(zip_handle, p_payload.get_data(), p_payload.length()) != ZIP_OK) {
		zipCloseFileInZip(zip_handle);
		zipClose(zip_handle, nullptr);
		return FAILED;
	}

	if (zipCloseFileInZip(zip_handle) != ZIP_OK) {
		zipClose(zip_handle, nullptr);
		return FAILED;
	}

	return zipClose(zip_handle, nullptr) == ZIP_OK ? OK : FAILED;
}
#endif // MINIZIP_ENABLED

} // namespace

TEST_CASE("[PCKPacker] Relative PCK pack remains readable after CWD changes") {
	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);

	ScopedPackAndWorkingDirectoryCleanup cleanup;
	const String directory_alpha = TestUtils::get_temp_path("pck_relative_cwd_alpha");
	const String directory_beta = TestUtils::get_temp_path("pck_relative_cwd_beta");
	cleanup.temporary_directories.push_back(directory_alpha);
	cleanup.temporary_directories.push_back(directory_beta);
	_remove_test_directory_if_exists(directory_alpha);
	_remove_test_directory_if_exists(directory_beta);
	REQUIRE(DirAccess::make_dir_recursive_absolute(directory_alpha) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(directory_beta) == OK);

	const String nested_pck_disk_path = directory_beta.path_join("nested_build_source.pck");
	const String nested_inner_resource_path = "res://relative_pck_cwd_test/nested_payload.txt";
	const String nested_expected_payload = "Nested pack payload survives CWD transition";
	{
		PCKPacker nested_packer;
		REQUIRE(nested_packer.pck_start(nested_pck_disk_path) == OK);
		REQUIRE(nested_packer.add_file_from_buffer(nested_inner_resource_path, nested_expected_payload.to_utf8_buffer()) == OK);
		REQUIRE(nested_packer.flush() == OK);
	}

	const String outer_pck_disk_path = directory_alpha.path_join("relative_test.pck");
	const String outer_resource_path = "res://relative_pck_cwd_test/outer_payload.txt";
	const String embedded_nested_pack_name = "embedded_nested_pack.pck";
	const String outer_expected_payload = "Outer relative PCK payload survives CWD transition";
	{
		PCKPacker outer_packer;
		REQUIRE(outer_packer.pck_start(outer_pck_disk_path) == OK);
		REQUIRE(outer_packer.add_file_from_buffer(outer_resource_path, outer_expected_payload.to_utf8_buffer()) == OK);
		REQUIRE(outer_packer.add_file(embedded_nested_pack_name, nested_pck_disk_path) == OK);
		REQUIRE(outer_packer.flush() == OK);
	}
	DirAccess::remove_file_or_error(nested_pck_disk_path);

	cleanup.registered_pack_files.push_back(outer_resource_path);
	cleanup.registered_pack_files.push_back("res://" + embedded_nested_pack_name);
	cleanup.registered_pack_files.push_back(nested_inner_resource_path);

	REQUIRE(OS::get_singleton()->set_cwd(directory_alpha) == OK);
	REQUIRE(packed_data->add_pack("./relative_test.pck", true, 0) == OK);

	REQUIRE(OS::get_singleton()->set_cwd(directory_beta) == OK);

	Error open_error = FAILED;
	Ref<FileAccess> outer_reader = FileAccess::open(outer_resource_path, FileAccess::READ, &open_error);
	CHECK(open_error == OK);
	REQUIRE(outer_reader.is_valid());
	CHECK(outer_reader->get_as_utf8_string() == outer_expected_payload);

	REQUIRE(packed_data->add_pack(embedded_nested_pack_name, true, 0) == OK);
	REQUIRE(OS::get_singleton()->set_cwd(directory_alpha) == OK);

	Ref<FileAccess> nested_reader = FileAccess::open(nested_inner_resource_path, FileAccess::READ, &open_error);
	CHECK(open_error == OK);
	REQUIRE(nested_reader.is_valid());
	CHECK(nested_reader->get_as_utf8_string() == nested_expected_payload);

#if defined(UNIX_ENABLED)
	const String outside_directory = TestUtils::get_temp_path("pck_relative_cwd_outside");
	const String symlink_child_directory = outside_directory.path_join("child");
	cleanup.temporary_directories.push_back(outside_directory);
	_remove_test_directory_if_exists(outside_directory);
	REQUIRE(DirAccess::make_dir_recursive_absolute(symlink_child_directory) == OK);

	Ref<DirAccess> filesystem_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem_access.is_valid());
	const String symlink_path = directory_alpha.path_join("link");
	if (filesystem_access->create_link(symlink_child_directory, symlink_path) == OK) {
		const String symlink_resource_path = "res://relative_pck_cwd_test/symlink_parent_payload.txt";
		const String real_pack_path = outside_directory.path_join("target.pck");
		const String decoy_pack_path = directory_alpha.path_join("target.pck");
		const String real_expected_payload = "Real outside PCK payload resolved through symlink";
		const String decoy_payload = "Decoy working-directory PCK payload";
		{
			PCKPacker real_packer;
			REQUIRE(real_packer.pck_start(real_pack_path) == OK);
			REQUIRE(real_packer.add_file_from_buffer(symlink_resource_path, real_expected_payload.to_utf8_buffer()) == OK);
			REQUIRE(real_packer.flush() == OK);
		}
		{
			PCKPacker decoy_packer;
			REQUIRE(decoy_packer.pck_start(decoy_pack_path) == OK);
			REQUIRE(decoy_packer.add_file_from_buffer(symlink_resource_path, decoy_payload.to_utf8_buffer()) == OK);
			REQUIRE(decoy_packer.flush() == OK);
		}

		cleanup.registered_pack_files.push_back(symlink_resource_path);

		REQUIRE(OS::get_singleton()->set_cwd(directory_alpha) == OK);
		REQUIRE(packed_data->add_pack("link/../target.pck", true, 0) == OK);
		REQUIRE(OS::get_singleton()->set_cwd(directory_beta) == OK);

		Ref<FileAccess> symlink_reader = FileAccess::open(symlink_resource_path, FileAccess::READ, &open_error);
		CHECK(open_error == OK);
		REQUIRE(symlink_reader.is_valid());
		CHECK(symlink_reader->get_as_utf8_string() == real_expected_payload);
	}
#endif // UNIX_ENABLED
}

#ifdef MINIZIP_ENABLED
TEST_CASE("[PCKPacker] Relative ZIP pack remains readable after CWD changes") {
	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);

	static bool zip_archive_registered = false;
	if (!zip_archive_registered) {
		packed_data->add_pack_source(ZipArchive::get_singleton());
		zip_archive_registered = true;
	}

	ScopedPackAndWorkingDirectoryCleanup cleanup;
	const String directory_alpha = TestUtils::get_temp_path("zip_relative_cwd_alpha");
	const String directory_beta = TestUtils::get_temp_path("zip_relative_cwd_beta");
	cleanup.temporary_directories.push_back(directory_alpha);
	cleanup.temporary_directories.push_back(directory_beta);
	_remove_test_directory_if_exists(directory_alpha);
	_remove_test_directory_if_exists(directory_beta);
	REQUIRE(DirAccess::make_dir_recursive_absolute(directory_alpha) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(directory_beta) == OK);

	const String zip_disk_path = directory_alpha.path_join("relative_test.zip");
	const String entry_relative_path = "relative_zip_cwd_test/zip_payload.txt";
	const String mounted_resource_path = "res://" + entry_relative_path;
	const String expected_payload = "Relative ZIP pack payload survives CWD transition";
	REQUIRE(_write_single_entry_zip_archive(zip_disk_path, entry_relative_path, expected_payload.utf8()) == OK);

	cleanup.registered_pack_files.push_back(mounted_resource_path);

	REQUIRE(OS::get_singleton()->set_cwd(directory_alpha) == OK);
	REQUIRE(packed_data->add_pack("./relative_test.zip", true, 0) == OK);

	REQUIRE(OS::get_singleton()->set_cwd(directory_beta) == OK);

	Error open_error = FAILED;
	Ref<FileAccess> zip_reader = FileAccess::open(mounted_resource_path, FileAccess::READ, &open_error);
	CHECK(open_error == OK);
	REQUIRE(zip_reader.is_valid());
	CHECK(zip_reader->get_as_utf8_string() == expected_payload);

#if defined(UNIX_ENABLED)
	const String outside_directory = TestUtils::get_temp_path("zip_relative_cwd_outside");
	const String symlink_child_directory = outside_directory.path_join("child");
	cleanup.temporary_directories.push_back(outside_directory);
	_remove_test_directory_if_exists(outside_directory);
	REQUIRE(DirAccess::make_dir_recursive_absolute(symlink_child_directory) == OK);

	Ref<DirAccess> filesystem_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem_access.is_valid());
	const String symlink_path = directory_alpha.path_join("link");
	if (filesystem_access->create_link(symlink_child_directory, symlink_path) == OK) {
		const String symlink_entry_relative_path = "relative_zip_cwd_test/symlink_parent_payload.txt";
		const String symlink_mounted_resource_path = "res://" + symlink_entry_relative_path;
		const String real_zip_path = outside_directory.path_join("target.zip");
		const String decoy_zip_path = directory_alpha.path_join("target.zip");
		const String real_expected_payload = "Real outside ZIP payload resolved through symlink";
		const String decoy_payload = "Decoy working-directory ZIP payload";
		REQUIRE(_write_single_entry_zip_archive(real_zip_path, symlink_entry_relative_path, real_expected_payload.utf8()) == OK);
		REQUIRE(_write_single_entry_zip_archive(decoy_zip_path, symlink_entry_relative_path, decoy_payload.utf8()) == OK);

		cleanup.registered_pack_files.push_back(symlink_mounted_resource_path);

		REQUIRE(OS::get_singleton()->set_cwd(directory_alpha) == OK);
		REQUIRE(packed_data->add_pack("link/../target.zip", true, 0) == OK);
		REQUIRE(OS::get_singleton()->set_cwd(directory_beta) == OK);

		Ref<FileAccess> symlink_zip_reader = FileAccess::open(symlink_mounted_resource_path, FileAccess::READ, &open_error);
		CHECK(open_error == OK);
		REQUIRE(symlink_zip_reader.is_valid());
		CHECK(symlink_zip_reader->get_as_utf8_string() == real_expected_payload);
	}
#endif // UNIX_ENABLED
}
#endif // MINIZIP_ENABLED

} // namespace TestPCKPacker
