#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fio.h"
#include "verify.h"
#include "parse.h"
#include "lib/fls.h"
#include "options.h"

/*
 * Check if mmap/mmaphuge has a :/foo/bar/file at the end. If so, return that.
 */
static char *get_opt_postfix(const char *str)
{
	char *p = strstr(str, ":");

	if (!p)
		return NULL;

	p++;
	strip_blank_front(&p);
	strip_blank_end(p);
	return strdup(p);
}

static int converthexchartoint(char a)
{
	int base;

	switch(a) {
	case '0'...'9':
		base = '0';
		break;
	case 'A'...'F':
		base = 'A' - 10;
		break;
	case 'a'...'f':
		base = 'a' - 10;
		break;
	default:
		base = 0;
	}
	return (a - base);
}

static int bs_cmp(const void *p1, const void *p2)
{
	const struct bssplit *bsp1 = p1;
	const struct bssplit *bsp2 = p2;

	return bsp1->perc < bsp2->perc;
}

static int bssplit_ddir(struct thread_data *td, int ddir, char *str)
{
	struct bssplit *bssplit;
	unsigned int i, perc, perc_missing;
	unsigned int max_bs, min_bs;
	long long val;
	char *fname;

	td->o.bssplit_nr[ddir] = 4;
	bssplit = malloc(4 * sizeof(struct bssplit));

	i = 0;
	max_bs = 0;
	min_bs = -1;
	while ((fname = strsep(&str, ":")) != NULL) {
		char *perc_str;

		if (!strlen(fname))
			break;

		/*
		 * grow struct buffer, if needed
		 */
		if (i == td->o.bssplit_nr[ddir]) {
			td->o.bssplit_nr[ddir] <<= 1;
			bssplit = realloc(bssplit, td->o.bssplit_nr[ddir]
						  * sizeof(struct bssplit));
		}

		perc_str = strstr(fname, "/");
		if (perc_str) {
			*perc_str = '\0';
			perc_str++;
			perc = atoi(perc_str);
			if (perc > 100)
				perc = 100;
			else if (!perc)
				perc = -1;
		} else
			perc = -1;

		if (str_to_decimal(fname, &val, 1, td)) {
			log_err("fio: bssplit conversion failed\n");
			free(td->o.bssplit);
			return 1;
		}

		if (val > max_bs)
			max_bs = val;
		if (val < min_bs)
			min_bs = val;

		bssplit[i].bs = val;
		bssplit[i].perc = perc;
		i++;
	}

	td->o.bssplit_nr[ddir] = i;

	/*
	 * Now check if the percentages add up, and how much is missing
	 */
	perc = perc_missing = 0;
	for (i = 0; i < td->o.bssplit_nr[ddir]; i++) {
		struct bssplit *bsp = &bssplit[i];

		if (bsp->perc == (unsigned char) -1)
			perc_missing++;
		else
			perc += bsp->perc;
	}

	if (perc > 100) {
		log_err("fio: bssplit percentages add to more than 100%%\n");
		free(bssplit);
		return 1;
	}
	/*
	 * If values didn't have a percentage set, divide the remains between
	 * them.
	 */
	if (perc_missing) {
		for (i = 0; i < td->o.bssplit_nr[ddir]; i++) {
			struct bssplit *bsp = &bssplit[i];

			if (bsp->perc == (unsigned char) -1)
				bsp->perc = (100 - perc) / perc_missing;
		}
	}

	td->o.min_bs[ddir] = min_bs;
	td->o.max_bs[ddir] = max_bs;

	/*
	 * now sort based on percentages, for ease of lookup
	 */
	qsort(bssplit, td->o.bssplit_nr[ddir], sizeof(struct bssplit), bs_cmp);
	td->o.bssplit[ddir] = bssplit;
	return 0;

}

static int str_bssplit_cb(void *data, const char *input)
{
	struct thread_data *td = data;
	char *str, *p, *odir;
	int ret = 0;

	p = str = strdup(input);

	strip_blank_front(&str);
	strip_blank_end(str);

	odir = strchr(str, ',');
	if (odir) {
		ret = bssplit_ddir(td, DDIR_WRITE, odir + 1);
		if (!ret) {
			*odir = '\0';
			ret = bssplit_ddir(td, DDIR_READ, str);
		}
	} else {
		char *op;

		op = strdup(str);

		ret = bssplit_ddir(td, DDIR_READ, str);
		if (!ret)
			ret = bssplit_ddir(td, DDIR_WRITE, op);

		free(op);
	}

	free(p);
	return ret;
}

static int str_rw_cb(void *data, const char *str)
{
	struct thread_data *td = data;
	char *nr = get_opt_postfix(str);

	td->o.ddir_nr = 1;
	if (nr) {
		td->o.ddir_nr = atoi(nr);
		free(nr);
	}

	return 0;
}

static int str_mem_cb(void *data, const char *mem)
{
	struct thread_data *td = data;

	if (td->o.mem_type == MEM_MMAPHUGE || td->o.mem_type == MEM_MMAP) {
		td->mmapfile = get_opt_postfix(mem);
		if (td->o.mem_type == MEM_MMAPHUGE && !td->mmapfile) {
			log_err("fio: mmaphuge:/path/to/file\n");
			return 1;
		}
	}

	return 0;
}

static int str_lockmem_cb(void fio_unused *data, unsigned long *val)
{
	mlock_size = *val;
	return 0;
}

static int str_rwmix_read_cb(void *data, unsigned int *val)
{
	struct thread_data *td = data;

	td->o.rwmix[DDIR_READ] = *val;
	td->o.rwmix[DDIR_WRITE] = 100 - *val;
	return 0;
}

static int str_rwmix_write_cb(void *data, unsigned int *val)
{
	struct thread_data *td = data;

	td->o.rwmix[DDIR_WRITE] = *val;
	td->o.rwmix[DDIR_READ] = 100 - *val;
	return 0;
}

#ifdef FIO_HAVE_IOPRIO
static int str_prioclass_cb(void *data, unsigned int *val)
{
	struct thread_data *td = data;
	unsigned short mask;

	/*
	 * mask off old class bits, str_prio_cb() may have set a default class
	 */
	mask = (1 << IOPRIO_CLASS_SHIFT) - 1;
	td->ioprio &= mask;

	td->ioprio |= *val << IOPRIO_CLASS_SHIFT;
	td->ioprio_set = 1;
	return 0;
}

static int str_prio_cb(void *data, unsigned int *val)
{
	struct thread_data *td = data;

	td->ioprio |= *val;

	/*
	 * If no class is set, assume BE
	 */
	if ((td->ioprio >> IOPRIO_CLASS_SHIFT) == 0)
		td->ioprio |= IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT;

	td->ioprio_set = 1;
	return 0;
}
#endif

static int str_exitall_cb(void)
{
	exitall_on_terminate = 1;
	return 0;
}

#ifdef FIO_HAVE_CPU_AFFINITY
static int str_cpumask_cb(void *data, unsigned int *val)
{
	struct thread_data *td = data;
	unsigned int i;
	long max_cpu;
	int ret;

	ret = fio_cpuset_init(&td->o.cpumask);
	if (ret < 0) {
		log_err("fio: cpuset_init failed\n");
		td_verror(td, ret, "fio_cpuset_init");
		return 1;
	}

	max_cpu = sysconf(_SC_NPROCESSORS_ONLN);

	for (i = 0; i < sizeof(int) * 8; i++) {
		if ((1 << i) & *val) {
			if (i > max_cpu) {
				log_err("fio: CPU %d too large (max=%ld)\n", i,
								max_cpu);
				return 1;
			}
			dprint(FD_PARSE, "set cpu allowed %d\n", i);
			fio_cpu_set(&td->o.cpumask, i);
		}
	}

	td->o.cpumask_set = 1;
	return 0;
}

static int set_cpus_allowed(struct thread_data *td, os_cpu_mask_t *mask,
			    const char *input)
{
	char *cpu, *str, *p;
	long max_cpu;
	int ret = 0;

	ret = fio_cpuset_init(mask);
	if (ret < 0) {
		log_err("fio: cpuset_init failed\n");
		td_verror(td, ret, "fio_cpuset_init");
		return 1;
	}

	p = str = strdup(input);

	strip_blank_front(&str);
	strip_blank_end(str);

	max_cpu = sysconf(_SC_NPROCESSORS_ONLN);

	while ((cpu = strsep(&str, ",")) != NULL) {
		char *str2, *cpu2;
		int icpu, icpu2;

		if (!strlen(cpu))
			break;

		str2 = cpu;
		icpu2 = -1;
		while ((cpu2 = strsep(&str2, "-")) != NULL) {
			if (!strlen(cpu2))
				break;

			icpu2 = atoi(cpu2);
		}

		icpu = atoi(cpu);
		if (icpu2 == -1)
			icpu2 = icpu;
		while (icpu <= icpu2) {
			if (icpu >= FIO_MAX_CPUS) {
				log_err("fio: your OS only supports up to"
					" %d CPUs\n", (int) FIO_MAX_CPUS);
				ret = 1;
				break;
			}
			if (icpu > max_cpu) {
				log_err("fio: CPU %d too large (max=%ld)\n",
							icpu, max_cpu);
				ret = 1;
				break;
			}

			dprint(FD_PARSE, "set cpu allowed %d\n", icpu);
			fio_cpu_set(mask, icpu);
			icpu++;
		}
		if (ret)
			break;
	}

	free(p);
	if (!ret)
		td->o.cpumask_set = 1;
	return ret;
}

static int str_cpus_allowed_cb(void *data, const char *input)
{
	struct thread_data *td = data;
	int ret;

	ret = set_cpus_allowed(td, &td->o.cpumask, input);
	if (!ret)
		td->o.cpumask_set = 1;

	return ret;
}

static int str_verify_cpus_allowed_cb(void *data, const char *input)
{
	struct thread_data *td = data;
	int ret;

	ret = set_cpus_allowed(td, &td->o.verify_cpumask, input);
	if (!ret)
		td->o.verify_cpumask_set = 1;

	return ret;
}
#endif

static int str_fst_cb(void *data, const char *str)
{
	struct thread_data *td = data;
	char *nr = get_opt_postfix(str);

	td->file_service_nr = 1;
	if (nr) {
		td->file_service_nr = atoi(nr);
		free(nr);
	}

	return 0;
}

#ifdef FIO_HAVE_SYNC_FILE_RANGE
static int str_sfr_cb(void *data, const char *str)
{
	struct thread_data *td = data;
	char *nr = get_opt_postfix(str);

	td->sync_file_range_nr = 1;
	if (nr) {
		td->sync_file_range_nr = atoi(nr);
		free(nr);
	}

	return 0;
}
#endif

static int check_dir(struct thread_data *td, char *fname)
{
	char file[PATH_MAX], *dir;
	int elen = 0;

	if (td->o.directory) {
		strcpy(file, td->o.directory);
		strcat(file, "/");
		elen = strlen(file);
	}

	sprintf(file + elen, "%s", fname);
	dir = dirname(file);

#if 0
	{
	struct stat sb;
	/*
	 * We can't do this on FIO_DISKLESSIO engines. The engine isn't loaded
	 * yet, so we can't do this check right here...
	 */
	if (lstat(dir, &sb) < 0) {
		int ret = errno;

		log_err("fio: %s is not a directory\n", dir);
		td_verror(td, ret, "lstat");
		return 1;
	}

	if (!S_ISDIR(sb.st_mode)) {
		log_err("fio: %s is not a directory\n", dir);
		return 1;
	}
	}
#endif

	return 0;
}

/*
 * Return next file in the string. Files are separated with ':'. If the ':'
 * is escaped with a '\', then that ':' is part of the filename and does not
 * indicate a new file.
 */
static char *get_next_file_name(char **ptr)
{
	char *str = *ptr;
	char *p, *start;

	if (!str || !strlen(str))
		return NULL;

	start = str;
	do {
		/*
		 * No colon, we are done
		 */
		p = strchr(str, ':');
		if (!p) {
			*ptr = NULL;
			break;
		}

		/*
		 * We got a colon, but it's the first character. Skip and
		 * continue
		 */
		if (p == start) {
			str = ++start;
			continue;
		}

		if (*(p - 1) != '\\') {
			*p = '\0';
			*ptr = p + 1;
			break;
		}

		memmove(p - 1, p, strlen(p) + 1);
		str = p;
	} while (1);

	return start;
}

static int str_filename_cb(void *data, const char *input)
{
	struct thread_data *td = data;
	char *fname, *str, *p;

	p = str = strdup(input);

	strip_blank_front(&str);
	strip_blank_end(str);

	if (!td->files_index)
		td->o.nr_files = 0;

	while ((fname = get_next_file_name(&str)) != NULL) {
		if (!strlen(fname))
			break;
		if (check_dir(td, fname)) {
			free(p);
			return 1;
		}
		add_file(td, fname);
		td->o.nr_files++;
	}

	free(p);
	return 0;
}

static int str_directory_cb(void *data, const char fio_unused *str)
{
	struct thread_data *td = data;
	struct stat sb;

	if (lstat(td->o.directory, &sb) < 0) {
		int ret = errno;

		log_err("fio: %s is not a directory\n", td->o.directory);
		td_verror(td, ret, "lstat");
		return 1;
	}
	if (!S_ISDIR(sb.st_mode)) {
		log_err("fio: %s is not a directory\n", td->o.directory);
		return 1;
	}

	return 0;
}

static int str_opendir_cb(void *data, const char fio_unused *str)
{
	struct thread_data *td = data;

	if (!td->files_index)
		td->o.nr_files = 0;

	return add_dir_files(td, td->o.opendir);
}

static int str_verify_offset_cb(void *data, unsigned int *off)
{
	struct thread_data *td = data;

	if (*off && *off < sizeof(struct verify_header)) {
		log_err("fio: verify_offset too small\n");
		return 1;
	}

	td->o.verify_offset = *off;
	return 0;
}

static int str_verify_pattern_cb(void *data, const char *input)
{
	struct thread_data *td = data;
	long off;
	int i = 0, j = 0, len, k, base = 10;
	char* loc1, * loc2;

	loc1 = strstr(input, "0x");
	loc2 = strstr(input, "0X");
	if (loc1 || loc2)
		base = 16;
	off = strtol(input, NULL, base);
	if (off != LONG_MAX || errno != ERANGE) {
		while (off) {
			td->o.verify_pattern[i] = off & 0xff;
			off >>= 8;
			i++;
		}
	} else {
		len = strlen(input);
		k = len - 1;
		if (base == 16) {
			if (loc1)
				j = loc1 - input + 2;
			else
				j = loc2 - input + 2;
		} else
			return 1;
		if (len - j < MAX_PATTERN_SIZE * 2) {
			while (k >= j) {
				off = converthexchartoint(input[k--]);
				if (k >= j)
					off += (converthexchartoint(input[k--])
						* 16);
				td->o.verify_pattern[i++] = (char) off;
			}
		}
	}
	td->o.verify_pattern_bytes = i;
	return 0;
}

static int str_lockfile_cb(void *data, const char *str)
{
	struct thread_data *td = data;
	char *nr = get_opt_postfix(str);

	td->o.lockfile_batch = 1;
	if (nr) {
		td->o.lockfile_batch = atoi(nr);
		free(nr);
	}

	return 0;
}

static int str_write_bw_log_cb(void *data, const char *str)
{
	struct thread_data *td = data;

	if (str)
		td->o.bw_log_file = strdup(str);

	td->o.write_bw_log = 1;
	return 0;
}

static int str_write_lat_log_cb(void *data, const char *str)
{
	struct thread_data *td = data;

	if (str)
		td->o.lat_log_file = strdup(str);

	td->o.write_lat_log = 1;
	return 0;
}

static int str_gtod_reduce_cb(void *data, int *il)
{
	struct thread_data *td = data;
	int val = *il;

	td->o.disable_clat = !!val;
	td->o.disable_slat = !!val;
	td->o.disable_bw = !!val;
	if (val)
		td->tv_cache_mask = 63;

	return 0;
}

static int str_gtod_cpu_cb(void *data, int *il)
{
	struct thread_data *td = data;
	int val = *il;

	td->o.gtod_cpu = val;
	td->o.gtod_offload = 1;
	return 0;
}

static int rw_verify(struct fio_option *o, void *data)
{
	struct thread_data *td = data;

	if (read_only && td_write(td)) {
		log_err("fio: job <%s> has write bit set, but fio is in"
			" read-only mode\n", td->o.name);
		return 1;
	}

	return 0;
}

static int gtod_cpu_verify(struct fio_option *o, void *data)
{
#ifndef FIO_HAVE_CPU_AFFINITY
	struct thread_data *td = data;

	if (td->o.gtod_cpu) {
		log_err("fio: platform must support CPU affinity for"
			"gettimeofday() offloading\n");
		return 1;
	}
#endif

	return 0;
}

static int kb_base_verify(struct fio_option *o, void *data)
{
	struct thread_data *td = data;

	if (td->o.kb_base != 1024 && td->o.kb_base != 1000) {
		log_err("fio: kb_base set to nonsensical value: %u\n",
				td->o.kb_base);
		return 1;
	}

	return 0;
}

#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)

/*
 * Map of job/command line options
 */
static struct fio_option options[FIO_MAX_OPTS] = {
	{
		.name	= "description",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(description),
		.help	= "Text job description",
	},
	{
		.name	= "name",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(name),
		.help	= "Name of this job",
	},
	{
		.name	= "directory",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(directory),
		.cb	= str_directory_cb,
		.help	= "Directory to store files in",
	},
	{
		.name	= "filename",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(filename),
		.cb	= str_filename_cb,
		.prio	= -1, /* must come after "directory" */
		.help	= "File(s) to use for the workload",
	},
	{
		.name	= "kb_base",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(kb_base),
		.verify	= kb_base_verify,
		.prio	= 1,
		.def	= "1024",
		.help	= "How many bytes per KB for reporting (1000 or 1024)",
	},
	{
		.name	= "lockfile",
		.type	= FIO_OPT_STR,
		.cb	= str_lockfile_cb,
		.off1	= td_var_offset(file_lock_mode),
		.help	= "Lock file when doing IO to it",
		.parent	= "filename",
		.def	= "none",
		.posval = {
			  { .ival = "none",
			    .oval = FILE_LOCK_NONE,
			    .help = "No file locking",
			  },
			  { .ival = "exclusive",
			    .oval = FILE_LOCK_EXCLUSIVE,
			    .help = "Exclusive file lock",
			  },
			  {
			    .ival = "readwrite",
			    .oval = FILE_LOCK_READWRITE,
			    .help = "Read vs write lock",
			  },
		},
	},
	{
		.name	= "opendir",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(opendir),
		.cb	= str_opendir_cb,
		.help	= "Recursively add files from this directory and down",
	},
	{
		.name	= "rw",
		.alias	= "readwrite",
		.type	= FIO_OPT_STR,
		.cb	= str_rw_cb,
		.off1	= td_var_offset(td_ddir),
		.help	= "IO direction",
		.def	= "read",
		.verify	= rw_verify,
		.posval = {
			  { .ival = "read",
			    .oval = TD_DDIR_READ,
			    .help = "Sequential read",
			  },
			  { .ival = "write",
			    .oval = TD_DDIR_WRITE,
			    .help = "Sequential write",
			  },
			  { .ival = "randread",
			    .oval = TD_DDIR_RANDREAD,
			    .help = "Random read",
			  },
			  { .ival = "randwrite",
			    .oval = TD_DDIR_RANDWRITE,
			    .help = "Random write",
			  },
			  { .ival = "rw",
			    .oval = TD_DDIR_RW,
			    .help = "Sequential read and write mix",
			  },
			  { .ival = "randrw",
			    .oval = TD_DDIR_RANDRW,
			    .help = "Random read and write mix"
			  },
		},
	},
	{
		.name	= "ioengine",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(ioengine),
		.help	= "IO engine to use",
		.def	= "sync",
		.posval	= {
			  { .ival = "sync",
			    .help = "Use read/write",
			  },
			  { .ival = "psync",
			    .help = "Use pread/pwrite",
			  },
			  { .ival = "vsync",
			     .help = "Use readv/writev",
			  },
#ifdef FIO_HAVE_LIBAIO
			  { .ival = "libaio",
			    .help = "Linux native asynchronous IO",
			  },
#endif
#ifdef FIO_HAVE_POSIXAIO
			  { .ival = "posixaio",
			    .help = "POSIX asynchronous IO",
			  },
#endif
#ifdef FIO_HAVE_SOLARISAIO
			  { .ival = "solarisaio",
			    .help = "Solaris native asynchronous IO",
			  },
#endif
			  { .ival = "mmap",
			    .help = "Memory mapped IO",
			  },
#ifdef FIO_HAVE_SPLICE
			  { .ival = "splice",
			    .help = "splice/vmsplice based IO",
			  },
			  { .ival = "netsplice",
			    .help = "splice/vmsplice to/from the network",
			  },
#endif
#ifdef FIO_HAVE_SGIO
			  { .ival = "sg",
			    .help = "SCSI generic v3 IO",
			  },
#endif
			  { .ival = "null",
			    .help = "Testing engine (no data transfer)",
			  },
			  { .ival = "net",
			    .help = "Network IO",
			  },
#ifdef FIO_HAVE_SYSLET
			  { .ival = "syslet-rw",
			    .help = "syslet enabled async pread/pwrite IO",
			  },
#endif
			  { .ival = "cpuio",
			    .help = "CPU cycler burner engine",
			  },
#ifdef FIO_HAVE_GUASI
			  { .ival = "guasi",
			    .help = "GUASI IO engine",
			  },
#endif
			  { .ival = "external",
			    .help = "Load external engine (append name)",
			  },
		},
	},
	{
		.name	= "iodepth",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(iodepth),
		.help	= "Amount of IO buffers to keep in flight",
		.minval = 1,
		.def	= "1",
	},
	{
		.name	= "iodepth_batch",
		.alias	= "iodepth_batch_submit",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(iodepth_batch),
		.help	= "Number of IO buffers to submit in one go",
		.parent	= "iodepth",
		.minval	= 1,
		.def	= "1",
	},
	{
		.name	= "iodepth_batch_complete",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(iodepth_batch_complete),
		.help	= "Number of IO buffers to retrieve in one go",
		.parent	= "iodepth",
		.minval	= 0,
		.def	= "1",
	},
	{
		.name	= "iodepth_low",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(iodepth_low),
		.help	= "Low water mark for queuing depth",
		.parent	= "iodepth",
	},
	{
		.name	= "size",
		.type	= FIO_OPT_STR_VAL,
		.off1	= td_var_offset(size),
		.minval = 1,
		.help	= "Total size of device or files",
	},
	{
		.name	= "fill_device",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(fill_device),
		.help	= "Write until an ENOSPC error occurs",
		.def	= "0",
	},
	{
		.name	= "filesize",
		.type	= FIO_OPT_STR_VAL,
		.off1	= td_var_offset(file_size_low),
		.off2	= td_var_offset(file_size_high),
		.minval = 1,
		.help	= "Size of individual files",
	},
	{
		.name	= "offset",
		.alias	= "fileoffset",
		.type	= FIO_OPT_STR_VAL,
		.off1	= td_var_offset(start_offset),
		.help	= "Start IO from this offset",
		.def	= "0",
	},
	{
		.name	= "bs",
		.alias	= "blocksize",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(bs[DDIR_READ]),
		.off2	= td_var_offset(bs[DDIR_WRITE]),
		.minval = 1,
		.help	= "Block size unit",
		.def	= "4k",
		.parent = "rw",
	},
	{
		.name	= "ba",
		.alias	= "blockalign",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(ba[DDIR_READ]),
		.off2	= td_var_offset(ba[DDIR_WRITE]),
		.minval	= 1,
		.help	= "IO block offset alignment",
		.parent	= "rw",
	},
	{
		.name	= "bsrange",
		.alias	= "blocksize_range",
		.type	= FIO_OPT_RANGE,
		.off1	= td_var_offset(min_bs[DDIR_READ]),
		.off2	= td_var_offset(max_bs[DDIR_READ]),
		.off3	= td_var_offset(min_bs[DDIR_WRITE]),
		.off4	= td_var_offset(max_bs[DDIR_WRITE]),
		.minval = 1,
		.help	= "Set block size range (in more detail than bs)",
		.parent = "rw",
	},
	{
		.name	= "bssplit",
		.type	= FIO_OPT_STR,
		.cb	= str_bssplit_cb,
		.help	= "Set a specific mix of block sizes",
		.parent	= "rw",
	},
	{
		.name	= "bs_unaligned",
		.alias	= "blocksize_unaligned",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(bs_unaligned),
		.help	= "Don't sector align IO buffer sizes",
		.parent = "rw",
	},
	{
		.name	= "randrepeat",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(rand_repeatable),
		.help	= "Use repeatable random IO pattern",
		.def	= "1",
		.parent = "rw",
	},
	{
		.name	= "norandommap",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(norandommap),
		.help	= "Accept potential duplicate random blocks",
		.parent = "rw",
	},
	{
		.name	= "softrandommap",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(softrandommap),
		.help	= "Set norandommap if randommap allocation fails",
		.parent	= "norandommap",
		.def	= "0",
	},
	{
		.name	= "nrfiles",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(nr_files),
		.help	= "Split job workload between this number of files",
		.def	= "1",
	},
	{
		.name	= "openfiles",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(open_files),
		.help	= "Number of files to keep open at the same time",
	},
	{
		.name	= "file_service_type",
		.type	= FIO_OPT_STR,
		.cb	= str_fst_cb,
		.off1	= td_var_offset(file_service_type),
		.help	= "How to select which file to service next",
		.def	= "roundrobin",
		.posval	= {
			  { .ival = "random",
			    .oval = FIO_FSERVICE_RANDOM,
			    .help = "Choose a file at random",
			  },
			  { .ival = "roundrobin",
			    .oval = FIO_FSERVICE_RR,
			    .help = "Round robin select files",
			  },
			  { .ival = "sequential",
			    .oval = FIO_FSERVICE_SEQ,
			    .help = "Finish one file before moving to the next",
			  },
		},
		.parent = "nrfiles",
	},
#ifdef FIO_HAVE_FALLOCATE
	{
		.name	= "fallocate",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(fallocate),
		.help	= "Use fallocate() when laying out files",
		.def	= "1",
	},
#endif
	{
		.name	= "fadvise_hint",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(fadvise_hint),
		.help	= "Use fadvise() to advise the kernel on IO pattern",
		.def	= "1",
	},
	{
		.name	= "fsync",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(fsync_blocks),
		.help	= "Issue fsync for writes every given number of blocks",
		.def	= "0",
	},
	{
		.name	= "fdatasync",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(fdatasync_blocks),
		.help	= "Issue fdatasync for writes every given number of blocks",
		.def	= "0",
	},
#ifdef FIO_HAVE_SYNC_FILE_RANGE
	{
		.name	= "sync_file_range",
		.posval	= {
			  { .ival = "wait_before",
			    .oval = SYNC_FILE_RANGE_WAIT_BEFORE,
			    .help = "SYNC_FILE_RANGE_WAIT_BEFORE",
			    .or	  = 1,
			  },
			  { .ival = "write",
			    .oval = SYNC_FILE_RANGE_WRITE,
			    .help = "SYNC_FILE_RANGE_WRITE",
			    .or	  = 1,
			  },
			  {
			    .ival = "wait_after",
			    .oval = SYNC_FILE_RANGE_WAIT_AFTER,
			    .help = "SYNC_FILE_RANGE_WAIT_AFTER",
			    .or	  = 1,
			  },
		},
		.type	= FIO_OPT_STR_MULTI,
		.cb	= str_sfr_cb,
		.off1	= td_var_offset(sync_file_range),
		.help	= "Use sync_file_range()",
	},
#endif
	{
		.name	= "direct",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(odirect),
		.help	= "Use O_DIRECT IO (negates buffered)",
		.def	= "0",
	},
	{
		.name	= "buffered",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(odirect),
		.neg	= 1,
		.help	= "Use buffered IO (negates direct)",
		.def	= "1",
	},
	{
		.name	= "overwrite",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(overwrite),
		.help	= "When writing, set whether to overwrite current data",
		.def	= "0",
	},
	{
		.name	= "loops",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(loops),
		.help	= "Number of times to run the job",
		.def	= "1",
	},
	{
		.name	= "numjobs",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(numjobs),
		.help	= "Duplicate this job this many times",
		.def	= "1",
	},
	{
		.name	= "startdelay",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(start_delay),
		.help	= "Only start job when this period has passed",
		.def	= "0",
	},
	{
		.name	= "runtime",
		.alias	= "timeout",
		.type	= FIO_OPT_STR_VAL_TIME,
		.off1	= td_var_offset(timeout),
		.help	= "Stop workload when this amount of time has passed",
		.def	= "0",
	},
	{
		.name	= "time_based",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(time_based),
		.help	= "Keep running until runtime/timeout is met",
	},
	{
		.name	= "ramp_time",
		.type	= FIO_OPT_STR_VAL_TIME,
		.off1	= td_var_offset(ramp_time),
		.help	= "Ramp up time before measuring performance",
	},
	{
		.name	= "mem",
		.alias	= "iomem",
		.type	= FIO_OPT_STR,
		.cb	= str_mem_cb,
		.off1	= td_var_offset(mem_type),
		.help	= "Backing type for IO buffers",
		.def	= "malloc",
		.posval	= {
			  { .ival = "malloc",
			    .oval = MEM_MALLOC,
			    .help = "Use malloc(3) for IO buffers",
			  },
			  { .ival = "shm",
			    .oval = MEM_SHM,
			    .help = "Use shared memory segments for IO buffers",
			  },
#ifdef FIO_HAVE_HUGETLB
			  { .ival = "shmhuge",
			    .oval = MEM_SHMHUGE,
			    .help = "Like shm, but use huge pages",
			  },
#endif
			  { .ival = "mmap",
			    .oval = MEM_MMAP,
			    .help = "Use mmap(2) (file or anon) for IO buffers",
			  },
#ifdef FIO_HAVE_HUGETLB
			  { .ival = "mmaphuge",
			    .oval = MEM_MMAPHUGE,
			    .help = "Like mmap, but use huge pages",
			  },
#endif
		  },
	},
	{
		.name	= "iomem_align",
		.alias	= "mem_align",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(mem_align),
		.minval	= 0,
		.help	= "IO memory buffer offset alignment",
		.def	= "0",
		.parent	= "iomem",
	},
	{
		.name	= "verify",
		.type	= FIO_OPT_STR,
		.off1	= td_var_offset(verify),
		.help	= "Verify data written",
		.def	= "0",
		.posval = {
			  { .ival = "0",
			    .oval = VERIFY_NONE,
			    .help = "Don't do IO verification",
			  },
			  { .ival = "md5",
			    .oval = VERIFY_MD5,
			    .help = "Use md5 checksums for verification",
			  },
			  { .ival = "crc64",
			    .oval = VERIFY_CRC64,
			    .help = "Use crc64 checksums for verification",
			  },
			  { .ival = "crc32",
			    .oval = VERIFY_CRC32,
			    .help = "Use crc32 checksums for verification",
			  },
			  { .ival = "crc32c-intel",
			    .oval = VERIFY_CRC32C_INTEL,
			    .help = "Use hw crc32c checksums for verification",
			  },
			  { .ival = "crc32c",
			    .oval = VERIFY_CRC32C,
			    .help = "Use crc32c checksums for verification",
			  },
			  { .ival = "crc16",
			    .oval = VERIFY_CRC16,
			    .help = "Use crc16 checksums for verification",
			  },
			  { .ival = "crc7",
			    .oval = VERIFY_CRC7,
			    .help = "Use crc7 checksums for verification",
			  },
			  { .ival = "sha1",
			    .oval = VERIFY_SHA1,
			    .help = "Use sha1 checksums for verification",
			  },
			  { .ival = "sha256",
			    .oval = VERIFY_SHA256,
			    .help = "Use sha256 checksums for verification",
			  },
			  { .ival = "sha512",
			    .oval = VERIFY_SHA512,
			    .help = "Use sha512 checksums for verification",
			  },
			  { .ival = "meta",
			    .oval = VERIFY_META,
			    .help = "Use io information",
			  },
			  {
			    .ival = "null",
			    .oval = VERIFY_NULL,
			    .help = "Pretend to verify",
			  },
		},
	},
	{
		.name	= "do_verify",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(do_verify),
		.help	= "Run verification stage after write",
		.def	= "1",
		.parent = "verify",
	},
	{
		.name	= "verifysort",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(verifysort),
		.help	= "Sort written verify blocks for read back",
		.def	= "1",
		.parent = "verify",
	},
	{
		.name   = "verify_interval",
		.type   = FIO_OPT_INT,
		.off1   = td_var_offset(verify_interval),
		.minval	= 2 * sizeof(struct verify_header),
		.help   = "Store verify buffer header every N bytes",
		.parent	= "verify",
	},
	{
		.name	= "verify_offset",
		.type	= FIO_OPT_INT,
		.help	= "Offset verify header location by N bytes",
		.def	= "0",
		.cb	= str_verify_offset_cb,
		.parent	= "verify",
	},
	{
		.name	= "verify_pattern",
		.type	= FIO_OPT_STR,
		.cb	= str_verify_pattern_cb,
		.help	= "Fill pattern for IO buffers",
		.parent	= "verify",
	},
	{
		.name	= "verify_fatal",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(verify_fatal),
		.def	= "0",
		.help	= "Exit on a single verify failure, don't continue",
		.parent = "verify",
	},
	{
		.name	= "verify_async",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(verify_async),
		.def	= "0",
		.help	= "Number of async verifier threads to use",
		.parent	= "verify",
	},
#ifdef FIO_HAVE_CPU_AFFINITY
	{
		.name	= "verify_async_cpus",
		.type	= FIO_OPT_STR,
		.cb	= str_verify_cpus_allowed_cb,
		.help	= "Set CPUs allowed for async verify threads",
		.parent	= "verify_async",
	},
#endif
	{
		.name	= "write_iolog",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(write_iolog_file),
		.help	= "Store IO pattern to file",
	},
	{
		.name	= "read_iolog",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(read_iolog_file),
		.help	= "Playback IO pattern from file",
	},
	{
		.name	= "exec_prerun",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(exec_prerun),
		.help	= "Execute this file prior to running job",
	},
	{
		.name	= "exec_postrun",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(exec_postrun),
		.help	= "Execute this file after running job",
	},
#ifdef FIO_HAVE_IOSCHED_SWITCH
	{
		.name	= "ioscheduler",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(ioscheduler),
		.help	= "Use this IO scheduler on the backing device",
	},
#endif
	{
		.name	= "zonesize",
		.type	= FIO_OPT_STR_VAL,
		.off1	= td_var_offset(zone_size),
		.help	= "Give size of an IO zone",
		.def	= "0",
	},
	{
		.name	= "zoneskip",
		.type	= FIO_OPT_STR_VAL,
		.off1	= td_var_offset(zone_skip),
		.help	= "Space between IO zones",
		.def	= "0",
	},
	{
		.name	= "lockmem",
		.type	= FIO_OPT_STR_VAL,
		.cb	= str_lockmem_cb,
		.help	= "Lock down this amount of memory",
		.def	= "0",
	},
	{
		.name	= "rwmixread",
		.type	= FIO_OPT_INT,
		.cb	= str_rwmix_read_cb,
		.maxval	= 100,
		.help	= "Percentage of mixed workload that is reads",
		.def	= "50",
	},
	{
		.name	= "rwmixwrite",
		.type	= FIO_OPT_INT,
		.cb	= str_rwmix_write_cb,
		.maxval	= 100,
		.help	= "Percentage of mixed workload that is writes",
		.def	= "50",
	},
	{
		.name	= "rwmixcycle",
		.type	= FIO_OPT_DEPRECATED,
	},
	{
		.name	= "nice",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(nice),
		.help	= "Set job CPU nice value",
		.minval	= -19,
		.maxval	= 20,
		.def	= "0",
	},
#ifdef FIO_HAVE_IOPRIO
	{
		.name	= "prio",
		.type	= FIO_OPT_INT,
		.cb	= str_prio_cb,
		.help	= "Set job IO priority value",
		.minval	= 0,
		.maxval	= 7,
	},
	{
		.name	= "prioclass",
		.type	= FIO_OPT_INT,
		.cb	= str_prioclass_cb,
		.help	= "Set job IO priority class",
		.minval	= 0,
		.maxval	= 3,
	},
#endif
	{
		.name	= "thinktime",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(thinktime),
		.help	= "Idle time between IO buffers (usec)",
		.def	= "0",
	},
	{
		.name	= "thinktime_spin",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(thinktime_spin),
		.help	= "Start think time by spinning this amount (usec)",
		.def	= "0",
		.parent	= "thinktime",
	},
	{
		.name	= "thinktime_blocks",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(thinktime_blocks),
		.help	= "IO buffer period between 'thinktime'",
		.def	= "1",
		.parent	= "thinktime",
	},
	{
		.name	= "rate",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(rate[0]),
		.off2	= td_var_offset(rate[1]),
		.help	= "Set bandwidth rate",
	},
	{
		.name	= "ratemin",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(ratemin[0]),
		.off2	= td_var_offset(ratemin[1]),
		.help	= "Job must meet this rate or it will be shutdown",
		.parent	= "rate",
	},
	{
		.name	= "rate_iops",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(rate_iops[0]),
		.off2	= td_var_offset(rate_iops[1]),
		.help	= "Limit IO used to this number of IO operations/sec",
	},
	{
		.name	= "rate_iops_min",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(rate_iops_min[0]),
		.off2	= td_var_offset(rate_iops_min[1]),
		.help	= "Job must meet this rate or it will be shutdown",
		.parent	= "rate_iops",
	},
	{
		.name	= "ratecycle",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(ratecycle),
		.help	= "Window average for rate limits (msec)",
		.def	= "1000",
		.parent = "rate",
	},
	{
		.name	= "invalidate",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(invalidate_cache),
		.help	= "Invalidate buffer/page cache prior to running job",
		.def	= "1",
	},
	{
		.name	= "sync",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(sync_io),
		.help	= "Use O_SYNC for buffered writes",
		.def	= "0",
		.parent = "buffered",
	},
	{
		.name	= "bwavgtime",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(bw_avg_time),
		.help	= "Time window over which to calculate bandwidth"
			  " (msec)",
		.def	= "500",
	},
	{
		.name	= "create_serialize",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(create_serialize),
		.help	= "Serialize creating of job files",
		.def	= "1",
	},
	{
		.name	= "create_fsync",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(create_fsync),
		.help	= "Fsync file after creation",
		.def	= "1",
	},
	{
		.name	= "create_on_open",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(create_on_open),
		.help	= "Create files when they are opened for IO",
		.def	= "0",
	},
	{
		.name	= "pre_read",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(pre_read),
		.help	= "Preread files before starting official testing",
		.def	= "0",
	},
	{
		.name	= "cpuload",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(cpuload),
		.help	= "Use this percentage of CPU",
	},
	{
		.name	= "cpuchunks",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(cpucycle),
		.help	= "Length of the CPU burn cycles (usecs)",
		.def	= "50000",
		.parent = "cpuload",
	},
#ifdef FIO_HAVE_CPU_AFFINITY
	{
		.name	= "cpumask",
		.type	= FIO_OPT_INT,
		.cb	= str_cpumask_cb,
		.help	= "CPU affinity mask",
	},
	{
		.name	= "cpus_allowed",
		.type	= FIO_OPT_STR,
		.cb	= str_cpus_allowed_cb,
		.help	= "Set CPUs allowed",
	},
#endif
	{
		.name	= "end_fsync",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(end_fsync),
		.help	= "Include fsync at the end of job",
		.def	= "0",
	},
	{
		.name	= "fsync_on_close",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(fsync_on_close),
		.help	= "fsync files on close",
		.def	= "0",
	},
	{
		.name	= "unlink",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(unlink),
		.help	= "Unlink created files after job has completed",
		.def	= "0",
	},
	{
		.name	= "exitall",
		.type	= FIO_OPT_STR_SET,
		.cb	= str_exitall_cb,
		.help	= "Terminate all jobs when one exits",
	},
	{
		.name	= "stonewall",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(stonewall),
		.help	= "Insert a hard barrier between this job and previous",
	},
	{
		.name	= "new_group",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(new_group),
		.help	= "Mark the start of a new group (for reporting)",
	},
	{
		.name	= "thread",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(use_thread),
		.help	= "Use threads instead of forks",
	},
	{
		.name	= "write_bw_log",
		.type	= FIO_OPT_STR,
		.off1	= td_var_offset(write_bw_log),
		.cb	= str_write_bw_log_cb,
		.help	= "Write log of bandwidth during run",
	},
	{
		.name	= "write_lat_log",
		.type	= FIO_OPT_STR,
		.off1	= td_var_offset(write_lat_log),
		.cb	= str_write_lat_log_cb,
		.help	= "Write log of latency during run",
	},
	{
		.name	= "hugepage-size",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(hugepage_size),
		.help	= "When using hugepages, specify size of each page",
		.def	= __stringify(FIO_HUGE_PAGE),
	},
	{
		.name	= "group_reporting",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(group_reporting),
		.help	= "Do reporting on a per-group basis",
	},
	{
		.name	= "zero_buffers",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(zero_buffers),
		.help	= "Init IO buffers to all zeroes",
	},
	{
		.name	= "refill_buffers",
		.type	= FIO_OPT_STR_SET,
		.off1	= td_var_offset(refill_buffers),
		.help	= "Refill IO buffers on every IO submit",
	},
#ifdef FIO_HAVE_DISK_UTIL
	{
		.name	= "disk_util",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(do_disk_util),
		.help	= "Log disk utilization statistics",
		.def	= "1",
	},
#endif
	{
		.name	= "gtod_reduce",
		.type	= FIO_OPT_BOOL,
		.help	= "Greatly reduce number of gettimeofday() calls",
		.cb	= str_gtod_reduce_cb,
		.def	= "0",
	},
	{
		.name	= "disable_clat",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(disable_clat),
		.help	= "Disable completion latency numbers",
		.parent	= "gtod_reduce",
		.def	= "0",
	},
	{
		.name	= "disable_slat",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(disable_slat),
		.help	= "Disable submissionn latency numbers",
		.parent	= "gtod_reduce",
		.def	= "0",
	},
	{
		.name	= "disable_bw_measurement",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(disable_bw),
		.help	= "Disable bandwidth logging",
		.parent	= "gtod_reduce",
		.def	= "0",
	},
	{
		.name	= "gtod_cpu",
		.type	= FIO_OPT_INT,
		.cb	= str_gtod_cpu_cb,
		.help	= "Setup dedicated gettimeofday() thread on this CPU",
		.verify	= gtod_cpu_verify,
	},
	{
		.name	= "continue_on_error",
		.type	= FIO_OPT_BOOL,
		.off1	= td_var_offset(continue_on_error),
		.help	= "Continue on non-fatal errors during I/O",
		.def	= "0",
	},
	{
		.name	= "profile",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(profile),
		.help	= "Select a specific builtin performance test",
	},
	{
		.name	= "cgroup",
		.type	= FIO_OPT_STR_STORE,
		.off1	= td_var_offset(cgroup),
		.help	= "Add job to cgroup of this name",
	},
	{
		.name	= "cgroup_weight",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(cgroup_weight),
		.help	= "Use given weight for cgroup",
		.minval = 100,
		.maxval	= 1000,
	},
	{
		.name	= "uid",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(uid),
		.help	= "Run job with this user ID",
	},
	{
		.name	= "gid",
		.type	= FIO_OPT_INT,
		.off1	= td_var_offset(gid),
		.help	= "Run job with this group ID",
	},
	{
		.name = NULL,
	},
};

static void add_to_lopt(struct option *lopt, struct fio_option *o)
{
	lopt->name = (char *) o->name;
	lopt->val = FIO_GETOPT_JOB;
	if (o->type == FIO_OPT_STR_SET)
		lopt->has_arg = no_argument;
	else
		lopt->has_arg = required_argument;
}

void fio_options_dup_and_init(struct option *long_options)
{
	struct fio_option *o;
	unsigned int i;

	options_init(options);

	i = 0;
	while (long_options[i].name)
		i++;

	o = &options[0];
	while (o->name) {
		add_to_lopt(&long_options[i], o);

		i++;
		o++;
		assert(i < FIO_NR_OPTIONS);
	}
}

struct fio_keyword {
	const char *word;
	const char *desc;
	char *replace;
};

static struct fio_keyword fio_keywords[] = {
	{
		.word	= "$pagesize",
		.desc	= "Page size in the system",
	},
	{
		.word	= "$mb_memory",
		.desc	= "Megabytes of memory online",
	},
	{
		.word	= "$ncpus",
		.desc	= "Number of CPUs online in the system",
	},
	{
		.word	= NULL,
	},
};

void fio_keywords_init(void)
{
	unsigned long long mb_memory;
	char buf[128];
	long l;

	sprintf(buf, "%lu", page_size);
	fio_keywords[0].replace = strdup(buf);

	mb_memory = os_phys_mem() / page_size;
	sprintf(buf, "%llu", mb_memory);
	fio_keywords[1].replace = strdup(buf);

	l = sysconf(_SC_NPROCESSORS_ONLN);
	sprintf(buf, "%lu", l);
	fio_keywords[2].replace = strdup(buf);
}

#define BC_APP		"bc"

static char *bc_calc(char *str)
{
	char *buf, *tmp, opt[80];
	FILE *f;
	int ret;

	/*
	 * No math, just return string
	 */
	if (!strchr(str, '+') && !strchr(str, '-') && !strchr(str, '*') &&
	    !strchr(str, '/'))
		return str;

	/*
	 * Split option from value, we only need to calculate the value
	 */
	tmp = strchr(str, '=');
	if (!tmp)
		return str;

	tmp++;
	memset(opt, 0, sizeof(opt));
	strncpy(opt, str, tmp - str);

	buf = malloc(128);

	sprintf(buf, "which %s > /dev/null", BC_APP);
	if (system(buf)) {
		log_err("fio: bc is needed for performing math\n");
		free(buf);
		return NULL;
	}

	sprintf(buf, "echo %s | %s", tmp, BC_APP);
	f = popen(buf, "r");
	if (!f) {
		free(buf);
		return NULL;
	}

	ret = fread(buf, 1, 128, f);
	if (ret <= 0) {
		free(buf);
		return NULL;
	}

	buf[ret - 1] = '\0';
	strcat(opt, buf);
	strcpy(buf, opt);
	pclose(f);
	free(str);
	return buf;
}

/*
 * Look for reserved variable names and replace them with real values
 */
static char *fio_keyword_replace(char *opt)
{
	char *s;
	int i;

	for (i = 0; fio_keywords[i].word != NULL; i++) {
		struct fio_keyword *kw = &fio_keywords[i];

		while ((s = strstr(opt, kw->word)) != NULL) {
			char *new = malloc(strlen(opt) + 1);
			char *o_org = opt;
			int olen = s - opt;
			int len;

			/*
			 * Copy part of the string before the keyword and
			 * sprintf() the replacement after it.
			 */
			memcpy(new, opt, olen);
			len = sprintf(new + olen, "%s", kw->replace);

			/*
			 * If there's more in the original string, copy that
			 * in too
			 */
			opt += strlen(kw->word) + olen;
			if (strlen(opt))
				memcpy(new + olen + len, opt, opt - o_org - 1);

			/*
			 * replace opt and free the old opt
			 */
			opt = new;
			//free(o_org);

			/*
			 * Check for potential math and invoke bc, if possible
			 */
			opt = bc_calc(opt);
		}
	}

	return opt;
}

int fio_options_parse(struct thread_data *td, char **opts, int num_opts)
{
	int i, ret;

	sort_options(opts, options, num_opts);

	for (ret = 0, i = 0; i < num_opts; i++) {
		opts[i] = fio_keyword_replace(opts[i]);
		ret |= parse_option(opts[i], options, td);
	}

	return ret;
}

int fio_cmd_option_parse(struct thread_data *td, const char *opt, char *val)
{
	return parse_cmd_option(opt, val, options, td);
}

void fio_fill_default_options(struct thread_data *td)
{
	fill_default_options(td, options);
}

int fio_show_option_help(const char *opt)
{
	return show_cmd_help(options, opt);
}

static void __options_mem(struct thread_data *td, int alloc)
{
	struct thread_options *o = &td->o;
	struct fio_option *opt;
	char **ptr;
	int i;

	for (i = 0, opt = &options[0]; opt->name; i++, opt = &options[i]) {
		if (opt->type != FIO_OPT_STR_STORE)
			continue;

		ptr = (void *) o + opt->off1;
		if (*ptr) {
			if (alloc)
				*ptr = strdup(*ptr);
			else {
				free(*ptr);
				*ptr = NULL;
			}
		}
	}
}

/*
 * dupe FIO_OPT_STR_STORE options
 */
void options_mem_dupe(struct thread_data *td)
{
	__options_mem(td, 1);
}

void options_mem_free(struct thread_data fio_unused *td)
{
#if 0
	__options_mem(td, 0);
#endif
}

unsigned int fio_get_kb_base(void *data)
{
	struct thread_data *td = data;
	unsigned int kb_base = 0;

	if (td)
		kb_base = td->o.kb_base;
	if (!kb_base)
		kb_base = 1024;

	return kb_base;
}

int add_option(struct fio_option *o)
{
	struct fio_option *__o;
	int opt_index = 0;

	__o = options;
	while (__o->name) {
		opt_index++;
		__o++;
	}

	memcpy(&options[opt_index], o, sizeof(*o));
	return 0;
}

void invalidate_profile_options(const char *prof_name)
{
	struct fio_option *o;

	o = options;
	while (o->name) {
		if (o->prof_name && !strcmp(o->prof_name, prof_name)) {
			o->type = FIO_OPT_INVALID;
			o->prof_name = NULL;
		}
		o++;
	}
}

void add_opt_posval(const char *optname, const char *ival, const char *help)
{
	struct fio_option *o;
	unsigned int i;

	o = find_option(options, optname);
	if (!o)
		return;

	for (i = 0; i < PARSE_MAX_VP; i++) {
		if (o->posval[i].ival)
			continue;

		o->posval[i].ival = ival;
		o->posval[i].help = help;
		break;
	}
}

void del_opt_posval(const char *optname, const char *ival)
{
	struct fio_option *o;
	unsigned int i;

	o = find_option(options, optname);
	if (!o)
		return;

	for (i = 0; i < PARSE_MAX_VP; i++) {
		if (!o->posval[i].ival)
			continue;
		if (strcmp(o->posval[i].ival, ival))
			continue;

		o->posval[i].ival = NULL;
		o->posval[i].help = NULL;
	}
}
