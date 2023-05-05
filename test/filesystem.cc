#include "eventuals/filesystem.h"

#include <filesystem>
#include <fstream>

#include "event-loop-test.h"
#include "eventuals/closure.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

  auto e = [&path]() {
    return OpenFile(path, UV_FS_O_RDONLY, 0)
        >> Then([&path](File&& file) {
             return Closure([&path, file = std::move(file)]() mutable {
               EXPECT_TRUE(file.IsOpen());
               EXPECT_TRUE(std::filesystem::exists(path));
               return CloseFile(std::move(file))
                   >> Then([&path]() {
                        std::filesystem::remove(path);
                        EXPECT_FALSE(std::filesystem::exists(path));
                      });
             });
           });
  };

  *e();
}


TEST_F(FilesystemTest, OpenFileFail) {
  const std::filesystem::path path = "test_open_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = [&path]() { return OpenFile(path, UV_FS_O_RDONLY, 0); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("no such file or directory")));

  EXPECT_FALSE(std::filesystem::exists(path));
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

  auto e = [&]() {
    return OpenFile(path, UV_FS_O_RDONLY, 0)
        >> Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return ReadFile(file, test_string.size(), 0)
                   >> Then([&](std::string&& data) {
                        EXPECT_EQ(test_string, data);
                        return CloseFile(std::move(file));
                      })
                   >> Then([&]() {
                        std::filesystem::remove(path);
                        EXPECT_FALSE(std::filesystem::exists(path));
                      });
             });
           });
  };

  *e();
}


TEST_F(FilesystemTest, ReadFileFail) {
  const std::filesystem::path path = "test_readfile_fail";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);

  ofs << test_string;
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  // Try to read from a File opened with WriteOnly flag.
  auto e = [&]() {
    return OpenFile(path, UV_FS_O_WRONLY, 0)
        >> Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return ReadFile(file, test_string.size(), 0)
                   >> Then([&](std::string&& data) {
                        EXPECT_EQ(test_string, data);
                        return CloseFile(std::move(file));
                      });
             });
           });
  };

  // NOTE: not checking 'what()' of error because it differs across
  // operating systems.
  EXPECT_THROW(*e(), RuntimeError);

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));
}


TEST_F(FilesystemTest, WriteFileSucceed) {
  const std::filesystem::path path = "test_writefile_succeed";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = [&]() {
    return OpenFile(path, UV_FS_O_WRONLY, 0)
        >> Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return WriteFile(file, test_string, 0)
                   >> Then([&]() {
                        return CloseFile(std::move(file));
                      })
                   >> Then([&]() {
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
  };

  *e();
}


TEST_F(FilesystemTest, WriteFileFail) {
  const std::filesystem::path path = "test_writefile_fail";
  const std::string test_string = "Hello GTest!";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  // Try to write to a File opened with ReadOnly flag.
  auto e = [&]() {
    return OpenFile(path, UV_FS_O_RDONLY, 0)
        >> Then([&](File&& file) {
             return Closure([&, file = std::move(file)]() mutable {
               return WriteFile(file, test_string, 0)
                   >> Then([&]() {
                        return CloseFile(std::move(file));
                      });
             });
           });
  };

  // NOTE: not checking 'what()' of error because it differs across
  // operating systems.
  EXPECT_THROW(*e(), RuntimeError);

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));
}


TEST_F(FilesystemTest, UnlinkFileSucceed) {
  const std::filesystem::path path = "test_unlinkfile_succeed";

  std::ofstream ofs(path);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = [&path]() {
    return UnlinkFile(path)
        >> Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  };

  *e();
}


TEST_F(FilesystemTest, UnlinkFileFail) {
  const std::filesystem::path path = "test_unlinkfile_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = [&path]() { return UnlinkFile(path); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("no such file or directory")));

  EXPECT_FALSE(std::filesystem::exists(path));
}


TEST_F(FilesystemTest, MakeDirectorySucceed) {
  const std::filesystem::path path = "test_mkdir_succeed";

  auto e = [&]() {
    return MakeDirectory(path, 0)
        >> Then([&]() {
             EXPECT_TRUE(std::filesystem::exists(path));
             std::filesystem::remove(path);
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  };

  *e();
}


TEST_F(FilesystemTest, MakeDirectoryFail) {
  const std::filesystem::path path = "test_mkdir_fail";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = [&]() { return MakeDirectory(path, 0); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("file already exists")));

  std::filesystem::remove(path);
  EXPECT_FALSE(std::filesystem::exists(path));
}


TEST_F(FilesystemTest, RemoveDirectorySucceed) {
  const std::filesystem::path path = "test_rmdir_succeed";

  std::filesystem::create_directory(path);
  EXPECT_TRUE(std::filesystem::exists(path));

  auto e = [&]() {
    return RemoveDirectory(path)
        >> Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(path));
           });
  };

  *e();
}


TEST_F(FilesystemTest, RemoveDirectoryFail) {
  const std::filesystem::path path = "test_rmdir_fail";

  EXPECT_FALSE(std::filesystem::exists(path));

  auto e = [&]() { return RemoveDirectory(path); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("no such file or directory")));
}


TEST_F(FilesystemTest, CopyFileSucceed) {
  const std::filesystem::path src = "test_srccp_succeed";
  const std::filesystem::path dst = "test_dstcp_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = [&]() {
    return CopyFile(src, dst, 0)
        >> Then([&]() {
             EXPECT_TRUE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  };

  *e();
}


TEST_F(FilesystemTest, CopyFileFail) {
  const std::filesystem::path src = "test_srccp_fail";
  const std::filesystem::path dst = "test_dstcp_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = [&]() { return CopyFile(src, dst, 0); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("no such file or directory")));

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));
}


TEST_F(FilesystemTest, RenameFileSucceed) {
  const std::filesystem::path src = "test_srcrename_succeed";
  const std::filesystem::path dst = "test_dstrename_succeed";

  std::ofstream ofs(src);
  ofs.close();

  EXPECT_TRUE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = [&]() {
    return RenameFile(src, dst)
        >> Then([&]() {
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_TRUE(std::filesystem::exists(dst));
             std::filesystem::remove(src);
             std::filesystem::remove(dst);
             EXPECT_FALSE(std::filesystem::exists(src));
             EXPECT_FALSE(std::filesystem::exists(dst));
           });
  };

  *e();
}


TEST_F(FilesystemTest, RenameFileFail) {
  const std::filesystem::path src = "test_srcrename_fail";
  const std::filesystem::path dst = "test_dstrename_fail";

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));

  auto e = [&]() { return RenameFile(src, dst); };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("no such file or directory")));

  EXPECT_FALSE(std::filesystem::exists(src));
  EXPECT_FALSE(std::filesystem::exists(dst));
}

} // namespace
} // namespace eventuals::filesystem::test
