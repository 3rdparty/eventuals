#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "stout/event-loop.h"
#include "stout/eventual.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {
namespace filesystem {

////////////////////////////////////////////////////////////////////////

// Moveable, not Copyable.
class Request {
 public:
  Request() {}

  Request(const Request&) = delete;
  Request(Request&& other) {
    req_ = std::move(other.req_);
    other.req_.reset();
  }

  Request& operator=(const Request&) = delete;
  Request& operator=(Request&& other) {
    if (req_.has_value()) {
      uv_fs_req_cleanup(&*req_);
    }

    req_ = std::move(other.req_);
    other.req_.reset();

    return *this;
  }

  // Apparently, trying to cleanup an empty uv_req_t gives us an exception.
  ~Request() {
    if (req_.has_value()) {
      uv_fs_req_cleanup(&*req_);
    }
  }

  // Seamless access to the fields in uv_fs_t structure.
  uv_fs_t* operator->() {
    CHECK(req_.has_value());
    return &*req_;
  }

  // Used as an adaptor to libuv functions.
  operator uv_fs_t*() {
    CHECK(req_.has_value());
    return &*req_;
  }

 private:
  // Stores request structure in stack, it gets moved around a lot,
  // optional eases the ownership control.
  std::optional<uv_fs_t> req_ = std::make_optional<uv_fs_t>();
};

////////////////////////////////////////////////////////////////////////

// Moveable, not Copyable.
class File {
 public:
  File() {}

  File(const uv_file& fd)
    : fd_(fd) {
  }

  File(const File&) = delete;
  File(File&& other) {
    fd_ = std::move(other.fd_);
    other.fd_.reset();
  }

  File& operator=(const File&) = delete;
  File& operator=(File&& other) {
    if (fd_.has_value()) {
      Request req;

      // No callback allows us to synchronously use this function,
      // loop is not needed in this variant.
      uv_fs_close(nullptr, req, *fd_, NULL);
    }

    fd_ = std::move(other.fd_);
    other.fd_.reset();

    return *this;
  }

  ~File() {
    if (fd_.has_value()) {
      Request req;

      // No callback allows us to synchronously use this function,
      // loop is not needed in this variant.
      uv_fs_close(nullptr, req, *fd_, NULL);
    }
  }

  bool is_open() const {
    return fd_.has_value();
  }

  void mark_as_closed() {
    fd_.reset();
  }

  // Used as an adaptor to libuv functions.
  operator uv_file() const {
    CHECK(fd_.has_value());
    return *fd_;
  }

 private:
  // Stores file descriptor in stack, it gets moved around a lot,
  // optional eases the ownership control.
  std::optional<uv_file> fd_;
};

////////////////////////////////////////////////////////////////////////

// Moveable and Copyable.
class Buffer {
 public:
  Buffer() {
    buffer_ = uv_buf_init(nullptr, 0);
  }

  Buffer(const size_t& size) {
    data_.resize(size);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), size);
  }

  Buffer(const std::string& data)
    : data_(data) {
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());
  }

  Buffer(const Buffer& other) {
    data_ = other.data_;
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());
  }

  Buffer(Buffer&& other) {
    data_ = std::move(other.data_);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    other.buffer_.len = 0;
    other.buffer_.base = nullptr;
  }

  Buffer& operator=(const std::string& data) {
    data_ = data;
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    return *this;
  }

  Buffer& operator=(std::string&& data) {
    data_ = std::move(data);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    return *this;
  }

  Buffer& operator=(const Buffer& other) {
    data_ = other.data_;
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    return *this;
  }

  Buffer& operator=(Buffer&& other) {
    data_ = std::move(other.data_);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    other.buffer_.len = 0;
    other.buffer_.base = nullptr;

    return *this;
  }

  ~Buffer() {}

  // Extracts the data from the buffer as a universal reference.
  // Empties out the buffer inside.
  std::string&& extract() noexcept {
    buffer_.len = 0;
    buffer_.base = nullptr;

    return std::move(data_);
  }

  size_t size() const noexcept {
    return data_.size();
  }

  void resize(const size_t& size) {
    data_.resize(size, 0);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), size);
  }

  // Used as an adaptor to libuv functions.
  operator uv_buf_t*() noexcept {
    return &buffer_;
  }

 private:
  // Used for performance purposes (SSO?)
  std::string data_ = "";

  // base - ptr to data; len - size of data
  uv_buf_t buffer_ = {};
};

////////////////////////////////////////////////////////////////////////

struct ReadResult {
  std::string data;
  File file;
};

////////////////////////////////////////////////////////////////////////

inline auto OpenFile(EventLoop& loop, const std::filesystem::path& path, const int& flags, const int& mode) {
  struct Data {
    EventLoop& loop;
    int flags;
    int mode;
    std::filesystem::path path;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<File>()
      .context(Data{loop, flags, mode, path})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_open(
              loop,
              data.req,
              data.path.string().c_str(),
              data.flags,
              data.mode,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result >= 0) {
                  k.Start(File(req->result));
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto OpenFile(const std::filesystem::path& path, const int& flags, const int& mode) {
  return OpenFile(EventLoop::Default(), path, flags, mode);
}

////////////////////////////////////////////////////////////////////////

inline auto CloseFile(EventLoop& loop, File& file) {
  struct Data {
    EventLoop& loop;
    File file;
    Request req;

    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<File>()
      .context(Data{loop, std::move(file)})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_close(
              loop,
              data.req,
              data.file,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  data.file.mark_as_closed();
                  k.Start(std::move(data.file));
                } else {
                  k.Fail(uv_strerror(req->result));
                };
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto CloseFile(File& file) {
  return CloseFile(EventLoop::Default(), file);
}

////////////////////////////////////////////////////////////////////////

inline auto ReadFile(EventLoop& loop, File& file, const size_t& bytes_to_read, const size_t& offset) {
  struct Data {
    EventLoop& loop;
    File file;
    size_t bytes_to_read;
    size_t offset;
    Buffer buf;
    Request req;

    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<ReadResult>()
      .context(Data{loop, std::move(file), bytes_to_read, offset, bytes_to_read})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_read(
              loop,
              data.req,
              data.file,
              data.buf,
              1,
              data.offset,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result >= 0) {
                  k.Start(ReadResult{data.buf.extract(), std::move(data.file)});
                } else {
                  k.Fail(uv_strerror(req->result));
                };
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto ReadFile(File& file, const size_t& bytes_to_read, const size_t& offset) {
  return ReadFile(EventLoop::Default(), file, bytes_to_read, offset);
}

////////////////////////////////////////////////////////////////////////

inline auto WriteFile(EventLoop& loop, File& file, const std::string& data, const size_t& offset) {
  struct Data {
    EventLoop& loop;
    File file;
    Buffer buf;
    size_t offset;
    Request req;

    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<File>()
      .context(Data{loop, std::move(file), data, offset})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_write(
              loop,
              data.req,
              data.file,
              data.buf,
              1,
              data.offset,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result >= 0) {
                  k.Start(std::move(data.file));
                } else {
                  k.Fail(uv_strerror(req->result));
                };
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto WriteFile(File& file, const std::string& data, const size_t& offset) {
  return WriteFile(EventLoop::Default(), file, data, offset);
}

////////////////////////////////////////////////////////////////////////

inline auto UnlinkFile(EventLoop& loop, const std::filesystem::path& path) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<void>()
      .context(Data{loop, path})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_unlink(
              loop,
              data.req,
              data.path.string().c_str(),
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  k.Start();
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto UnlinkFile(const std::filesystem::path& path) {
  return UnlinkFile(EventLoop::Default(), path);
}

////////////////////////////////////////////////////////////////////////

inline auto MakeDir(EventLoop& loop, const std::filesystem::path& path, const int& mode) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;
    int mode;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<void>()
      .context(Data{loop, path, mode})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_mkdir(
              loop,
              data.req,
              data.path.string().c_str(),
              data.mode,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  k.Start();
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto MakeDir(const std::filesystem::path& path, const int& mode) {
  return MakeDir(EventLoop::Default(), path, mode);
}

////////////////////////////////////////////////////////////////////////

inline auto RemoveDir(EventLoop& loop, const std::filesystem::path& path) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path path;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<void>()
      .context(Data{loop, path})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_rmdir(
              loop,
              data.req,
              data.path.string().c_str(),
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  k.Start();
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto RemoveDir(const std::filesystem::path& path) {
  return RemoveDir(EventLoop::Default(), path);
}

////////////////////////////////////////////////////////////////////////

inline auto CopyFile(EventLoop& loop, const std::filesystem::path& src, const std::filesystem::path& dst, const int& flags) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path src;
    std::filesystem::path dst;
    int flags;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<void>()
      .context(Data{loop, src, dst, flags})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_copyfile(
              loop,
              data.req,
              data.src.string().c_str(),
              data.dst.string().c_str(),
              data.flags,
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  k.Start();
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto CopyFile(const std::filesystem::path& src, const std::filesystem::path& dst, const int& flags) {
  return CopyFile(EventLoop::Default(), src, dst, flags);
}

////////////////////////////////////////////////////////////////////////

inline auto RenameFile(EventLoop& loop, const std::filesystem::path& src, const std::filesystem::path& dst) {
  struct Data {
    EventLoop& loop;
    std::filesystem::path src;
    std::filesystem::path dst;

    Request req;
    void* k = nullptr;

    EventLoop::Callback start;
  };

  return Eventual<void>()
      .context(Data{loop, src, dst})
      .start([](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        data.k = &k;
        data.req->data = &data;

        data.start = [&data](EventLoop& loop) {
          auto error = uv_fs_rename(
              loop,
              data.req,
              data.src.string().c_str(),
              data.dst.string().c_str(),
              [](uv_fs_t* req) {
                auto& data = *static_cast<Data*>(req->data);
                auto& k = *static_cast<K*>(data.k);
                if (req->result == 0) {
                  k.Start();
                } else {
                  k.Fail(uv_strerror(req->result));
                }
              });

          if (error) {
            static_cast<K*>(data.k)->Fail(uv_strerror(error));
          }
        };

        data.loop.Invoke(&data.start);
      });
}

////////////////////////////////////////////////////////////////////////

inline auto RenameFile(const std::filesystem::path& src, const std::filesystem::path& dst) {
  return RenameFile(EventLoop::Default(), src, dst);
}

////////////////////////////////////////////////////////////////////////

} // namespace filesystem
} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
