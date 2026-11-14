/* Repack an MPK1 archive (tools/pack_race_assets.py) as MPK2: entries laid out
 * in a given order and LZ4-compressed where that saves space.
 *
 * usage: pakpack [--stored] [--align N] [--threads N] in.pak|- order.txt out.pak [name=file | @list ...]
 * name=file adds a loose file as entry name (e.g. a stream the game reads whole);
 * @list reads name=file lines from list. in.pak "-" starts from no entries.
 * order.txt lists entry names, one per line, in the order their data is
 * written; entries it doesn't name follow in index order. Data the game
 * reads together then sits together, so one SD request covers several entries.
 * --stored keeps every entry raw (files the game streams, already compressed
 * or compressed inside); --align puts each entry at an N-byte offset (512:
 * SD sectors), 32 by default. --threads sets the compression threads (default:
 * online CPUs); the output is the same for any count.
 *
 * usage: pakpack --gmv in.gmv out.gmv
 * Rewrites a GMV1 movie (tools/cook_menu_movies.py: raw ETC1 frames) as GMV2,
 * the same frames as LZ4 bands (below). Lossless.
 *
 * MPK2: "MPK2", u32 count, u32 namesBytes; count x {u32 name, u32 offset,
 * u32 size, u32 stored} sorted by name; the names; the data.
 * stored == size: raw. stored < size: one LZ4 block (lz4 block format) that
 * decodes to size bytes. Lossless: the game gets the same bytes either way.
 * Bit 31 of stored marks entries order.txt names (read in that order by the
 * game): the reader reads further ahead among them.
 *
 * GMV2 (src/menu_movie.cpp): a 64-byte header, "GMV2", u32 2, u16 width,
 * u16 height, u32 frames, u32 fps numerator, u32 fps denominator, u32 raw
 * frame bytes, u32 index offset (64), u16 band tile rows, u16 bands, u32 data
 * offset; zeros. The index: per frame u32 offset, then u16 stored size per
 * band. Each frame starts on a 512-byte boundary, its bands back to back:
 * an LZ4 block, or raw when its stored size equals its raw size. A band is
 * a run of 8-pixel tile rows, so a crop reads and decodes only its bands.
 * Build: gcc -O2 -pthread -o pakpack pakpack.c
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct {uint32_t name, offset, size, stored, rank; uint8_t *data; const char *source;} Entry;

static uint32_t u32(const uint8_t *p) {return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;}
static void put32(uint8_t *p, uint32_t v) {p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;}

/* LZ4 block compressor: hash chains, lazy matching one step ahead. */
#define HASH_BITS 16
#define WINDOW 65535
#define MIN_MATCH 4
#define CHAIN 96
static uint32_t hash4(const uint8_t *p) {return (u32(p) * 2654435761u) >> (32 - HASH_BITS);}

static size_t emit(uint8_t *o, const uint8_t *lit, size_t litLen, size_t matchLen, size_t dist) {
    uint8_t *start = o, *token = o++;
    size_t ml = matchLen ? matchLen - MIN_MATCH : 0;
    *token = (uint8_t)((litLen < 15 ? litLen : 15) << 4 | (ml < 15 ? ml : 15));
    if (litLen >= 15) {size_t n = litLen - 15; while (n >= 255) {*o++ = 255; n -= 255;} *o++ = (uint8_t)n;}
    memcpy(o, lit, litLen); o += litLen;
    if (matchLen) {
        *o++ = (uint8_t)dist; *o++ = (uint8_t)(dist >> 8);
        if (ml >= 15) {size_t n = ml - 15; while (n >= 255) {*o++ = 255; n -= 255;} *o++ = (uint8_t)n;}
    }
    return (size_t)(o - start);
}

static void best(const uint8_t *in, size_t n, size_t i, const int32_t *head, const int32_t *prev, size_t limit, size_t *len, size_t *dist) {
    *len = 0; *dist = 0;
    if (i + MIN_MATCH > limit) return;
    int chain = CHAIN;
    for (int32_t c = head[hash4(in + i)]; c >= 0 && i - (size_t)c <= WINDOW && chain--; c = prev[c]) {
        size_t l = 0, max = limit - i;
        if (in[c + *len] != in[i + *len]) continue;
        while (l < max && in[c + l] == in[i + l]) l++;
        if (l >= MIN_MATCH && l > *len) {*len = l; *dist = i - (size_t)c; if (l == max) break;}
    }
    (void)n;
}

/* head: scratch of 1 << HASH_BITS, one per thread. */
static size_t lz4(const uint8_t *in, size_t n, uint8_t *out, int32_t *head) {
    int32_t *prev = malloc((n ? n : 1) * sizeof *prev);
    for (size_t i = 0; i < (1u << HASH_BITS); i++) head[i] = -1;
    size_t o = 0, anchor = 0, i = 0;
    /* The last match must start 12 bytes before the end; the last 5 are literals. */
    const size_t matchEnd = n > 5 ? n - 5 : 0, startLimit = n > 12 ? n - 12 : 0;
    #define INSERT(k) do {if ((k) + MIN_MATCH <= n) {uint32_t h_ = hash4(in + (k)); prev[k] = head[h_]; head[h_] = (int32_t)(k);}} while (0)
    while (i < startLimit) {
        size_t len, dist; best(in, n, i, head, prev, matchEnd, &len, &dist);
        if (len >= MIN_MATCH) {
            size_t len2, dist2; INSERT(i);
            best(in, n, i + 1, head, prev, matchEnd, &len2, &dist2);
            if (len2 > len + 1) {i++; continue;}
            o += emit(out + o, in + anchor, i - anchor, len, dist);
            for (size_t k = i + 1; k < i + len; k++) INSERT(k);
            i += len; anchor = i;
        } else {INSERT(i); i++;}
    }
    o += emit(out + o, in + anchor, n - anchor, 0, 0);
    free(prev);
    return o;
}

/* Reference decoder, to check every block before it's written. */
static int unlz4(const uint8_t *in, size_t n, uint8_t *out, size_t size) {
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t token = in[i++]; size_t lit = token >> 4;
        if (lit == 15) {uint8_t b; do {if (i >= n) return 0; b = in[i++]; lit += b;} while (b == 255);}
        if (lit > n - i || lit > size - o) return 0;
        memcpy(out + o, in + i, lit); i += lit; o += lit;
        if (i == n) break;
        if (n - i < 2) return 0;
        size_t dist = in[i] | in[i + 1] << 8; i += 2;
        size_t ml = (token & 15);
        if (ml == 15) {uint8_t b; do {if (i >= n) return 0; b = in[i++]; ml += b;} while (b == 255);}
        ml += MIN_MATCH;
        if (!dist || dist > o || ml > size - o) return 0;
        for (size_t k = 0; k < ml; k++, o++) out[o] = out[o - dist];
    }
    return o == size;
}

static Entry *entries; static char *names;
static int byRank(const void *a, const void *b) {
    const Entry *x = a, *y = b; return x->rank < y->rank ? -1 : x->rank > y->rank;
}
static int byName(const void *a, const void *b) {
    const Entry *x = a, *y = b; return strcmp(names + x->name, names + y->name);
}
static int findName(uint32_t count, const char *key) {
    int lo = 0, hi = (int)count - 1;
    while (lo <= hi) {int mid = (lo + hi) / 2, c = strcmp(names + entries[mid].name, key); if (!c) return mid; if (c < 0) lo = mid + 1; else hi = mid - 1;}
    return -1;
}


static int addLoose(uint32_t *count, uint32_t *namesBytes, const char *spec) {
    const char *eq = strchr(spec, '='); if (!eq) {fprintf(stderr, "expected name=file: %s\n", spec); return 2;}
    size_t len = (size_t)(eq - spec);
    FILE *f = fopen(eq + 1, "rb"); if (!f) {perror(eq + 1); return 1;}
    fseek(f, 0, SEEK_END); long bytes = ftell(f); fclose(f);
    names = realloc(names, *namesBytes + len + 1); memcpy(names + *namesBytes, spec, len); names[*namesBytes + len] = 0;
    entries = realloc(entries, (*count + 1) * sizeof *entries);
    Entry e = {*namesBytes, 0, (uint32_t)bytes, 0, 0xffffffffu, NULL, strdup(eq + 1)};
    *namesBytes += (uint32_t)len + 1;
    entries[(*count)++] = e;
    return 0;
}

/* An entry's bytes, from its loose file or the input pack. */
static uint8_t *readData(const Entry *e, FILE *in) {
    uint8_t *data = malloc(e->size ? e->size : 1);
    if (e->source) {
        FILE *f = fopen(e->source, "rb");
        if (!f || fread(data, 1, e->size, f) != e->size) {fprintf(stderr, "read %s\n", e->source); exit(1);}
        fclose(f);
    } else if (fseek(in, (long)e->offset, SEEK_SET) || fread(data, 1, e->size, in) != e->size) {fprintf(stderr, "read %s\n", names + e->name); exit(1);}
    return data;
}

/* Compression workers take entries in index order, each reading the input
 * pack through its own handle and holding one entry's buffers at a time. */
static struct {const char *pack; uint32_t next, count; pthread_mutex_t lock;} jobs = {NULL, 0, 0, PTHREAD_MUTEX_INITIALIZER};
static void *compressEntries(void *arg) {
    (void)arg;
    FILE *in = jobs.pack ? fopen(jobs.pack, "rb") : NULL;
    if (jobs.pack && !in) {perror(jobs.pack); exit(1);}
    int32_t *head = malloc(sizeof *head << HASH_BITS);
    for (;;) {
        pthread_mutex_lock(&jobs.lock); uint32_t i = jobs.next++; pthread_mutex_unlock(&jobs.lock);
        if (i >= jobs.count) break;
        Entry *e = &entries[i];
        if (i % 2000 == 0) fprintf(stderr, "%u/%u\r", i, jobs.count);
        if (e->size < 64) continue;
        uint8_t *data = readData(e, in);
        uint8_t *block = malloc(e->size + e->size / 255 + 16);
        size_t n = lz4(data, e->size, block, head);
        if (n < e->size && n + 512 <= e->size && n * 10 <= (size_t)e->size * 9) {
            uint8_t *check = malloc(e->size);
            if (!unlz4(block, n, check, e->size) || memcmp(check, data, e->size)) {fprintf(stderr, "lz4 check failed: %s\n", names + e->name); exit(1);}
            free(check); e->data = block; e->stored = (uint32_t)n;
        } else free(block);
        free(data);
    }
    free(head); if (in) fclose(in);
    return NULL;
}

static int cpus(void) {
#ifdef _WIN32
    SYSTEM_INFO s; GetSystemInfo(&s); return (int)s.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1;
#endif
}

#define GMV_BAND_ROWS 4
static int gmv(const char *inPath, const char *outPath) {
    FILE *in = fopen(inPath, "rb"); if (!in) {perror(inPath); return 1;}
    uint8_t header[64];
    if (fread(header, 1, 64, in) != 64 || memcmp(header, "GMV1", 4) || u32(header + 4) != 1) {fprintf(stderr, "not GMV1: %s\n", inPath); return 1;}
    const uint32_t width = header[8] | header[9] << 8, height = header[10] | header[11] << 8, count = u32(header + 12);
    const uint32_t frameBytes = u32(header + 24), offset = u32(header + 28);
    const uint32_t rows = height / 8, rowBytes = width / 8 * 32, bands = (rows + GMV_BAND_ROWS - 1) / GMV_BAND_ROWS;
    if (!width || !height || width % 8 || height % 8 || frameBytes != width * height / 2 || offset != 64 || GMV_BAND_ROWS * rowBytes > 65535) {fprintf(stderr, "bad GMV1 layout\n"); return 1;}
    const uint32_t record = 4 + 2 * bands, dataOffset = (64 + count * record + 511u) & ~511u;
    uint8_t *index = calloc(count, record), *frame = malloc(frameBytes), *block = malloc(frameBytes + frameBytes / 255 + 16 * bands);
    uint8_t *check = malloc(GMV_BAND_ROWS * rowBytes), zero[512] = {0};
    int32_t *head = malloc(sizeof *head << HASH_BITS);
    FILE *out = fopen(outPath, "wb"); if (!out) {perror(outPath); return 1;}
    if (fseek(out, (long)dataOffset, SEEK_SET)) return 1;
    uint64_t at = dataOffset, stored = 0;
    for (uint32_t f = 0; f < count; f++) {
        if (fread(frame, 1, frameBytes, in) != frameBytes) {fprintf(stderr, "short frame %u\n", f); return 1;}
        if (at >= (1ull << 31)) {fprintf(stderr, "over 2 GiB\n"); return 1;}
        put32(index + (size_t)f * record, (uint32_t)at);
        uint32_t written = 0;
        for (uint32_t b = 0; b < bands; b++) {
            const uint32_t top = b * GMV_BAND_ROWS, n = ((top + GMV_BAND_ROWS < rows ? top + GMV_BAND_ROWS : rows) - top) * rowBytes;
            const uint8_t *raw = frame + (size_t)top * rowBytes;
            size_t packed = lz4(raw, n, block, head);
            /* LZ4 only where it saves a tenth; every block is checked. */
            const int keep = packed * 10 <= (size_t)n * 9;
            if (keep && (!unlz4(block, packed, check, n) || memcmp(check, raw, n))) {fprintf(stderr, "lz4 check failed: frame %u band %u\n", f, b); return 1;}
            const uint32_t size = keep ? (uint32_t)packed : n;
            fwrite(keep ? block : raw, 1, size, out);
            index[(size_t)f * record + 4 + b * 2] = (uint8_t)size; index[(size_t)f * record + 5 + b * 2] = (uint8_t)(size >> 8);
            written += size;
        }
        const uint32_t pad = ((written + 511u) & ~511u) - written;
        fwrite(zero, 1, pad, out);
        at += written + pad; stored += written;
    }
    fclose(in);
    memcpy(header, "GMV2", 4); put32(header + 4, 2);
    memset(header + 32, 0, 32);
    header[32] = GMV_BAND_ROWS; header[33] = 0; header[34] = (uint8_t)bands; header[35] = (uint8_t)(bands >> 8);
    put32(header + 36, dataOffset);
    fseek(out, 0, SEEK_SET); fwrite(header, 1, 64, out); fwrite(index, record, count, out);
    const uint32_t gap = dataOffset - 64 - count * record; for (uint32_t i = 0; i < gap; i++) fputc(0, out);
    fclose(out);
    fprintf(stderr, "%s: %u frames, %u bands: %.1f MB -> %.1f MB\n", outPath, count, bands,
        (double)frameBytes * count / 1048576.0, (double)stored / 1048576.0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--gmv")) return gmv(argv[2], argv[3]);
    int stored = 0, threads = cpus(); uint32_t align = 32;
    while (argc > 1 && !strncmp(argv[1], "--", 2)) {
        if (!strcmp(argv[1], "--stored")) {stored = 1; argv++; argc--;}
        else if (!strcmp(argv[1], "--align") && argc > 2) {align = (uint32_t)atoi(argv[2]); argv += 2; argc -= 2;}
        else if (!strcmp(argv[1], "--threads") && argc > 2) {threads = atoi(argv[2]); argv += 2; argc -= 2;}
        else break;
    }
    if (argc < 4 || align < 32 || (align & (align - 1)) || threads < 1) {fprintf(stderr, "usage: pakpack [--stored] [--align N] [--threads N] in.pak|- order.txt out.pak [name=file | @list ...]\n       pakpack --gmv in.gmv out.gmv\n"); return 2;}
    FILE *in = NULL;
    uint8_t header[12]; uint32_t count = 0, namesBytes = 0;
    if (strcmp(argv[1], "-")) {
        in = fopen(argv[1], "rb"); if (!in) {perror(argv[1]); return 1;}
        if (fread(header, 1, 12, in) != 12 || memcmp(header, "MPK1", 4)) {fprintf(stderr, "not MPK1\n"); return 1;}
        count = u32(header + 4); namesBytes = u32(header + 8);
        uint8_t *table = malloc((size_t)count * 12); names = malloc(namesBytes);
        entries = calloc(count ? count : 1, sizeof *entries);
        if (fread(table, 12, count, in) != count || fread(names, 1, namesBytes, in) != namesBytes) {fprintf(stderr, "short index\n"); return 1;}
        for (uint32_t i = 0; i < count; i++) {
            entries[i].name = u32(table + i * 12); entries[i].offset = u32(table + i * 12 + 4); entries[i].size = u32(table + i * 12 + 8);
            entries[i].rank = 0xffffffffu;
        }
        free(table);
    }
    /* Loose files join the index (kept sorted by name for the game's lookup). */
    for (int x = 4; x < argc; x++) {
        if (argv[x][0] == '@') {
            FILE *list = fopen(argv[x] + 1, "r"); if (!list) {perror(argv[x] + 1); return 1;}
            char line[2048];
            while (fgets(line, sizeof line, list)) {
                line[strcspn(line, "\r\n")] = 0; if (!line[0]) continue;
                int rc = addLoose(&count, &namesBytes, line); if (rc) return rc;
            }
            fclose(list);
        } else {int rc = addLoose(&count, &namesBytes, argv[x]); if (rc) return rc;}
    }
    /* One sort after all loose files (a sort per file took 9 s for race.pak). */
    if (argc > 4) {
        qsort(entries, count, sizeof *entries, byName);
        for (uint32_t i = 1; i < count; i++) if (!strcmp(names + entries[i - 1].name, names + entries[i].name)) {fprintf(stderr, "already packed: %s\n", names + entries[i].name); return 1;}
    }
    if (!count) {fprintf(stderr, "no entries\n"); return 1;}
    put32(header + 4, count); put32(header + 8, namesBytes);
    /* Order: named entries first, in file order; the rest keep index order. */
    FILE *order = fopen(argv[2], "r"); if (!order) {perror(argv[2]); return 1;}
    char line[1024]; uint32_t ranked = 0;
    while (fgets(line, sizeof line, order)) {
        line[strcspn(line, "\r\n")] = 0; if (!line[0]) continue;
        int k = findName(count, line); if (k >= 0 && entries[k].rank == 0xffffffffu) entries[k].rank = ranked++;
    }
    fclose(order);
    for (uint32_t i = 0; i < count; i++) if (entries[i].rank == 0xffffffffu) entries[i].rank = ranked + i;
    const uint32_t traced = ranked;
    /* Compress each entry on the worker threads; keep the LZ4 block only when
     * it saves 10% and a sector's worth. Raw entries are read again when
     * written, so only the compressed blocks stay in memory. */
    uint64_t raw = 0, packed = 0; uint32_t compressed = 0;
    for (uint32_t i = 0; i < count; i++) entries[i].stored = entries[i].size;
    if (!stored) {
        jobs.pack = in ? argv[1] : NULL; jobs.count = count;
        if ((uint32_t)threads > count) threads = (int)count;
        pthread_t *pool = malloc((size_t)threads * sizeof *pool);
        for (int t = 0; t < threads; t++) if (pthread_create(&pool[t], NULL, compressEntries, NULL)) {fprintf(stderr, "pthread_create failed\n"); return 1;}
        for (int t = 0; t < threads; t++) pthread_join(pool[t], NULL);
        free(pool);
    }
    for (uint32_t i = 0; i < count; i++) {raw += entries[i].size; packed += entries[i].stored; compressed += entries[i].data != NULL;}
    /* Lay out data by rank. */
    Entry *byOrder = malloc((size_t)count * sizeof *byOrder); memcpy(byOrder, entries, (size_t)count * sizeof *byOrder);
    qsort(byOrder, count, sizeof *byOrder, byRank);
    uint32_t headerBytes = 12 + 16 * count + namesBytes;
    uint64_t at = (headerBytes + align - 1) & ~(uint64_t)(align - 1);
    FILE *out = fopen(argv[3], "wb"); if (!out) {perror(argv[3]); return 1;}
    uint8_t *zero = calloc(align, 1);
    if (fseek(out, (long)at, SEEK_SET)) return 1;
    for (uint32_t i = 0; i < count; i++) {
        Entry *e = &byOrder[i];
        if (at + e->stored >= (1ull << 31)) {fprintf(stderr, "over 2 GiB\n"); return 1;}
        entries[findName(count, names + e->name)].offset = (uint32_t)at;
        uint8_t *data = e->data ? e->data : readData(e, in);
        fwrite(data, 1, e->stored, out); free(data);
        uint32_t pad = (uint32_t)(((e->stored + align - 1) & ~(uint64_t)(align - 1)) - e->stored); fwrite(zero, 1, pad, out);
        at += e->stored + pad;
    }
    if (in) fclose(in);
    /* Index in name order, as in MPK1. */
    fseek(out, 0, SEEK_SET);
    memcpy(header, "MPK2", 4); fwrite(header, 1, 12, out);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t rec[16]; put32(rec, entries[i].name); put32(rec + 4, entries[i].offset);
        put32(rec + 8, entries[i].size); put32(rec + 12, entries[i].stored | (entries[i].rank < traced ? 0x80000000u : 0));
        fwrite(rec, 1, 16, out);
    }
    fwrite(names, 1, namesBytes, out);
    fclose(out);
    fprintf(stderr, "%u entries, %u compressed: %.1f MB -> %.1f MB of data, file %.1f MB\n",
        count, compressed, raw / 1048576.0, packed / 1048576.0, at / 1048576.0);
    return 0;
}
