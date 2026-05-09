#pragma once
// miniz.h - Single-header C library for ZIP creation/reading
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef unsigned char mz_uint8;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned long long mz_uint64;
typedef unsigned int mz_uint;
typedef int mz_bool;

#define MZ_TRUE 1
#define MZ_FALSE 0
#define MZ_BEST_COMPRESSION 9
#define MZ_DEFAULT_COMPRESSION 6

typedef struct {
    void *m_pBuf;
    size_t m_size;
    size_t m_capacity;
    mz_bool m_expandable;
} mz_zip_archive;

mz_bool mz_zip_writer_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint64 size_to_reserve_at_beginning);
mz_bool mz_zip_writer_init_heap(mz_zip_archive *pZip, size_t size_to_reserve_at_beginning, size_t initial_allocation_size);
mz_bool mz_zip_writer_add_mem(mz_zip_archive *pZip, const char *pArchive_name, const void *pBuf, size_t buf_size, mz_uint level);
mz_bool mz_zip_writer_finalize_archive(mz_zip_archive *pZip);
mz_bool mz_zip_writer_end(mz_zip_archive *pZip);

// Reader API
mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pBuf, size_t buf_size, mz_uint flags);
mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint flags);
int mz_zip_reader_locate_file(mz_zip_archive *pZip, const char *pName, const char *pComment, mz_uint flags);
void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index, size_t *pSize, mz_uint flags);
mz_bool mz_zip_reader_end(mz_zip_archive *pZip);
void mz_free(void *p);

// Additional reader API for class collision scanning
int mz_zip_reader_get_num_files(mz_zip_archive *pZip);
const char *mz_zip_reader_get_filename(mz_zip_archive *pZip, mz_uint file_index);

#ifdef __cplusplus
}
#endif