/* Media Station data files (BOOT.STM, *.CXT): RIFF-like subfiles, chunks and typed datums.
 *
 * A data file is a sequence of subfiles. Each subfile is
 *   "RIFF" len "IMTS" "rate" 4 <u32 rate> "LIST" len "data" <chunks...>
 * and every chunk is <4-char id, big-endian> <u32le length> <payload>, aligned to even offsets.
 * CXT files start with a 16-byte header ("II\0\0", ?, subfile count, file size) before the
 * first subfile; BOOT.STM has no header. See docs/DATA_FORMAT.md. */
#ifndef MS_DATAFILE_H
#define MS_DATAFILE_H

#include <stddef.h>
#include <stdint.h>

#define MS_TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

enum {
	DATUM_EMPTY = 0x00,
	DATUM_UINT8 = 0x02,
	DATUM_UINT16 = 0x03,
	DATUM_UINT32 = 0x04,
	DATUM_INT8 = 0x05,
	DATUM_INT16 = 0x06,
	DATUM_INT32 = 0x07,
	DATUM_FLOAT = 0x08,
	DATUM_DOUBLE = 0x09,
	DATUM_FILENAME = 0x0a,
	DATUM_RECT = 0x0d,
	DATUM_POINT = 0x0e,
	DATUM_GRAPHIC_SIZE = 0x0f,
	DATUM_GRAPHIC_UNIT = 0x10,
	DATUM_TIME = 0x11,
	DATUM_STRING = 0x12,
	DATUM_VERSION = 0x13,
	DATUM_CHANNEL_IDENT = 0x1b,
	DATUM_POLYGON = 0x1d
};

typedef struct {
	int16_t x, y;
} MsPoint;

typedef struct {
	int16_t x, y, w, h;
} MsRect;

/* A whole data file loaded in memory. */
typedef struct {
	char path[512];
	uint8_t *data;
	size_t size;
} MsFile;

/* A bounded read cursor over a file: a chunk payload or a whole subfile. */
typedef struct {
	const uint8_t *base; /* file data */
	size_t pos;          /* absolute offset in the file */
	size_t end;          /* absolute end of the readable range */
	uint32_t id;         /* chunk id (0 for a plain range) */
	size_t start;        /* absolute start of the payload */
} MsChunk;

typedef struct {
	MsChunk root; /* the RIFF payload */
	uint32_t rate;
	size_t first_chunk; /* offset of the first chunk after "data" */
} MsSubfile;

/* Fatal error: prints the message (printf-style) and exits. Data errors are not recoverable,
 * like in the original player. */
void ms_fatal(const char *fmt, ...);
void ms_warn(const char *fmt, ...);
extern int ms_verbose;

int ms_file_load(MsFile *f, const char *path);
void ms_file_free(MsFile *f);

/* Opens the subfile at offset `pos` of the file; returns the offset just past it. */
size_t ms_subfile_open(MsSubfile *sf, const MsFile *f, size_t pos);
/* Next chunk of the subfile starting at `pos` (aligned to even); 0 at the end. */
int ms_subfile_next_chunk(const MsSubfile *sf, size_t *pos, MsChunk *out);

/* Raw little-endian reads, bounded by the chunk. */
size_t ms_remaining(const MsChunk *c);
uint8_t ms_u8(MsChunk *c);
uint16_t ms_u16(MsChunk *c);
uint32_t ms_u32(MsChunk *c);
uint32_t ms_u32be(MsChunk *c);
int16_t ms_s16(MsChunk *c);
void ms_read(MsChunk *c, void *dst, size_t n);
void ms_skip(MsChunk *c, size_t n);
const uint8_t *ms_ptr(const MsChunk *c);

/* Typed datums: a u16 type then the value. They abort on a type mismatch. */
uint16_t ms_peek_type(const MsChunk *c);
uint8_t ms_t_u8(MsChunk *c);
uint16_t ms_t_u16(MsChunk *c);
uint32_t ms_t_u32(MsChunk *c);
int8_t ms_t_s8(MsChunk *c);
int16_t ms_t_s16(MsChunk *c);
int32_t ms_t_s32(MsChunk *c);
float ms_t_float(MsChunk *c);
double ms_t_double(MsChunk *c);
double ms_t_time(MsChunk *c);
int16_t ms_t_gunit(MsChunk *c);
MsPoint ms_t_point(MsChunk *c);
MsPoint ms_t_gsize(MsChunk *c);
MsRect ms_t_rect(MsChunk *c);
uint32_t ms_t_channel(MsChunk *c);
/* Strings and filenames: returns a malloc'd NUL-terminated copy. */
char *ms_t_string(MsChunk *c);
char *ms_t_filename(MsChunk *c);
void ms_t_version(MsChunk *c, uint16_t *major, uint16_t *minor, uint16_t *patch);

/* "igod" -> static string (for diagnostics; rotates among 8 buffers). */
const char *ms_tag_str(uint32_t tag);

#endif
