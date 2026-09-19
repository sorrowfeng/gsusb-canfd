/*
 * canfd.h - stable C interface for the gsusb-canfd library.
 *
 * The C++ API lives in <canfd/canfd.hpp>. This header is a thin, language
 * agnostic wrapper (usable from ctypes/cffi, Rust FFI, C#, ...): it never
 * throws, reports errors through return codes and canfd_last_error(), and
 * keeps all objects opaque.
 */

#ifndef CANFD_C_H
#define CANFD_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(CANFD_SHARED)
#define CANFD_API __declspec(dllexport)
#elif defined(CANFD_STATIC)
#define CANFD_API
#else
#define CANFD_API __declspec(dllimport)
#endif
#else
#define CANFD_API __attribute__((visibility("default")))
#endif

#define CANFD_MAX_PAYLOAD 64

/* Return codes. */
#define CANFD_OK 0
#define CANFD_ERROR (-1)
/* canfd_receive() returns CANFD_RECEIVE_FRAME, CANFD_RECEIVE_TIMEOUT or CANFD_ERROR. */
#define CANFD_RECEIVE_FRAME 1
#define CANFD_RECEIVE_TIMEOUT 0

/* Feature bits returned by canfd_feature(). */
#define CANFD_FEATURE_LISTEN_ONLY (1u << 0)
#define CANFD_FEATURE_LOOPBACK (1u << 1)
#define CANFD_FEATURE_ONE_SHOT (1u << 3)
#define CANFD_FEATURE_HW_TIMESTAMP (1u << 4)
#define CANFD_FEATURE_FD (1u << 8)
#define CANFD_FEATURE_BT_CONST_EXT (1u << 10)

typedef struct CanFdAdapterInfo {
  uint16_t vendor_id;
  uint16_t product_id;
  uint8_t bus;
  uint8_t address;
  char name[160];
  char manufacturer[64];
  char product[64];
  char serial[64];
} CanFdAdapterInfo;

typedef struct CanFdBusConfig {
  uint32_t bitrate;
  double sample_point;
  uint32_t data_bitrate;
  double data_sample_point;
  int fd;
  int listen_only;
  int loopback;
  int one_shot;
  int hw_timestamp;
} CanFdBusConfig;

typedef struct CanFdFrame {
  uint32_t id;
  uint8_t size;       /* number of valid bytes in data */
  uint8_t channel;
  uint8_t extended;
  uint8_t fd;
  uint8_t brs;
  uint8_t remote;
  uint8_t error;
  uint8_t echo;
  uint8_t overflow;
  double timestamp;   /* seconds, 0 when unavailable */
  uint8_t data[CANFD_MAX_PAYLOAD];
} CanFdFrame;

typedef struct CanFdHandle CanFdHandle;

/* Called on the receive thread when the bus was started with canfd_start(). */
typedef void (*CanFdReceiveCallback)(const CanFdFrame* frame, void* user);

CANFD_API const char* canfd_version(void);
CANFD_API const char* canfd_last_error(void);

CANFD_API void canfd_bus_config_default(CanFdBusConfig* config);

/* Fill up to `max` adapters, returns the count (or CANFD_ERROR). */
CANFD_API int canfd_scan(CanFdAdapterInfo* out, int max);

CANFD_API CanFdHandle* canfd_open(int index);
CANFD_API CanFdHandle* canfd_open_vid_pid(uint16_t vid, uint16_t pid, int index);
/* vid/pid 0 means "any adapter"; channel selects a CAN channel on the device. */
CANFD_API CanFdHandle* canfd_open_channel(uint16_t vid, uint16_t pid, int index, int channel);

CANFD_API int canfd_configure(CanFdHandle* handle, const CanFdBusConfig* config);
CANFD_API int canfd_send(CanFdHandle* handle, const CanFdFrame* frame);
CANFD_API int canfd_receive(CanFdHandle* handle, CanFdFrame* frame, int timeout_ms);

CANFD_API int canfd_start(CanFdHandle* handle, CanFdReceiveCallback callback, void* user);
CANFD_API void canfd_stop(CanFdHandle* handle);
CANFD_API void canfd_close(CanFdHandle* handle);

CANFD_API int canfd_is_open(CanFdHandle* handle);
CANFD_API int canfd_is_started(CanFdHandle* handle);
CANFD_API int canfd_is_fd(CanFdHandle* handle);
CANFD_API uint32_t canfd_feature(CanFdHandle* handle);
CANFD_API uint32_t canfd_clock_frequency(CanFdHandle* handle);
CANFD_API uint32_t canfd_channel_count(CanFdHandle* handle);
CANFD_API int canfd_channel(CanFdHandle* handle);
CANFD_API int canfd_endpoints(CanFdHandle* handle, int* ep_in, int* ep_out);

#ifdef __cplusplus
}
#endif

#endif /* CANFD_C_H */
