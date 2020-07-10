#pragma once

// TODO(benh): Move to stout-stringify.
template <typename T>
std::string stringify(const T& t)
{
  std::ostringstream out;
  out << t;
  if (!out.good()) {
    std::cerr << "Failed to stringify!" << std::endl;
    abort();
  }
  return out.str();
}
