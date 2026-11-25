#ifndef CE_ERRNO_H
#define CE_ERRNO_H
#ifdef __cplusplus
extern "C" {
#endif
extern int ce_errno;
#define errno ce_errno
#define ENOENT 2
#define EIO 5
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34
#define EINTR 4
#define EAGAIN 11
const char *ce_strerror(int e);
#define strerror ce_strerror
#ifdef __cplusplus
}
#endif
#endif
