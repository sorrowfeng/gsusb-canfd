#ifndef CANFD_C_API_H
#define CANFD_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(CANFD_BUILD_SHARED)
#define CANFD_API __declspec(dllexport)
#else
#define CANFD_API
#endif
#else
#define CANFD_API __attribute__((visibility("default")))
#endif

#define CANFD_MAX_DATA 64
#define CANFD_OK 0
#define CANFD_ERR 1
#define CANFD_TIMEOUT 2

#define CANFD_FLAG_FD 0x01
#define CANFD_FLAG_BRS 0x02
#define CANFD_FLAG_EXTENDED 0x04
#define CANFD_FLAG_REMOTE 0x08
#define CANFD_FLAG_ECHO 0x10
#define CANFD_FLAG_ERROR 0x20

typedef struct CanFdMsg {
  uint32_t id;
  uint32_t timestamp_us;
  uint8_t channel;
  uint8_t dlc;
  uint8_t size;
  uint8_t flags;
  uint8_t data[CANFD_MAX_DATA];
} CanFdMsg;

typedef struct CanFdHandle CanFdHandle;

CANFD_API int canfd_scan(void);
CANFD_API CanFdHandle* canfd_open(int index);
CANFD_API int canfd_configure(CanFdHandle* handle, uint32_t bitrate, double sample_point,
                              uint32_t data_bitrate, double data_sample_point, int fd);
CANFD_API int canfd_transmit(CanFdHandle* handle, const CanFdMsg* msg);
CANFD_API int canfd_receive(CanFdHandle* handle, CanFdMsg* msg, int timeout_ms);
CANFD_API void canfd_close(CanFdHandle* handle);
CANFD_API const char* canfd_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  /* CANFD_C_API_H */
