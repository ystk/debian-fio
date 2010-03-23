#ifndef FIO_IOLOG_H
#define FIO_IOLOG_H

/*
 * Use for maintaining statistics
 */
struct io_stat {
	unsigned long max_val;
	unsigned long min_val;
	unsigned long samples;

	double mean;
	double S;
};

/*
 * A single data sample
 */
struct io_sample {
	unsigned long time;
	unsigned long val;
	enum fio_ddir ddir;
	unsigned int bs;
};

/*
 * Dynamically growing data sample log
 */
struct io_log {
	unsigned long nr_samples;
	unsigned long max_samples;
	struct io_sample *log;
};

/*
 * When logging io actions, this matches a single sent io_u
 */
struct io_piece {
	union {
		struct rb_node rb_node;
		struct flist_head list;
	};
	union {
		int fileno;
		struct fio_file *file;
	};
	unsigned long long offset;
	unsigned long len;
	enum fio_ddir ddir;
	union {
		unsigned long delay;
		unsigned int file_action;
	};
};

/*
 * Log exports
 */
enum file_log_act {
	FIO_LOG_ADD_FILE,
	FIO_LOG_OPEN_FILE,
	FIO_LOG_CLOSE_FILE,
	FIO_LOG_UNLINK_FILE,
};

extern int __must_check read_iolog_get(struct thread_data *, struct io_u *);
extern void log_io_u(struct thread_data *, struct io_u *);
extern void log_file(struct thread_data *, struct fio_file *, enum file_log_act);
extern int __must_check init_iolog(struct thread_data *td);
extern void log_io_piece(struct thread_data *, struct io_u *);
extern void queue_io_piece(struct thread_data *, struct io_piece *);
extern void prune_io_piece_log(struct thread_data *);
extern void write_iolog_close(struct thread_data *);

/*
 * Logging
 */
extern void add_clat_sample(struct thread_data *, enum fio_ddir, unsigned long,
				unsigned int);
extern void add_slat_sample(struct thread_data *, enum fio_ddir, unsigned long,
				unsigned int);
extern void add_bw_sample(struct thread_data *, enum fio_ddir, unsigned int,
				struct timeval *);
extern void show_run_stats(void);
extern void init_disk_util(struct thread_data *);
extern void update_rusage_stat(struct thread_data *);
extern void update_io_ticks(void);
extern void setup_log(struct io_log **);
extern void finish_log(struct thread_data *, struct io_log *, const char *);
extern void finish_log_named(struct thread_data *, struct io_log *, const char *, const char *);
extern void __finish_log(struct io_log *, const char *);
extern struct io_log *agg_io_log[2];
extern int write_bw_log;
extern void add_agg_sample(unsigned long, enum fio_ddir, unsigned int);

#endif
