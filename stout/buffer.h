#pragma once

#include <string>
#include <utility> // std::move

#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

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

  Buffer(const Buffer& that) {
    data_ = that.data_;
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());
  }

  Buffer(Buffer&& that) {
    data_ = std::move(that.data_);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    that.buffer_.len = 0;
    that.buffer_.base = nullptr;
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

  Buffer& operator=(const Buffer& that) {
    data_ = that.data_;
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    return *this;
  }

  Buffer& operator=(Buffer&& that) {
    data_ = std::move(that.data_);
    buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

    that.buffer_.len = 0;
    that.buffer_.base = nullptr;

    return *this;
  }

  ~Buffer() {}

  // Extracts the data from the buffer as a universal reference.
  // Empties out the buffer inside.
  std::string&& Extract() noexcept {
    buffer_.len = 0;
    buffer_.base = nullptr;

    return std::move(data_);
  }

  size_t Size() const noexcept {
    return data_.size();
  }

  void Resize(const size_t& size) {
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

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
