#ifndef FIO_VERIFY_H
#define FIO_VERIFY_H

#define FIO_HDR_MAGIC	0xf00baaef

enum {
	VERIFY_NONE = 0,		/* no verification */
	VERIFY_MD5,			/* md5 sum data blocks */
	VERIFY_CRC64,			/* crc64 sum data blocks */
	VERIFY_CRC32,			/* crc32 sum data blocks */
	VERIFY_CRC32C,			/* crc32c sum data blocks */
	VERIFY_CRC32C_INTEL,		/* crc32c sum data blocks with hw */
	VERIFY_CRC16,			/* crc16 sum data blocks */
	VERIFY_CRC7,			/* crc7 sum data blocks */
	VERIFY_SHA256,			/* sha256 sum data blocks */
	VERIFY_SHA512,			/* sha512 sum data blocks */
	VERIFY_META,			/* block_num, timestamp etc. */
	VERIFY_SHA1,			/* sha1 sum data blocks */
	VERIFY_NULL,			/* pretend to verify */
};

/*
 * A header structure associated with each checksummed data block. It is
 * followed by a checksum specific header that contains the verification
 * data.
 */
struct verify_header {
	unsigned int fio_magic;
	unsigned int len;
	unsigned int verify_type;
};

struct vhdr_md5 {
	uint32_t md5_digest[16];
};
struct vhdr_sha512 {
	uint8_t sha512[128];
};
struct vhdr_sha256 {
	uint8_t sha256[64];
};
struct vhdr_sha1 {
	uint32_t sha1[5];
};
struct vhdr_crc64 {
	uint64_t crc64;
};
struct vhdr_crc32 {
	uint32_t crc32;
};
struct vhdr_crc16 {
	uint16_t crc16;
};
struct vhdr_crc7 {
	uint8_t crc7;
};
struct vhdr_meta {
	uint64_t offset;
	unsigned char thread;
	unsigned short numberio;
	unsigned long time_sec;
	unsigned long time_usec;
};

/*
 * Verify helpers
 */
extern void populate_verify_io_u(struct thread_data *, struct io_u *);
extern int __must_check get_next_verify(struct thread_data *td, struct io_u *);
extern int __must_check verify_io_u(struct thread_data *, struct io_u *);
extern int verify_io_u_async(struct thread_data *, struct io_u *);

/*
 * Async verify offload
 */
extern int verify_async_init(struct thread_data *);
extern void verify_async_exit(struct thread_data *);

#endif
