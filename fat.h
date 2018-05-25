#ifndef __FAT_H__
#define __FAT_H__

#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <wchar.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>



// Attribute flags of FAT entry.
#define ATTR_VOLUME_ID              (1 << 3)
#define ATTR_DIR                    (1 << 4)
#define ATTR_LFN                    0x0F

// Long file names
#define LFN_MAX_ENTRIES             20
#define LFN_CHARS_PER_ENTRY         13
#define LFN_BUFFER_LENGTH           (LFN_MAX_ENTRIES * LFN_CHARS_PER_ENTRY)
#define LFN_UNUSED_CHAR             0xFFFFu
#define LFN_SEQ_NUM_MASK            0x1F
#define LFN_DELETED_ENTRY           0xE5

#define VOLUME_LABEL_LENGTH         11
#define END_OF_CHAIN                0x0FFFFFFFu


#pragma pack(push,1)
typedef struct _boot_sector_t {
	// Boot sector
	uint8_t  jmp[3];
	uint8_t  oem[8];

	// DOS BIOS parameter block
	uint16_t sector_size;
	uint8_t  sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t  number_of_fats;
	uint16_t root_dir_entries;
	uint16_t total_sectors_short;
	uint8_t  media_descriptor;
	uint16_t _unused_fat_size_sectors;
	uint16_t sectors_per_track;
	uint16_t number_of_heads;
	uint32_t hidden_sectors;
	uint32_t total_sectors_long;

	// FAT32 extended BPB
	uint32_t sectors_per_fat;
	uint16_t drive_description;
	uint16_t version;
	uint32_t root_dir_start_cluster;
	uint16_t info_sector;
	uint16_t bs_copy_sector;
	uint8_t  reserved[38];
}  boot_sector_t;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct _fat_entry_t {
	uint8_t  filename[8];
	uint8_t  ext[3];
	uint8_t  attributes;
	uint8_t  reserved[8];
	uint16_t start_cluster_high;
	uint16_t modify_time;
	uint16_t modify_date;
	uint16_t start_cluster;
	uint32_t file_size;
}  fat_entry_t;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct _lfn_entry_t {
	uint8_t  sequence_num;
	uint8_t  name_part1[10];
	uint8_t  attributes;
	uint8_t  type;
	uint8_t  checksum;
	uint8_t  name_part2[12];
	uint16_t first_cluster;
	uint8_t  name_part3[4];
}  lfn_entry_t;
#pragma pack(pop)



typedef struct _fat_t {
	FILE*    img;
	uint16_t sector_size;
	uint8_t  sectors_per_cluster;
	uint32_t fat_start;
	uint32_t cluster_size;
	uint32_t cluster_start_lba;
	uint32_t rootdir_first_cluster;
} fat_t;


typedef struct _dir_t {
	fat_t*   fat;
	bool     end_reached;
	long     prev_pos;
	long     cluster_end_pos;
	uint32_t cluster;
	fat_entry_t entry;
} dir_t;


void print_console(const char *filename);


#endif // __FAT_H__

