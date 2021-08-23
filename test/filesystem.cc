#include "stout/filesystem.h"

#include <filesystem>
#include <fstream>

#include "event-loop-test.h"
#include "gtest/gtest.h"
#include "stout/eventual.h"
#include "stout/lambda.h"
#include "stout/terminal.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using eventuals::EventLoop;
using eventuals::Lambda;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::filesystem::File;
using eventuals::filesystem::ReadResult;

using eventuals::filesystem::CloseFile;
using eventuals::filesystem::CopyFile;
using eventuals::filesystem::MakeDir;
using eventuals::filesystem::OpenFile;
using eventuals::filesystem::ReadFile;
using eventuals::filesystem::RemoveDir;
using eventuals::filesystem::RenameFile;
using eventuals::filesystem::UnlinkFile;
using eventuals::filesystem::WriteFile;

class FilesystemTest : public EventLoopTest {};

TEST_F(FilesystemTest, OpenAndCloseFileSucceed) {
  const std::filesystem::path path = "test_openclose_succeed";

  std::ofstream ofs(path);
  ofs.close();

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0)
      | Then([&path](auto&& file) {
             EXPECT_TRUE(std::filesystem::exists(path));
             EXPECT_TRUE(file.is_open());
             return CloseFile(file);
           })
      | Lambda([&path](auto&& file) {
             EXPECT_FALSE(file.is_open());
             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, OpenFileFail) {
  const std::filesystem::path path = "test_open_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_FALSE(std::filesystem::exists(path));

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, CloseFileFail) {
  const uv_file not_valid_file_descriptor = -14283;
  File not_valid_file(not_valid_file_descriptor);

  auto e = CloseFile(not_valid_file);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, ReadFileSucceed) {
  const std::filesystem::path path = "test_readfile_succeed";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);

  ofs << test_string;
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0)
      | Then([&test_string](File&& file) {
             EXPECT_TRUE(file.is_open());
             return ReadFile(file, test_string.size(), 0);
           })
      | Then([&test_string](ReadResult&& result) {
             EXPECT_EQ(test_string, result.data);
             return CloseFile(result.file);
           })
      | Lambda([&path](File&& file) {
             EXPECT_FALSE(file.is_open());
             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, ReadFileFail) {
  const uv_file not_valid_file_descriptor = -14283;
  File not_valid_file(not_valid_file_descriptor);

  auto e = ReadFile(not_valid_file, 1000, 0);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, WriteFileSucceed) {
  const std::filesystem::path path = "test_writefile_succeed";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);
  ofs.close();

  auto e = OpenFile(path, UV_FS_O_WRONLY, 0)
      | Then([&path, &test_string](File&& file) {
             EXPECT_TRUE(std::filesystem::exists(path));
             EXPECT_TRUE(file.is_open());
             return WriteFile(file, test_string, 0);
           })
      | Then([](File&& file) {
             EXPECT_TRUE(file.is_open());
             return CloseFile(file);
           })
      | Lambda([&path, &test_string](auto&& file) {
             std::ifstream ifs(path);
             std::string ifs_read_string;
             ifs_read_string.resize(test_string.size());
             ifs.read(const_cast<char*>(ifs_read_string.data()), test_string.size());
             ifs.close();

             EXPECT_EQ(ifs_read_string, test_string);

             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, WriteFileFail) {
  const uv_file not_valid_file_descriptor = -14283;
  File not_valid_file(not_valid_file_descriptor);

  auto e = WriteFile(not_valid_file, "Hello GTest!", 0);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, UnlinkFileSucceed) {
  const std::filesystem::path path = "test_unlinkfile_succeed";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = UnlinkFile(path)
      | Lambda([&path]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, UnlinkFileFail) {
  const std::filesystem::path path = "test_unlinkfile_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = UnlinkFile(path);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, MakeDirSucceed) {
  const std::filesystem::path path = "test_mkdir_succeed";

  auto e = MakeDir(path, 0)
      | Lambda([&path]() {
             EXPECT_TRUE(std::filesystem::exists(path));
             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, MakeDirFail) {
  const std::filesystem::path path = "test_mkdir_fail";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = MakeDir(path, 0);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, RemoveDirSucceed) {
  const std::filesystem::path path = "test_rmdir_succeed";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = RemoveDir(path)
      | Lambda([&path]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, RemoveDirFail) {
  const std::filesystem::path path = "test_rmdir_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = RemoveDir(path);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, CopyFileSucceed) {
  const std::filesystem::path src = "test_srccp_succeed";
  const std::filesystem::path dst = "test_dstcp_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = CopyFile(src, dst, 0)
      | Lambda([&src, &dst]() {
             EXPECT_TRUE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, CopyFileFail) {
  const std::filesystem::path src = "test_srccp_fail";
  const std::filesystem::path dst = "test_dstcp_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = CopyFile(src, dst, 0);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(FilesystemTest, RenameFileSucceed) {
  const std::filesystem::path src = "test_srcrename_succeed";
  const std::filesystem::path dst = "test_dstrename_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = RenameFile(src, dst)
      | Lambda([&src, &dst]() {
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(FilesystemTest, RenameFileFail) {
  const std::filesystem::path src = "test_srcrename_fail";
  const std::filesystem::path dst = "test_dstrename_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = RenameFile(src, dst);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  EXPECT_THROW(future.get(), const char*);
}
