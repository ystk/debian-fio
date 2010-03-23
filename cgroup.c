/*
 * Code related to setting up a blkio cgroup
 */
#include <stdio.h>
#include <stdlib.h>
#include <mntent.h>
#include "fio.h"
#include "flist.h"
#include "cgroup.h"
#include "smalloc.h"

static struct fio_mutex *lock;

struct cgroup_member {
	struct flist_head list;
	char *root;
};

static char *find_cgroup_mnt(struct thread_data *td)
{
	char *mntpoint = NULL;
	struct mntent *mnt;
	FILE *f;

	f = setmntent("/proc/mounts", "r");
	if (!f) {
		td_verror(td, errno, "setmntent /proc/mounts");
		return NULL;
	}

	while ((mnt = getmntent(f)) != NULL) {
		if (!strcmp(mnt->mnt_type, "cgroup") &&
		    strstr(mnt->mnt_opts, "blkio"))
			break;
	}

	if (mnt)
		mntpoint = smalloc_strdup(mnt->mnt_dir);
	else
		log_err("fio: cgroup blkio does not appear to be mounted\n");

	endmntent(f);
	return mntpoint;
}

static void add_cgroup(const char *name, struct flist_head *clist)
{
	struct cgroup_member *cm;

	cm = smalloc(sizeof(*cm));
	INIT_FLIST_HEAD(&cm->list);
	cm->root = smalloc_strdup(name);

	fio_mutex_down(lock);
	flist_add_tail(&cm->list, clist);
	fio_mutex_up(lock);
}

void cgroup_kill(struct flist_head *clist)
{
	struct flist_head *n, *tmp;
	struct cgroup_member *cm;

	fio_mutex_down(lock);

	flist_for_each_safe(n, tmp, clist) {
		cm = flist_entry(n, struct cgroup_member, list);
		rmdir(cm->root);
		flist_del(&cm->list);
		sfree(cm->root);
		sfree(cm);
	}

	fio_mutex_up(lock);
}

static char *get_cgroup_root(struct thread_data *td, char *mnt)
{
	char *str = malloc(64);

	if (td->o.cgroup)
		sprintf(str, "%s/%s", mnt, td->o.cgroup);
	else
		sprintf(str, "%s/%s", mnt, td->o.name);

	return str;
}

static int write_int_to_file(struct thread_data *td, const char *path,
			     const char *filename, unsigned int val,
			     const char *onerr)
{
	char tmp[256];
	FILE *f;
	
	sprintf(tmp, "%s/%s", path, filename);
	f = fopen(tmp, "w");
	if (!f) {
		td_verror(td, errno, onerr);
		return 1;
	}

	fprintf(f, "%u", val);
	fclose(f);
	return 0;

}

static int cgroup_write_pid(struct thread_data *td, const char *root)
{
	unsigned int val = td->pid;

	return write_int_to_file(td, root, "tasks", val, "cgroup write pid");
}

/*
 * Move pid to root class
 */
static int cgroup_del_pid(struct thread_data *td, char *mnt)
{
	return cgroup_write_pid(td, mnt);
}

int cgroup_setup(struct thread_data *td, struct flist_head *clist, char **mnt)
{
	char *root;

	if (!*mnt) {
		*mnt = find_cgroup_mnt(td);
		if (!*mnt)
			return 1;
	}

	/*
	 * Create container, if it doesn't exist
	 */
	root = get_cgroup_root(td, *mnt);
	if (mkdir(root, 0755) < 0) {
		int __e = errno;

		if (__e != EEXIST) {
			td_verror(td, __e, "cgroup mkdir");
			log_err("fio: path %s\n", root);
			goto err;
		}
	} else
		add_cgroup(root, clist);

	if (td->o.cgroup_weight) {
		if (write_int_to_file(td, root, "blkio.weight",
					td->o.cgroup_weight,
					"cgroup open weight"))
			goto err;
	}

	if (!cgroup_write_pid(td, root)) {
		free(root);
		return 0;
	}

err:
	free(root);
	return 1;
}

void cgroup_shutdown(struct thread_data *td, char **mnt)
{
	if (*mnt == NULL)
		return;
	if (!td->o.cgroup_weight && !td->o.cgroup)
		return;

	cgroup_del_pid(td, *mnt);
}

static void fio_init cgroup_init(void)
{
	lock = fio_mutex_init(1);
}

static void fio_exit cgroup_exit(void)
{
	fio_mutex_remove(lock);
}
