#ifndef OPERATORS_H
#define OPERATORS_H

#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

template <typename T1, typename T2>
std::ostream &operator<<(std::ostream &out, const std::pair<T1, T2> &pair) {
  return out << "[" << pair.first << ", " << pair.second << "]";
}

template <class Tuple, std::size_t N>
struct TuplePrinter {
  static void print(std::ostream &out, const Tuple &t) {
    TuplePrinter<Tuple, N - 1>::print(out, t);
    out << ", " << std::get<N - 1>(t);
  }
};

template <class Tuple>
struct TuplePrinter<Tuple, 1> {
  static void print(std::ostream &out, const Tuple &t) {
    out << std::get<0>(t);
  }
};

template <typename... T>
std::ostream &operator<<(std::ostream &out, const std::tuple<T...> &tuple) {
  out << "[";
  TuplePrinter<decltype(tuple), sizeof...(T)>::print(out, tuple);
  return out << "]";
}

template <typename T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &vec) {
  std::string separator;
  out << "{";
  for (const auto &item : vec) {
    out << separator << item;
    separator = ", ";
  }
  return out << "}";
}

template <>
inline std::ostream &operator<<(std::ostream &out,
                                const std::vector<bool> &vec) {
  out << "{";
  for (const auto &item : vec)
    out << (item ? 1 : 0); // in case std::boolalpha is used
  return out << "}";
}

template <typename T>
std::ostream &operator<<(std::ostream &out, const std::set<T> &set) {
  std::string separator;
  out << "{";
  for (const auto &item : set) {
    out << separator << item;
    separator = ", ";
  }
  return out << "}";
}

template <typename T, std::size_t N>
std::ostream &operator<<(std::ostream &out, const std::array<T, N> &arr) {
  std::string separator;
  out << "{";
  for (const auto &item : arr) {
    out << separator << item;
    separator = ", ";
  }
  return out << "}";
}

template <typename K, typename V>
std::ostream &operator<<(std::ostream &out, const std::map<K, V> &map) {
  std::string separator;
  out << "{";
  for (const auto &[key, value] : map) {
    out << separator << key << ": " << value;
    separator = ", ";
  }
  return out << "}";
}

template <typename T>
struct reversion_wrapper {
  T &iterable;
};

template <typename T>
auto begin(reversion_wrapper<T> w) {
  return std::rbegin(w.iterable);
}

template <typename T>
auto end(reversion_wrapper<T> w) {
  return std::rend(w.iterable);
}

template <typename T>
reversion_wrapper<T> reverse(T &&iterable) {
  return { iterable };
}

static constexpr unsigned long long operator""_KiB(const unsigned long long x) {
  return x << 10;
}

static constexpr unsigned long long operator""_MiB(const unsigned long long x) {
  return x << 20;
}

static constexpr unsigned long long operator""_GiB(const unsigned long long x) {
  return x << 30;
}

static constexpr uint64_t xor_bits(uint64_t value, uint64_t bitmask = ~0ull) {
  // https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
  // Returns the parity of x, i.e. the number of 1-bits in x modulo 2.
  return __builtin_parityll(value & bitmask);
}

static constexpr uint64_t count_one_bits(uint64_t value) {
  // Returns the number of 1-bits in x.
  return __builtin_popcountll(value);
}

static constexpr uint64_t count_trailing_zero_bits(uint64_t value) {
  // Returns the number of trailing 0-bits in x, starting at the least
  // significant bit position. If x is 0, the result is undefined.
  return __builtin_ctzll(value);
}

static constexpr uint64_t count_leading_zero_bits(uint64_t value) {
  // Returns the number of leading 0-bits in x, starting at the most significant
  // bit position. If x is 0, the result is undefined.F
  return __builtin_clzll(value);
}

template <typename T, template <typename> class Container>
bool in(const T &elem, const Container<T> &cont) {
  return std::find(cont.begin(), cont.end(), elem) != cont.end();
}

template <template <typename> class Container>
void split(const std::string &str, Container<std::string> &cont,
           char delim = ' ') {
  std::istringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delim)) {
    cont.push_back(token);
  }
}

template <template <typename> class Container>
void split(const std::string &str, Container<uint64_t> &cont,
           char delim = ' ') {
  std::istringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delim)) {
    cont.push_back(std::stoull(token, nullptr, 0));
  }
}

inline std::string now(const std::string &format = "%F %T",
                       bool put_millis = true) {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto timer = system_clock::to_time_t(now);

  std::ostringstream ss;
  ss << std::put_time(std::localtime(&timer), format.c_str());

  if (put_millis) {
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  }

  return ss.str();
}

template <typename T>
std::array<uint8_t, sizeof(T)> to_byte_array(T value) {
  std::array<uint8_t, sizeof(T)> byte_array;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    byte_array[i] = (value >> (i * 8)) & 0xFF;
  }
  return byte_array;
}

// inspired by Boost's ios_flags_saver
class ios_fmt_saver {
  std::ios &stream;
  std::ios backup;

  ios_fmt_saver &operator=(const ios_fmt_saver &);

public:
  explicit ios_fmt_saver(std::ios &stream) : stream(stream), backup(nullptr) {
    backup.copyfmt(stream);
  }

  ~ios_fmt_saver() {
    this->restore();
  }

  void restore() {
    stream.copyfmt(backup);
  }
};

#endif // OPERATORS_H
