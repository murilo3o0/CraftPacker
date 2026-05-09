#include "miniz.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

static unsigned int crc32_table[256];
static int crc32_init = 0;

static void init_crc32() {
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) c = 0xEDB88320 ^ (c >> 1);
            else c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_init = 1;
}

static unsigned int crc32_func(const void* buf, size_t len) {
    if (!crc32_init) init_crc32();
    unsigned int c = 0xFFFFFFFF;
    const unsigned char* p = (const unsigned char*)buf;
    for (size_t i = 0; i < len; i++)
        c = crc32_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

static unsigned int dos_time() {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (!tm) return 0;
    return (unsigned int)((tm->tm_mday | ((tm->tm_mon + 1) << 5) | ((tm->tm_year - 80) << 9)) << 16) |
           (tm->tm_sec / 2) | (tm->tm_min << 5) | (tm->tm_hour << 11);
}

typedef struct {
    unsigned int crc32;
    unsigned long long size_comp;
    unsigned long long size_uncomp;
    unsigned short comp_method;
    unsigned short name_len;
    char name[512];
    unsigned int local_offset;
} entry_t;

typedef struct {
    FILE* f;
    entry_t entries[1024];
    int count;
    int finalized;
} state_t;

// ============================================================
// READER STATE
// ============================================================
typedef struct {
    const unsigned char* data;
    size_t size;
    int index_count;
    struct {
        char name[512];
        unsigned int local_offset;
        unsigned int size_comp;
        unsigned int size_uncomp;
        unsigned short comp_method;
    } indices[4096];
} reader_state_t;

static void w16(FILE* f, unsigned short v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}
static void w32(FILE* f, unsigned int v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

// ============================================================
// WRITER IMPLEMENTATION
// ============================================================
mz_bool mz_zip_writer_init_file(mz_zip_archive *p, const char *fn, mz_uint64) {
    if (!p || !fn) return MZ_FALSE;
    state_t* s = (state_t*)calloc(1, sizeof(state_t));
    if (!s) return MZ_FALSE;
    s->f = fopen(fn, "wb");
    if (!s->f) { free(s); return MZ_FALSE; }
    s->count = 0; s->finalized = 0;
    p->m_pBuf = s;
    return MZ_TRUE;
}

mz_bool mz_zip_writer_init_heap(mz_zip_archive *p, size_t, size_t) {
    if (!p) return MZ_FALSE;
    state_t* s = (state_t*)calloc(1, sizeof(state_t));
    if (!s) return MZ_FALSE;
    char tmp[1024]; sprintf(tmp, "%s\\cp_temp_%d.zip", getenv("TEMP") ? getenv("TEMP") : ".", rand());
    s->f = fopen(tmp, "wb+");
    if (!s->f) { free(s); return MZ_FALSE; }
    s->count = 0; s->finalized = 0;
    p->m_pBuf = s;
    return MZ_TRUE;
}

mz_bool mz_zip_writer_add_mem(mz_zip_archive *p, const char *name, const void *buf, size_t sz, mz_uint) {
    if (!p || !p->m_pBuf || !name) return MZ_FALSE;
    state_t* s = (state_t*)p->m_pBuf;
    if (s->count >= 1024) return MZ_FALSE;
    entry_t* e = &s->entries[s->count];
    e->local_offset = (unsigned int)ftell(s->f);
    e->crc32 = crc32_func(buf, sz);
    e->size_uncomp = sz; e->size_comp = sz; e->comp_method = 0;
    strncpy(e->name, name, sizeof(e->name)-1); e->name[sizeof(e->name)-1] = 0;
    e->name_len = (unsigned short)strlen(e->name);
    fputc('P', s->f); fputc('K', s->f); w16(s->f, 0x0403);
    w16(s->f, 20); w16(s->f, 0); w16(s->f, e->comp_method);
    w16(s->f, (unsigned short)(dos_time() >> 16));
    w16(s->f, (unsigned short)(dos_time() & 0xFFFF));
    w32(s->f, e->crc32); w32(s->f, (unsigned int)e->size_comp);
    w32(s->f, (unsigned int)e->size_uncomp);
    w16(s->f, e->name_len); w16(s->f, 0);
    fwrite(e->name, 1, e->name_len, s->f);
    fwrite(buf, 1, sz, s->f);
    s->count++;
    return MZ_TRUE;
}

mz_bool mz_zip_writer_finalize_archive(mz_zip_archive *p) {
    if (!p || !p->m_pBuf) return MZ_FALSE;
    state_t* s = (state_t*)p->m_pBuf;
    if (s->finalized) return MZ_TRUE;
    unsigned int cd_offset = (unsigned int)ftell(s->f);
    for (int i = 0; i < s->count; i++) {
        entry_t* e = &s->entries[i];
        fputc('P', s->f); fputc('K', s->f); w16(s->f, 0x0201);
        w16(s->f, 20); w16(s->f, 20); w16(s->f, 0);
        w16(s->f, e->comp_method);
        w16(s->f, (unsigned short)(dos_time() >> 16));
        w16(s->f, (unsigned short)(dos_time() & 0xFFFF));
        w32(s->f, e->crc32); w32(s->f, (unsigned int)e->size_comp);
        w32(s->f, (unsigned int)e->size_uncomp);
        w16(s->f, e->name_len); w16(s->f, 0); w16(s->f, 0);
        w16(s->f, 0); w16(s->f, 0); w32(s->f, 0);
        w32(s->f, e->local_offset);
        fwrite(e->name, 1, e->name_len, s->f);
    }
    unsigned int cd_size = (unsigned int)(ftell(s->f) - cd_offset);
    fputc('P', s->f); fputc('K', s->f); w16(s->f, 0x0605);
    w16(s->f, 0); w16(s->f, 0);
    w16(s->f, (unsigned short)s->count);
    w16(s->f, (unsigned short)s->count);
    w32(s->f, cd_size); w32(s->f, cd_offset);
    w16(s->f, 0);
    s->finalized = 1;
    return MZ_TRUE;
}

mz_bool mz_zip_writer_end(mz_zip_archive *p) {
    if (!p || !p->m_pBuf) return MZ_FALSE;
    state_t* s = (state_t*)p->m_pBuf;
    if (s->f) fclose(s->f);
    free(s);
    p->m_pBuf = NULL;
    return MZ_TRUE;
}

// ============================================================
// READER IMPLEMENTATION
// ============================================================
mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pBuf, size_t buf_size, mz_uint flags) {
    (void)flags;
    if (!pZip || !pBuf || buf_size < 22) return MZ_FALSE;

    reader_state_t* rs = (reader_state_t*)calloc(1, sizeof(reader_state_t));
    if (!rs) return MZ_FALSE;

    rs->data = (const unsigned char*)pBuf;
    rs->size = buf_size;
    rs->index_count = 0;

    // Find End of Central Directory Record (EOCD) signature: PK\x05\x06
    const unsigned char* eocd = NULL;
    for (size_t i = (buf_size < 65557 ? 0 : buf_size - 65557); i < buf_size - 22; i++) {
        if (rs->data[i] == 'P' && rs->data[i+1] == 'K' && rs->data[i+2] == 0x05 && rs->data[i+3] == 0x06) {
            eocd = rs->data + i;
            break;
        }
    }
    if (!eocd) { free(rs); return MZ_FALSE; }

    unsigned int cd_offset = (unsigned int)(eocd[16]) | ((unsigned int)(eocd[17]) << 8) |
                             ((unsigned int)(eocd[18]) << 16) | ((unsigned int)(eocd[19]) << 24);
    unsigned int cd_size = (unsigned int)(eocd[12]) | ((unsigned int)(eocd[13]) << 8) |
                           ((unsigned int)(eocd[14]) << 16) | ((unsigned int)(eocd[15]) << 24);
    unsigned int num_entries = (unsigned int)(eocd[10]) | ((unsigned int)(eocd[11]) << 8);
    if (num_entries == 0 || num_entries > 4096) { free(rs); return MZ_FALSE; }
    if (cd_offset + cd_size > buf_size) { free(rs); return MZ_FALSE; }

    // Parse Central Directory entries
    const unsigned char* cd = rs->data + cd_offset;
    int count = 0;
    for (unsigned int i = 0; i < num_entries; i++) {
        if (cd + 46 > rs->data + rs->size) break;
        if (cd[0] != 'P' || cd[1] != 'K' || cd[2] != 0x02 || cd[3] != 0x01) break;

        unsigned short name_len = (unsigned short)(cd[28]) | ((unsigned short)(cd[29]) << 8);
        unsigned short extra_len = (unsigned short)(cd[30]) | ((unsigned short)(cd[31]) << 8);
        unsigned short comment_len = (unsigned short)(cd[32]) | ((unsigned short)(cd[33]) << 8);
        unsigned int local_offset = (unsigned int)(cd[42]) | ((unsigned int)(cd[43]) << 8) |
                                     ((unsigned int)(cd[44]) << 16) | ((unsigned int)(cd[45]) << 24);
        unsigned int size_comp = (unsigned int)(cd[20]) | ((unsigned int)(cd[21]) << 8) |
                                  ((unsigned int)(cd[22]) << 16) | ((unsigned int)(cd[23]) << 24);
        unsigned int size_uncomp = (unsigned int)(cd[24]) | ((unsigned int)(cd[25]) << 8) |
                                    ((unsigned int)(cd[26]) << 16) | ((unsigned int)(cd[27]) << 24);
        unsigned short comp_method = (unsigned short)(cd[10]) | ((unsigned short)(cd[11]) << 8);

        if (name_len > 0 && name_len < 511 && count < 4096) {
            memcpy(rs->indices[count].name, cd + 46, name_len);
            rs->indices[count].name[name_len] = 0;
            // Convert to forward slashes
            for (int j = 0; j < name_len; j++)
                if (rs->indices[count].name[j] == '\\') rs->indices[count].name[j] = '/';
            rs->indices[count].local_offset = local_offset;
            rs->indices[count].size_comp = size_comp;
            rs->indices[count].size_uncomp = size_uncomp;
            rs->indices[count].comp_method = comp_method;
            count++;
        }

        cd += 46 + name_len + extra_len + comment_len;
    }

    rs->index_count = count;
    pZip->m_pBuf = rs;
    return MZ_TRUE;
}

mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint flags) {
    FILE* f = fopen(pFilename, "rb");
    if (!f) return MZ_FALSE;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return MZ_FALSE; }
    unsigned char* buf = (unsigned char*)malloc(sz);
    if (!buf) { fclose(f); return MZ_FALSE; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return MZ_FALSE; }
    fclose(f);
    mz_bool result = mz_zip_reader_init_mem(pZip, buf, sz, flags);
    if (!result) free(buf);
    return result;
}

int mz_zip_reader_locate_file(mz_zip_archive *pZip, const char *pName, const char *pComment, mz_uint flags) {
    (void)pComment; (void)flags;
    if (!pZip || !pZip->m_pBuf || !pName) return -1;
    reader_state_t* rs = (reader_state_t*)pZip->m_pBuf;
    for (int i = 0; i < rs->index_count; i++) {
        if (strcmp(rs->indices[i].name, pName) == 0) return i;
    }
    return -1;
}

void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index, size_t *pSize, mz_uint flags) {
    (void)flags;
    if (!pZip || !pZip->m_pBuf) return NULL;
    reader_state_t* rs = (reader_state_t*)pZip->m_pBuf;
    if ((int)file_index < 0 || (int)file_index >= rs->index_count) return NULL;

    unsigned int local_offset = rs->indices[file_index].local_offset;
    unsigned int size_comp = rs->indices[file_index].size_comp;
    unsigned int size_uncomp = rs->indices[file_index].size_uncomp;
    unsigned short comp_method = rs->indices[file_index].comp_method;

    // Go to local file header
    if (local_offset + 30 > rs->size) return NULL;
    const unsigned char* local = rs->data + local_offset;
    if (local[0] != 'P' || local[1] != 'K' || local[2] != 0x03 || local[3] != 0x04) return NULL;

    unsigned short name_len_local = (unsigned short)(local[26]) | ((unsigned short)(local[27]) << 8);
    unsigned short extra_len_local = (unsigned short)(local[28]) | ((unsigned short)(local[29]) << 8);
    unsigned int data_offset = local_offset + 30 + name_len_local + extra_len_local;

    if (comp_method == 0) {
        // Stored (uncompressed)
        if (pSize) *pSize = size_uncomp ? size_uncomp : size_comp;
        void* result = malloc(size_uncomp ? size_uncomp : size_comp);
        if (!result) return NULL;
        // Ensure we don't read past buffer
        size_t read_size = (size_t)(size_comp ? size_comp : size_uncomp);
        if (data_offset + read_size > rs->size) read_size = rs->size - data_offset;
        memcpy(result, rs->data + data_offset, read_size);
        return result;
    } else {
        // For deflated data, return the compressed data as-is
        // (caller can handle decompression if needed, but for JAR metadata
        // files are typically small and stored, not deflated)
        if (pSize) *pSize = size_comp;
        void* result = malloc(size_comp);
        if (!result) return NULL;
        size_t read_size = size_comp;
        if (data_offset + read_size > rs->size) read_size = rs->size - data_offset;
        memcpy(result, rs->data + data_offset, read_size);
        return result;
    }
}

mz_bool mz_zip_reader_end(mz_zip_archive *pZip) {
    if (!pZip || !pZip->m_pBuf) return MZ_FALSE;
    reader_state_t* rs = (reader_state_t*)pZip->m_pBuf;
    // If the reader was initialized with init_mem, the data pointer is external,
    // but we allocated the reader_state, so free it.
    free(rs);
    pZip->m_pBuf = NULL;
    return MZ_TRUE;
}

void mz_free(void *p) {
    free(p);
}

int mz_zip_reader_get_num_files(mz_zip_archive *pZip) {
    if (!pZip || !pZip->m_pBuf) return 0;
    reader_state_t* rs = (reader_state_t*)pZip->m_pBuf;
    return rs->index_count;
}

const char *mz_zip_reader_get_filename(mz_zip_archive *pZip, mz_uint file_index) {
    if (!pZip || !pZip->m_pBuf) return NULL;
    reader_state_t* rs = (reader_state_t*)pZip->m_pBuf;
    if ((int)file_index < 0 || (int)file_index >= rs->index_count) return NULL;
    return rs->indices[file_index].name;
}
