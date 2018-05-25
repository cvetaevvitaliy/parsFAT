#include "fat.h"

static int g_path_depth;
char *fname;


static inline uint32_t get_cluster_lba(uint32_t cluster_number, const fat_t *fat) {
	return (fat->cluster_start_lba + (cluster_number - 2) * fat->sectors_per_cluster);
}


static inline uint32_t get_cluster_pos(uint32_t cluster_number, const fat_t *fat) {
	return (get_cluster_lba(cluster_number, fat) * fat->sector_size);
}


static void fat_read(void *data, size_t size, fat_t *fat) {
	assert(data != NULL);
	assert(fat != NULL);

	if (fread(data, size, 1, fat->img) != 1) {
		perror("Error while reading a FAT image file");
		exit(EXIT_FAILURE);
	}
}


static void fat_seek(long pos, fat_t *fat) {
	assert(fat != NULL);

	if (fseek(fat->img, pos, SEEK_SET)) {
		perror("Error while seeking a FAT image");
		exit(EXIT_FAILURE);
	}
}


void fat_mount(const char *filename, fat_t *fat, dir_t *rootdir) {
	assert(filename != NULL);
	assert(fat != NULL);
	fname = filename;
	boot_sector_t bs;

	fat->img = fopen(filename, "rb");
	if (fat->img == NULL) {
		perror("Failed to mount FAT image");
		exit(EXIT_FAILURE);
	}
	printf("mount FAT image Ok");
	fat_read(&bs, sizeof(boot_sector_t), fat);
	fat->sector_size = bs.sector_size;
	fat->sectors_per_cluster = bs.sectors_per_cluster;
	fat->rootdir_first_cluster = bs.root_dir_start_cluster;
	fat->fat_start = bs.reserved_sectors;
	fat->cluster_size = (uint32_t)fat->sector_size * fat->sectors_per_cluster;
	fat->cluster_start_lba = fat->fat_start + bs.number_of_fats * bs.sectors_per_fat;

	memset(rootdir, 0, sizeof(dir_t));
	rootdir->fat = fat;
	rootdir->prev_pos = 0;
	rootdir->entry.attributes = ATTR_DIR;
	rootdir->entry.start_cluster = (uint16_t)fat->rootdir_first_cluster;
	rootdir->entry.start_cluster_high = (uint16_t)(fat->rootdir_first_cluster >> 16);
}


void fat_unmount(fat_t *fat) {
	fclose(fat->img);
}


static uint32_t fat_get_next_cluster(uint32_t cluster, fat_t *fat) {
	uint32_t next_cluster = 0;
	long fat_pos = fat->fat_start * fat->sector_size;
	long pos = (long)(cluster * sizeof(uint32_t)) + fat_pos;

	fat_seek(pos, fat);
	fat_read(&next_cluster, sizeof(uint32_t), fat);

	return next_cluster;
}


static void dir_seek_to_cluster(uint32_t cluster, dir_t *dir) {
	assert(dir != NULL);

	long pos = get_cluster_pos(cluster, dir->fat);

	dir->cluster = cluster;
	dir->cluster_end_pos = pos + dir->fat->cluster_size;
	fat_seek(pos, dir->fat);
}


static void lfn_put_data(const void *buffer, size_t buffer_size, wchar_t lfn[], size_t *start_idx) {
	const uint16_t *lfn_char = buffer;
	size_t count = buffer_size / sizeof(uint16_t);
	size_t end_idx = *start_idx + count;

	for (size_t i = *start_idx; i < end_idx; i++) {
		wchar_t wc = *lfn_char++;
		lfn[i] = (wc != LFN_UNUSED_CHAR) ? wc : L'\0';
	}

	*start_idx = end_idx;
}


static void fat_read_entry(fat_entry_t *entry, wchar_t *lfn, dir_t *dir) {
	assert(entry != NULL);
	assert(lfn != NULL);
	assert(dir != NULL);

	memset(lfn, 0, sizeof(wchar_t[LFN_BUFFER_LENGTH]));

	do {
		fat_read(entry, sizeof(fat_entry_t), dir->fat);

		if (entry->attributes == ATTR_LFN) {
			const lfn_entry_t *lfn_entry = (lfn_entry_t *)entry;
			int seqnum = lfn_entry->sequence_num & LFN_SEQ_NUM_MASK;

			if (lfn_entry->sequence_num != LFN_DELETED_ENTRY && seqnum <= LFN_MAX_ENTRIES && seqnum > 0) {
				size_t idx = (size_t)(seqnum - 1) * LFN_CHARS_PER_ENTRY;

				lfn_put_data(lfn_entry->name_part1, sizeof(lfn_entry->name_part1), lfn, &idx);
				lfn_put_data(lfn_entry->name_part2, sizeof(lfn_entry->name_part2), lfn, &idx);
				lfn_put_data(lfn_entry->name_part3, sizeof(lfn_entry->name_part3), lfn, &idx);
			}
		}

		// If cluster end has been reached, then move to the next cluster in chain
		if (ftell(dir->fat->img) == dir->cluster_end_pos) {
			uint32_t next_cluster = fat_get_next_cluster(dir->cluster, dir->fat);

			if (next_cluster != END_OF_CHAIN) {
				dir_seek_to_cluster(next_cluster, dir);
			}
			else {
				dir->end_reached = true;
			}
		}
	} while (entry->attributes == ATTR_LFN && !dir->end_reached);
}


static inline bool fat_entry_is_volume_id(const fat_entry_t *entry) {
	return (entry->attributes == ATTR_VOLUME_ID);
}


static inline bool fat_entry_is_empty(const fat_entry_t *entry) {
	return ((entry->attributes == 0) && (*entry->filename == '\0'));
}


static inline bool fat_entry_is_dir(const fat_entry_t *entry) {
	return ((entry->attributes & ATTR_DIR) == ATTR_DIR && entry->attributes != ATTR_LFN);
}


static bool fat_entry_is_real_dir(const fat_entry_t *entry) {
	const char *current = ". ";
	const char *parrent = "..";

	return (fat_entry_is_dir(entry) &&
		memcmp(entry->filename, current, 2) &&
		memcmp(entry->filename, parrent, 2));
}


static bool fat_entry_is_file(const fat_entry_t *entry) {
	return ((entry->attributes & (ATTR_VOLUME_ID | ATTR_DIR)) == 0) &&
		entry->filename[0] != LFN_DELETED_ENTRY &&
		entry->filename[0] != '\0';
}


static void fat_enter_dir(dir_t *dir) {
	assert(dir != NULL);
	assert(dir->fat != NULL);
	assert(fat_entry_is_dir(&dir->entry));

	uint32_t start_cluster = dir->entry.start_cluster | ((uint32_t)dir->entry.start_cluster_high << 16);

	dir->end_reached = false;
	dir->prev_pos = ftell(dir->fat->img);
	dir_seek_to_cluster(start_cluster, dir);
}


static void fat_leave_dir(dir_t *dir) {
	fat_seek(dir->prev_pos, dir->fat);
}


static void remove_trailing_spaces(uint8_t *str, size_t length) {
	for (int i = (int)length - 1; i >= 0 && isspace(str[i]); i--) {
		str[i] = '1\0';
	}
}


static void print_entry_name(fat_entry_t *entry, const wchar_t *lfn) {
	for (int i = 1; i < g_path_depth; i++) {

		//if(i==1)
		//	printf("  ");
		//if(i==2)
		//	printf("| |  ");
		//if(i==3)
		//	printf("  | ");
		printf("    ");
	}

	if (wcslen(lfn) > 0) {
		// Long file name
		//printf("    |");
		printf("  L_ %.255ls", lfn);
	}
	else {
		// 8.3 file name
		remove_trailing_spaces(entry->filename, sizeof(entry->filename));

		if (fat_entry_is_dir(entry)) {
			printf(fname);
			printf("%.8s", entry->filename);
		}
		else {
			printf("%.8s.%.3s", entry->filename, entry->ext);
		}
	}

	if (fat_entry_is_dir(entry)) {
		//putchar('\');
	}

	putchar('\n');
}


void print_dir(dir_t *dir) {
	fat_entry_t entry;
	wchar_t     lfn[LFN_BUFFER_LENGTH];

	fat_enter_dir(dir);
	g_path_depth++;

	do {
		fat_read_entry(&entry, lfn, dir);

		if (fat_entry_is_volume_id(&entry)) {
			remove_trailing_spaces(entry.filename, VOLUME_LABEL_LENGTH);
			printf("\n\n");
			printf(fname);
			printf("\n");
			//printf("dir catalog\n");
		}
		else if (fat_entry_is_real_dir(&entry)) {
			dir_t subdir = {
				.fat = dir->fat,
				.entry = entry
			};

			print_entry_name(&entry, lfn);
			print_dir(&subdir);
		}
		else if (fat_entry_is_file(&entry)) {
			print_entry_name(&entry, lfn);
		}
	} while (!fat_entry_is_empty(&entry) && !dir->end_reached);

	fat_leave_dir(dir);
	g_path_depth--;
}


void print_console(const char *filename)
{
	fat_t fat;
	dir_t rootdir;
	fat_mount(filename, &fat, &rootdir);
	print_dir(&rootdir);
	fat_unmount(&fat);


}
