#ifndef CE_DIRENT_H
#define CE_DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
struct dirent { char d_name[260]; };
typedef struct ce_dir DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#ifdef __cplusplus
}
#endif
#endif
