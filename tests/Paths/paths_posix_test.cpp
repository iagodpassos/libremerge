// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge: POSIX counterpart of upstream's paths_test.cpp, exercising the
// ports/paths_posix.cpp implementation with POSIX path semantics.
#include "pch.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include "paths.h"
#include "Environment.h"
#include "PathContext.h"

namespace
{

class PathTestPosix : public testing::Test
{
};

TEST_F(PathTestPosix, EndsWithSlash)
{
	EXPECT_TRUE(paths::EndsWithSlash(_T("/")));
	EXPECT_TRUE(paths::EndsWithSlash(_T("/tmp/")));
	EXPECT_FALSE(paths::EndsWithSlash(_T("/tmp")));
	EXPECT_FALSE(paths::EndsWithSlash(_T("")));
}

TEST_F(PathTestPosix, Exists)
{
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::DoesPathExist(_T("/")));
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::DoesPathExist(_T("/tmp")));
	EXPECT_EQ(paths::IS_EXISTING_FILE, paths::DoesPathExist(_T("/bin/sh")));
	EXPECT_EQ(paths::DOES_NOT_EXIST, paths::DoesPathExist(_T("/nonexistent_lm_path")));
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::DoesPathExist(_T(".")));
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::DoesPathExist(_T("..")));
}

TEST_F(PathTestPosix, FindFileName)
{
	EXPECT_EQ(String(_T("file.txt")), paths::FindFileName(_T("/tmp/file.txt")));
	EXPECT_EQ(String(_T("file.txt")), paths::FindFileName(_T("file.txt")));
	EXPECT_EQ(String(_T("file")), paths::FindFileName(_T("/a/b/file")));
}

TEST_F(PathTestPosix, FindExtension)
{
	EXPECT_EQ(String(_T(".txt")), paths::FindExtension(_T("/tmp/file.txt")));
	EXPECT_EQ(String(_T("")), paths::FindExtension(_T("/tmp.d/file")));
	EXPECT_EQ(String(_T(".gz")), paths::FindExtension(_T("archive.tar.gz")));
}

TEST_F(PathTestPosix, RemoveExtension)
{
	EXPECT_EQ(String(_T("/tmp/file")), paths::RemoveExtension(_T("/tmp/file.txt")));
	EXPECT_EQ(String(_T("/tmp.d/file")), paths::RemoveExtension(_T("/tmp.d/file")));
}

TEST_F(PathTestPosix, Normalize)
{
	String path = _T("/tmp/");
	paths::normalize(path);
	EXPECT_EQ(String(_T("/tmp")), path);

	path = _T("/tmp/../tmp/./");
	paths::normalize(path);
	EXPECT_EQ(String(_T("/tmp")), path);

	path = _T("/");
	paths::normalize(path);
	EXPECT_EQ(String(_T("/")), path);
}

TEST_F(PathTestPosix, GetLongPath)
{
	EXPECT_EQ(String(_T("/tmp")), paths::GetLongPath(_T("/tmp/foo/..")));
	EXPECT_EQ(String(_T("/")), paths::GetLongPath(_T("/../..")));
	// relative paths become absolute against the current directory
	String longPath = paths::GetLongPath(_T("relfile.txt"));
	EXPECT_TRUE(paths::IsPathAbsolute(longPath));
	// environment expansion
	const char *home = getenv("HOME");
	if (home != nullptr)
		EXPECT_EQ(String(home), paths::GetLongPath(_T("%HOME%")));
}

TEST_F(PathTestPosix, CreateIfNeeded)
{
	String base = env::GetTemporaryPath();
	ASSERT_FALSE(base.empty());
	String deep = paths::ConcatPath(base, _T("lm_paths_test/a/b/c"));
	EXPECT_TRUE(paths::CreateIfNeeded(deep));
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::DoesPathExist(deep));
	// cleanup
	rmdir(deep.c_str());
	rmdir(paths::GetParentPath(deep).c_str());
	rmdir(paths::GetParentPath(paths::GetParentPath(deep)).c_str());
	rmdir(paths::ConcatPath(base, _T("lm_paths_test")).c_str());
	EXPECT_FALSE(paths::CreateIfNeeded(_T("")));
}

TEST_F(PathTestPosix, ConcatPath)
{
	EXPECT_EQ(String(_T("/a/b")), paths::ConcatPath(_T("/a"), _T("b")));
	EXPECT_EQ(String(_T("/a/b")), paths::ConcatPath(_T("/a/"), _T("b")));
	EXPECT_EQ(String(_T("/a/b")), paths::ConcatPath(_T("/a"), _T("/b")));
	EXPECT_EQ(String(_T("/a/b")), paths::ConcatPath(_T("/a/"), _T("/b")));
	EXPECT_EQ(String(_T("/a")), paths::ConcatPath(_T("/a"), _T("")));
	EXPECT_EQ(String(_T("b")), paths::ConcatPath(_T(""), _T("b")));
}

TEST_F(PathTestPosix, GetParentPath)
{
	EXPECT_EQ(String(_T("/a/b")), paths::GetParentPath(_T("/a/b/c")));
	EXPECT_EQ(String(_T("/a/b")), paths::GetParentPath(_T("/a/b/c/")));
	EXPECT_EQ(String(_T("/")), paths::GetParentPath(_T("/a")));
}

TEST_F(PathTestPosix, GetLastSubdir)
{
	EXPECT_EQ(String(_T("/c")), paths::GetLastSubdir(_T("/a/b/c")));
	EXPECT_EQ(String(_T("/c")), paths::GetLastSubdir(_T("/a/b/c/")));
}

TEST_F(PathTestPosix, IsPathAbsolute)
{
	EXPECT_TRUE(paths::IsPathAbsolute(_T("/")));
	EXPECT_TRUE(paths::IsPathAbsolute(_T("/tmp/file")));
	EXPECT_FALSE(paths::IsPathAbsolute(_T("file.txt")));
	EXPECT_FALSE(paths::IsPathAbsolute(_T("./file.txt")));
	// Windows-style absolutes are still recognized by the engine
	EXPECT_TRUE(paths::IsPathAbsolute(_T("C:\\dir\\file")));
}

TEST_F(PathTestPosix, SplitFilename)
{
	String path, name, ext;
	paths::SplitFilename(_T("/tmp/dir/file.txt"), &path, &name, &ext);
	EXPECT_EQ(String(_T("/tmp/dir")), path);
	EXPECT_EQ(String(_T("file")), name);
	EXPECT_EQ(String(_T("txt")), ext);

	path.clear(); name.clear(); ext.clear();
	paths::SplitFilename(_T("file"), &path, &name, &ext);
	EXPECT_EQ(String(_T("")), path);
	EXPECT_EQ(String(_T("file")), name);
	EXPECT_EQ(String(_T("")), ext);
}

TEST_F(PathTestPosix, GetPathOnly)
{
	EXPECT_EQ(String(_T("/tmp/dir")), paths::GetPathOnly(_T("/tmp/dir/file.txt")));
}

TEST_F(PathTestPosix, IsURL)
{
	EXPECT_TRUE(paths::IsURL(_T("https://example.com/x")));
	EXPECT_FALSE(paths::IsURL(_T("/tmp/file")));
	EXPECT_FALSE(paths::IsURL(_T("C:\\file")));
}

TEST_F(PathTestPosix, SlashConversion)
{
	EXPECT_EQ(String(_T("a\\b\\c")), paths::ToWindowsPath(_T("a/b/c")));
	EXPECT_EQ(String(_T("a/b/c")), paths::ToUnixPath(_T("a\\b\\c")));
}

TEST_F(PathTestPosix, IsNullDeviceName)
{
	EXPECT_TRUE(paths::IsNullDeviceName(_T("/dev/null")));
	EXPECT_TRUE(paths::IsNullDeviceName(_T("NUL")));
	EXPECT_FALSE(paths::IsNullDeviceName(_T("/dev/zero")));
}

TEST_F(PathTestPosix, GetPairComparability)
{
	PathContext both(_T("/tmp"), _T("/"));
	EXPECT_EQ(paths::IS_EXISTING_DIR, paths::GetPairComparability(both));
	PathContext files(_T("/bin/sh"), _T("/bin/sh"));
	EXPECT_EQ(paths::IS_EXISTING_FILE, paths::GetPairComparability(files));
	PathContext mixed(_T("/bin/sh"), _T("/tmp"));
	EXPECT_EQ(paths::DOES_NOT_EXIST, paths::GetPairComparability(mixed));
}

} // namespace
