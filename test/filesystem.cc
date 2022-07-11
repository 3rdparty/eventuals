#include "eventuals/filesystem.h"

#include <filesystem>
#include <fstream>

#include "event-loop-test.h"
#include "eventuals/closure.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::filesystem::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

class FilesystemTest : public ::eventuals::test::EventLoopTest {};


TEST_F(FilesystemTest, OpenAndCloseFileSucceed) {
  const std::filesystem::path path = "test_openclose_succeed";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0)
      | Then([&path](auto&& file) {
             return Closure([&path, file = std::move(file)]() mutable {
               EXPECT_TRUE(file.IsOpen());
               EXPECT_TRUE(std::filesystem::exists(path));
               return CloseFile(std::move(file))
                   | Then([&path]() {
                        std::filesystem::remove(path);
                        EXPECT_FALSE(std::filesystem::exists(path));
                      });
             });
           });

  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, OpenFileFail) {
  const std::filesystem::path path = "test_open_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_FALSE(std::filesystem::exists(path));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("no such file or directory")));
}


// TODO(FolMing): Figure out a way how to make CloseFile function fail.
// TEST_F(FilesystemTest, CloseFileFail)


TEST_F(FilesystemTest, ReadFileSucceed) {
  const std::filesystem::path path = "test_readfile_succeed";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);

  ofs << test_string;
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_RDONLY, 0)
      | Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return ReadFile(file, test_string.size(), 0)
                   | Then([&](auto&& data) {
                        EXPECT_EQ(test_string, data);
                        return CloseFile(std::move(file));
                      })
                   | Then([&]() {
                        std::filesystem::remove(path);
                        EXPECT_FALSE(std::filesystem::exists(path));
                      });
             });
           });

  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, ReadFileFail) {
  const std::filesystem::path path = "test_readfile_fail";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);

  ofs << test_string;
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  // Try to read from a File opened with WriteOnly flag.
  auto e = OpenFile(path, UV_FS_O_WRONLY, 0)
      | Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return ReadFile(file, test_string.size(), 0)
                   | Then([&](auto&& data) {
                        EXPECT_EQ(test_string, data);
                        return CloseFile(std::move(file));
                      });
             });
           });

  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));

  // NOTE: not checking 'what()' of error because it differs across
  // operating systems.
  EXPECT_THROW(future.get(), std::runtime_error);
}


TEST_F(FilesystemTest, WriteFileSucceed) {
  const std::filesystem::path path = "test_writefile_succeed";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = OpenFile(path, UV_FS_O_WRONLY, 0)
      | Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return WriteFile(file, test_string, 0)
                   | Then([&]() {
                        return CloseFile(std::move(file));
                      })
                   | Then([&]() {
                        std::ifstream ifs(path);
                        std::string ifs_read_string;
                        ifs_read_string.resize(test_string.size());
                        ifs.read(
                            const_cast<char*>(ifs_read_string.data()),
                            test_string.size());
                        ifs.close();

                        EXPECT_EQ(ifs_read_string, test_string);

                        std::filesystem::remove(path);
                        EXPECT_FALSE(std::filesystem::exists(path));
                      });
             });
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, WriteFileFail) {
  const std::filesystem::path path = "test_writefile_fail";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  // Try to write to a File opened with ReadOnly flag.
  auto e = OpenFile(path, UV_FS_O_RDONLY, 0)
      | Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return WriteFile(file, test_string, 0)
                   | Then([&]() {
                        return CloseFile(std::move(file));
                      });
             });
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));

  // NOTE: not checking 'what()' of error because it differs across
  // operating systems.
  EXPECT_THROW(future.get(), std::runtime_error);
}


TEST_F(FilesystemTest, UnlinkFileSucceed) {
  const std::filesystem::path path = "test_unlinkfile_succeed";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = UnlinkFile(path)
      | Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, UnlinkFileFail) {
  const std::filesystem::path path = "test_unlinkfile_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = UnlinkFile(path);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("no such file or directory")));
}


TEST_F(FilesystemTest, MakeDirectorySucceed) {
  const std::filesystem::path path = "test_mkdir_succeed";

  auto e = MakeDirectory(path, 0)
      | Then([&]() {
             EXPECT_TRUE(std::filesystem::exists(path));
             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, MakeDirectoryFail) {
  const std::filesystem::path path = "test_mkdir_fail";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = MakeDirectory(path, 0);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("file already exists")));
}


TEST_F(FilesystemTest, RemoveDirectorySucceed) {
  const std::filesystem::path path = "test_rmdir_succeed";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = RemoveDirectory(path)
      | Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, RemoveDirectoryFail) {
  const std::filesystem::path path = "test_rmdir_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = RemoveDirectory(path);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("no such file or directory")));
}


TEST_F(FilesystemTest, CopyFileSucceed) {
  const std::filesystem::path src = "test_srccp_succeed";
  const std::filesystem::path dst = "test_dstcp_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = CopyFile(src, dst, 0)
      | Then([&]() {
             EXPECT_TRUE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, CopyFileFail) {
  const std::filesystem::path src = "test_srccp_fail";
  const std::filesystem::path dst = "test_dstcp_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = CopyFile(src, dst, 0);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("no such file or directory")));
}


TEST_F(FilesystemTest, RenameFileSucceed) {
  const std::filesystem::path src = "test_srcrename_succeed";
  const std::filesystem::path dst = "test_dstrename_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = RenameFile(src, dst)
      | Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  future.get();
}


TEST_F(FilesystemTest, RenameFileFail) {
  const std::filesystem::path src = "test_srcrename_fail";
  const std::filesystem::path dst = "test_dstrename_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = RenameFile(src, dst);
  auto [future, k] = PromisifyForTest(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("no such file or directory")));
}

} // namespace
} // namespace eventuals::filesystem::test
