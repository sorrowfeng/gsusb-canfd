#ifndef CANFD_ERROR_HPP
#define CANFD_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace canfd {

/* Machine-readable class of a failure, so a caller can map errors onto its own
   error codes instead of catching everything as CanFdError. */
enum class ErrorCode {
  kNone = 0,
  kNotFound,   /* no matching adapter / interface, or a bad selector */
  kTimeout,    /* an operation ran out of time */
  kBus,        /* USB/transport failure or an unsupported configuration */
  kArgument,   /* invalid argument or state */
};

class CanFdError : public std::runtime_error {
 public:
  explicit CanFdError(const std::string& what, ErrorCode code = ErrorCode::kBus)
      : std::runtime_error(what), code_(code) {}

  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

/* No adapter or interface found, or the selector does not match anything. */
class NotFoundError : public CanFdError {
 public:
  explicit NotFoundError(const std::string& what)
      : CanFdError(what, ErrorCode::kNotFound) {}
};

/* An operation exceeded its timeout. */
class TimeoutError : public CanFdError {
 public:
  explicit TimeoutError(const std::string& what)
      : CanFdError(what, ErrorCode::kTimeout) {}
};

/* USB/transport failure, a control request failure, or an unsupported mode. */
class BusError : public CanFdError {
 public:
  explicit BusError(const std::string& what) : CanFdError(what, ErrorCode::kBus) {}
};

/* Invalid argument, or an operation called in the wrong state. */
class ArgumentError : public CanFdError {
 public:
  explicit ArgumentError(const std::string& what)
      : CanFdError(what, ErrorCode::kArgument) {}
};

}  // namespace canfd

#endif  // CANFD_ERROR_HPP
