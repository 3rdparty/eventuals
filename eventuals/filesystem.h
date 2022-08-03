#pragma once

#include <filesystem> // std::filesystem::path
#include <optional>

#include "eventuals/event-loop.h"
#include "eventuals/eventual.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace filesystem {

////////////////////////////////////////////////////////////////////////

// Moveable, not Copyable.
class Request final {
 public:
  Request() = default;

  Request(const Request&) = delete;
  Request(Request&& that) noexcept
    : request_(that.request_) {
    that.request_.reset();
  }

  Request& operator=(const Request&) = delete;
  Request& operator=(Request&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    if (request_.has_value()) {
      // libuv doesn't use loop in this call, so we don't have to be
      // within the event loop.
      uv_fs_req_cleanup(&*request_);
    }

    request_ = that.request_;
    that.request_.reset();

    return *this;
  }

  // Trying to cleanup an empty uv_req_t gives us an exception.
  ~Request() {
    if (request_.has_value()) {
      // libuv doesn't use loop in this call, so we don't have to be
      // within the event loop.
      uv_fs_req_cleanup(&*request_);
    }
  }

  // Seamless access to the fields in uv_fs_t structure.
  uv_fs_t* operator->() {
    CHECK(request_.has_value());
    return &*request_;
  }

  // Used as an adaptor to libuv functions.
  operator uv_fs_t*() {
    CHECK(request_.has_value());
    return &*request_;
  }

 private:
  // Stores request structure in stack, it gets moved around a lot,
  // optional eases the ownership control.
  std::optional<uv_fs_t> request_ = std::make_optional<uv_fs_t>();
};

////////////////////////////////////////////////////////////////////////

// Moveable, not Copyable.
class File final {
 public:
#if _WIN32
  // NOTE: default constructor should not exist or be used but is
  // necessary on Windows so this type can be used as a type parameter
  // to 'std::promise', see: https://bit.ly/VisualStudioStdPromiseBug
  File() = default;
#endif

  File(const File& that) = delete;

  File(File&& that) noexcept
    : descriptor_(that.descriptor_) {
    that.descriptor_.reset();
  }

  File& operator=(const File& that) = delete;

  File& operator=(File&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    if (descriptor_.has_value()) {
      // No callback allows us to synchronously use this function,
      // loop is not needed in this variant.
      uv_fs_close(nullptr, Request(), *descriptor_, NULL);
    }

    descriptor_ = that.descriptor_;
    that.descriptor_.reset();

    return *this;
  }

  ~File() {
    if (descriptor_.has_value()) {
      // No callback allows us to synchronously use this function,
      // loop is not needed in this variant.
      uv_fs_close(nullptr, Request(), *descriptor_, NULL);
    }
  }

  bool IsOpen() {
    return descriptor_.has_value();
  }

  // Used as an adaptor to libuv functions.
  operator uv_file() const {
    return descriptor_.value();
  }

 private:
  // Stores file descriptor in stack, it gets moved around a lot,
  // optional eases the ownership control.
  std::optional<uv_file> descriptor_;

  // Takes ownership of the descriptor.
  File(const uv_file& descriptor)
    : descriptor_(descriptor) {}

  void MarkAsClosed() {
    descriptor_.reset();
  }

  friend auto OpenFile(
      const std::filesystem::path& path,
      const int& flags,
      const int& mode,
      EventLoop& loop);

  friend auto CloseFile(File&& file, EventLoop& loop);
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto OpenFile(
    const std::filesystem::path& path,
    const int& flags,
    const int& mode,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    int flags;
    int mode;
    std::filesystem::path path;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "OpenFile",
      Eventual<File>()
          .raises<std::runtime_error>()
          .context(Data{loop, flags, mode, path})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_open(
                data.loop,
                data.request,
                data.path.string().c_str(),
                data.flags,
                data.mode,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result >= 0) {
                    k.Start(File(request->result));
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto CloseFile(
    File&& file,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    File file;
    Request request;

    void* k = nullptr;
  };

  return loop.Schedule(
      "CloseFile",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, std::move(file)})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_close(
                data.loop,
                data.request,
                data.file,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    data.file.MarkAsClosed();
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  };
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto ReadFile(
    const File& file,
    const size_t& bytes_to_read,
    const size_t& offset,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    const File& file;
    size_t bytes_to_read;
    size_t offset;
    EventLoop::Buffer buffer;
    Request request;

    void* k = nullptr;
  };

  return loop.Schedule(
      "ReadFile",
      Eventual<std::string>()
          .raises<std::runtime_error>()
          .context(Data{loop, file, bytes_to_read, offset, bytes_to_read})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_read(
                data.loop,
                data.request,
                data.file,
                data.buffer,
                1,
                data.offset,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result >= 0) {
                    k.Start(data.buffer.Extract());
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  };
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto WriteFile(
    const File& file,
    const std::string& data,
    const size_t& offset,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    const File& file;
    EventLoop::Buffer buffer;
    size_t offset;
    Request request;

    void* k = nullptr;
  };

  return loop.Schedule(
      "WriteFile",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, file, data, offset})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_write(
                data.loop,
                data.request,
                data.file,
                data.buffer,
                1,
                data.offset,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result >= 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  };
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto UnlinkFile(
    const std::filesystem::path& path,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "UnlinkFile",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, path})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_unlink(
                data.loop,
                data.request,
                data.path.string().c_str(),
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto MakeDirectory(
    const std::filesystem::path& path,
    const int& mode,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;
    int mode;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "MakeDirectory",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, path, mode})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_mkdir(
                data.loop,
                data.request,
                data.path.string().c_str(),
                data.mode,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto RemoveDirectory(
    const std::filesystem::path& path,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "RemoveDirectory",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, path})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_rmdir(
                data.loop,
                data.request,
                data.path.string().c_str(),
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto CopyFile(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    const int& flags,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path src;
    std::filesystem::path dst;
    int flags;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "CopyFile",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, src, dst, flags})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_copyfile(
                data.loop,
                data.request,
                data.src.string().c_str(),
                data.dst.string().c_str(),
                data.flags,
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto RenameFile(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    EventLoop& loop = EventLoop::Default()) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path src;
    std::filesystem::path dst;

    Request request;
    void* k = nullptr;
  };

  return loop.Schedule(
      "RenameFile",
      Eventual<void>()
          .raises<std::runtime_error>()
          .context(Data{loop, src, dst})
          .start([](auto& data, auto& k) mutable {
            using K = std::decay_t<decltype(k)>;

            data.k = &k;
            data.request->data = &data;

            auto error = uv_fs_rename(
                data.loop,
                data.request,
                data.src.string().c_str(),
                data.dst.string().c_str(),
                [](uv_fs_t* request) {
                  auto& data = *static_cast<Data*>(request->data);
                  auto& k = *static_cast<K*>(data.k);
                  if (request->result == 0) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(uv_strerror(request->result)));
                  }
                });

            if (error) {
              static_cast<K*>(data.k)->Fail(
                  std::runtime_error(uv_strerror(error)));
            }
          }));
}

////////////////////////////////////////////////////////////////////////

} // namespace filesystem
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
