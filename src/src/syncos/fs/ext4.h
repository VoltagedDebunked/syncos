#ifndef _SYNCOS_FS_EXT4_H
#define _SYNCOS_FS_EXT4_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <syncos/spinlock.h>

// Forward declarations for storage drivers
struct storage_device;

// Ext4 magic number and offsets
#define EXT4_SUPER_MAGIC    0xEF53
#define EXT4_SUPERBLOCK_OFFSET 1024

// Ext4 feature flags
#define EXT4_FEATURE_COMPAT_DIR_PREALLOC   0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES  0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL    0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR       0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE   0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX      0x0020

#define EXT4_FEATURE_INCOMPAT_COMPRESSION  0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE     0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER      0x0004
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV  0x0008
#define EXT4_FEATURE_INCOMPAT_META_BG      0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS      0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT        0x0080
#define EXT4_FEATURE_INCOMPAT_MMP          0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG      0x0200

// File type constants
#define EXT4_FT_UNKNOWN     0
#define EXT4_FT_REG_FILE    1
#define EXT4_FT_DIR         2
#define EXT4_FT_CHRDEV      3
#define EXT4_FT_BLKDEV      4
#define EXT4_FT_FIFO        5
#define EXT4_FT_SOCK        6
#define EXT4_FT_SYMLINK     7
#define EXT4_FT_MAX         8

// Inode flags
#define EXT4_SECRM_FL         0x00000001  // Secure deletion
#define EXT4_UNRM_FL          0x00000002  // Record for undelete
#define EXT4_COMPR_FL         0x00000004  // Compressed file
#define EXT4_SYNC_FL          0x00000008  // Synchronous updates
#define EXT4_IMMUTABLE_FL     0x00000010  // Immutable file
#define EXT4_APPEND_FL        0x00000020  // Append only
#define EXT4_NODUMP_FL        0x00000040  // Do not dump/delete file
#define EXT4_NOATIME_FL       0x00000080  // Do not update atime
#define EXT4_DIRTY_FL         0x00000100  // Dirty (modified)
#define EXT4_COMPRBLK_FL      0x00000200  // Compressed blocks
#define EXT4_NOCOMPR_FL       0x00000400  // Don't compress
#define EXT4_ENCRYPT_FL       0x00000800  // Encrypted file
#define EXT4_INDEX_FL         0x00001000  // Hash indexed directory
#define EXT4_IMAGIC_FL        0x00002000  // AFS directory
#define EXT4_JOURNAL_DATA_FL  0x00004000  // Journal file data
#define EXT4_NOTAIL_FL        0x00008000  // Don't use tail packing
#define EXT4_DIRSYNC_FL       0x00010000  // Directory modifications are synchronous
#define EXT4_TOPDIR_FL        0x00020000  // Top of dir hierarchy
#define EXT4_HUGE_FILE_FL     0x00040000  // Huge file
#define EXT4_EXTENTS_FL       0x00080000  // Extents
#define EXT4_EA_INODE_FL      0x00200000  // Inode uses extents
#define EXT4_EOFBLOCKS_FL     0x00400000  // Blocks allocated beyond EOF
#define EXT4_SNAPFILE_FL      0x01000000  // Snapshot file
#define EXT4_SNAPFILE_DELETED_FL 0x04000000  // Snapshot being deleted
#define EXT4_SNAPFILE_SHRUNK_FL  0x08000000  // Snapshot shrink has completed
#define EXT4_INLINE_DATA_FL   0x10000000  // Inode has inline data
#define EXT4_PROJINHERIT_FL   0x20000000  // Create with parents projid
#define EXT4_CASEFOLD_FL      0x40000000  // Directory has case folding

// Ext4 superblock structure
typedef struct {
    uint32_t s_inodes_count;           // Total inode count
    uint32_t s_blocks_count_lo;        // Total block count (low 32 bits)
    uint32_t s_r_blocks_count_lo;      // Reserved block count (low 32 bits) 
    uint32_t s_free_blocks_count_lo;   // Free block count (low 32 bits)
    uint32_t s_free_inodes_count;      // Free inode count
    uint32_t s_first_data_block;       // First data block
    uint32_t s_log_block_size;         // Block size (log2(block size) - 10)
    uint32_t s_log_cluster_size;       // Cluster size (log2(cluster size) - 10)
    uint32_t s_blocks_per_group;       // Blocks per group
    uint32_t s_clusters_per_group;     // Clusters per group
    uint32_t s_inodes_per_group;       // Inodes per group
    uint32_t s_mtime;                  // Mount time
    uint32_t s_wtime;                  // Write time
    uint16_t s_mnt_count;              // Mount count
    uint16_t s_max_mnt_count;          // Maximal mount count
    uint16_t s_magic;                  // Magic signature
    uint16_t s_state;                  // Filesystem state
    uint16_t s_errors;                 // Error behavior
    uint16_t s_minor_rev_level;        // Minor revision level
    uint32_t s_lastcheck;              // Last check time
    uint32_t s_checkinterval;          // Check interval
    uint32_t s_creator_os;             // Creator OS
    uint32_t s_rev_level;              // Revision level
    uint16_t s_def_resuid;             // Default uid for reserved blocks
    uint16_t s_def_resgid;             // Default gid for reserved blocks
    
    // For EXT4_DYNAMIC_REV superblocks
    uint32_t s_first_ino;              // First non-reserved inode
    uint16_t s_inode_size;             // Size of inode structure
    uint16_t s_block_group_nr;         // Block group # of this superblock
    uint32_t s_feature_compat;         // Compatible features
    uint32_t s_feature_incompat;       // Incompatible features
    uint32_t s_feature_ro_compat;      // Read-only compatible features
    uint8_t  s_uuid[16];               // 128-bit UUID for volume
    char     s_volume_name[16];        // Volume name
    char     s_last_mounted[64];       // Directory where last mounted
    uint32_t s_algorithm_usage_bitmap; // For compression
    
    // Performance hints
    uint8_t  s_prealloc_blocks;        // # of blocks to preallocate
    uint8_t  s_prealloc_dir_blocks;    // # to preallocate for dirs
    uint16_t s_reserved_gdt_blocks;    // Per group descriptor for growth
    
    // Journaling support
    uint8_t  s_journal_uuid[16];       // UUID of journal superblock
    uint32_t s_journal_inum;           // Inode number of journal file
    uint32_t s_journal_dev;            // Device number of journal file
    uint32_t s_last_orphan;            // Head of list of inodes to delete
    uint32_t s_hash_seed[4];           // HTREE hash seed
    uint8_t  s_def_hash_version;       // Default hash version
    uint8_t  s_jnl_backup_type;        // Journal backup type
    uint16_t s_desc_size;              // Size of group desc
    uint32_t s_default_mount_opts;     // Default mount options
    uint32_t s_first_meta_bg;          // First metablock group
    uint32_t s_mkfs_time;              // When filesystem was created
    uint32_t s_jnl_blocks[17];         // Backup of journal inode
    
    // 64-bit support
    uint32_t s_blocks_count_hi;        // Blocks count (high 32 bits)
    uint32_t s_r_blocks_count_hi;      // Reserved blocks count (high 32 bits)
    uint32_t s_free_blocks_count_hi;   // Free blocks count (high 32 bits)
    uint16_t s_min_extra_isize;        // Min inode extra size
    uint16_t s_want_extra_isize;       // Desired inode extra size
    uint32_t s_flags;                  // Miscellaneous flags
    uint16_t s_raid_stride;            // RAID stride
    uint16_t s_mmp_interval;           // MMP check interval
    uint64_t s_mmp_block;              // Block for MMP data
    uint32_t s_raid_stripe_width;      // RAID stripe width
    uint8_t  s_log_groups_per_flex;    // FLEX_BG group size
    uint8_t  s_checksum_type;          // Metadata checksum algorithm
    uint8_t  s_encryption_level;       // Versioning level for encryption
    uint8_t  s_reserved_pad;           // Padding
    uint64_t s_kbytes_written;         // Number of KiB written
    uint32_t s_snapshot_inum;          // Inode number of active snapshot
    uint32_t s_snapshot_id;            // ID of the active snapshot
    uint64_t s_snapshot_r_blocks_count; // Reserved blocks for snapshot
    uint32_t s_snapshot_list;          // Inode number of snapshot list
    uint32_t s_error_count;            // Number of errors seen
    uint32_t s_first_error_time;       // First time an error happened
    uint32_t s_first_error_ino;        // Inode involved in first error
    uint64_t s_first_error_block;      // Block involved in first error
    uint8_t  s_first_error_func[32];   // Function where error hit
    uint32_t s_first_error_line;       // Line number where error hit
    uint32_t s_last_error_time;        // Most recent time of an error
    uint32_t s_last_error_ino;         // Inode involved in last error
    uint32_t s_last_error_line;        // Line number where error hit
    uint64_t s_last_error_block;       // Block involved in last error
    uint8_t  s_last_error_func[32];    // Function where error hit
    uint8_t  s_mount_opts[64];         // Mount options in use
    uint32_t s_usr_quota_inum;         // Inode for tracking user quota
    uint32_t s_grp_quota_inum;         // Inode for tracking group quota
    uint32_t s_overhead_clusters;      // Overhead blocks/clusters in fs
    uint32_t s_backup_bgs[2];          // Groups containing superblock backups
    uint8_t  s_encrypt_algos[4];       // Encryption algorithms
    uint8_t  s_encrypt_pw_salt[16];    // Salt used for string2key algorithm
    uint32_t s_lpf_ino;                // Lost+found inode
    uint32_t s_prj_quota_inum;         // Inode for tracking project quota
    uint32_t s_checksum_seed;          // Checksum seed
    uint8_t  s_wtime_hi;               // High bits of last write time
    uint8_t  s_mtime_hi;               // High bits of last mount time
    uint8_t  s_mkfs_time_hi;           // High bits of fs creation time
    uint8_t  s_lastcheck_hi;           // High bits of last check time
    uint8_t  s_first_error_time_hi;    // High bits of first error time
    uint8_t  s_last_error_time_hi;     // High bits of last error time
    uint8_t  s_pad[2];                 // Padding
    uint16_t s_encoding;               // Filename charset encoding
    uint16_t s_encoding_flags;         // Filename charset encoding flags
    uint32_t s_reserved[95];           // Padding to the end of the block
    uint32_t s_checksum;               // Checksum of the superblock
} __attribute__((packed)) ext4_superblock_t;

// Ext4 group descriptor
typedef struct {
    uint32_t bg_block_bitmap_lo;       // Blocks bitmap block (lo)
    uint32_t bg_inode_bitmap_lo;       // Inodes bitmap block (lo)
    uint32_t bg_inode_table_lo;        // Inodes table block (lo)
    uint16_t bg_free_blocks_count_lo;  // Free blocks count (lo)
    uint16_t bg_free_inodes_count_lo;  // Free inodes count (lo)
    uint16_t bg_used_dirs_count_lo;    // Directories count (lo)
    uint16_t bg_flags;                 // Flags
    uint32_t bg_exclude_bitmap_lo;     // Exclude bitmap block (lo)
    uint16_t bg_block_bitmap_csum_lo;  // Block bitmap checksum (lo)
    uint16_t bg_inode_bitmap_csum_lo;  // Inode bitmap checksum (lo)
    uint16_t bg_itable_unused_lo;      // Unused inodes count (lo)
    uint16_t bg_checksum;              // Group descriptor checksum
    uint32_t bg_block_bitmap_hi;       // Blocks bitmap block (hi)
    uint32_t bg_inode_bitmap_hi;       // Inodes bitmap block (hi)
    uint32_t bg_inode_table_hi;        // Inodes table block (hi)
    uint16_t bg_free_blocks_count_hi;  // Free blocks count (hi)
    uint16_t bg_free_inodes_count_hi;  // Free inodes count (hi)
    uint16_t bg_used_dirs_count_hi;    // Directories count (hi)
    uint16_t bg_itable_unused_hi;      // Unused inodes count (hi)
    uint32_t bg_exclude_bitmap_hi;     // Exclude bitmap block (hi)
    uint16_t bg_block_bitmap_csum_hi;  // Block bitmap checksum (hi)
    uint16_t bg_inode_bitmap_csum_hi;  // Inode bitmap checksum (hi)
    uint32_t bg_reserved;              // Reserved
} __attribute__((packed)) ext4_group_desc_t;

// Ext4 inode structure
typedef struct {
    uint16_t i_mode;                   // File mode
    uint16_t i_uid;                    // Owner Uid (low 16 bits)
    uint32_t i_size_lo;                // Size in bytes (low 32 bits)
    uint32_t i_atime;                  // Access time
    uint32_t i_ctime;                  // Creation time
    uint32_t i_mtime;                  // Modification time
    uint32_t i_dtime;                  // Deletion Time
    uint16_t i_gid;                    // Group Id (low 16 bits)
    uint16_t i_links_count;            // Links count
    uint32_t i_blocks_lo;              // Blocks count (low 32 bits)
    uint32_t i_flags;                  // File flags
    union {
        struct {
            uint32_t reserved1;        // OS dependent
        } linux1;
        struct {
            uint32_t high_size;        // High 32 bits of size
        } hurd1;
        struct {
            uint32_t translator;       // Translator
        } masix1;
    } osd1;                            // OS dependent 1
    uint32_t i_block[15];              // Block pointers
    uint32_t i_generation;             // File version (for NFS)
    uint32_t i_file_acl_lo;            // EA block (low 32 bits)
    uint32_t i_size_high;              // Size in bytes (high 32 bits)
    uint32_t i_obso_faddr;             // Obsoleted fragment address
    union {
        struct {
            uint16_t l_i_blocks_high;  // Blocks count (high 16 bits)
            uint16_t l_i_file_acl_high;// EA block (high 16 bits)
            uint16_t l_i_uid_high;     // Owner uid (high 16 bits)
            uint16_t l_i_gid_high;     // Group id (high 16 bits)
            uint16_t l_i_checksum_lo;  // Inode checksum (low 16 bits)
            uint16_t l_i_reserved;     // Reserved
        } linux2;
        struct {
            uint16_t h_i_reserved1;    // Reserved
            uint16_t h_i_mode_high;    // Mode high bits
            uint16_t h_i_uid_high;     // Owner uid (high 16 bits)
            uint16_t h_i_gid_high;     // Group id (high 16 bits)
            uint32_t h_i_author;       // Author
        } hurd2;
        struct {
            uint16_t h_i_reserved1;    // Reserved
            uint16_t m_i_file_acl_high;// EA block (high 16 bits)
            uint32_t m_i_reserved2[2]; // Reserved
        } masix2;
    } osd2;                            // OS dependent 2
    uint16_t i_extra_isize;            // Additional size
    uint16_t i_checksum_hi;            // Inode checksum (high 16 bits)
    uint32_t i_ctime_extra;            // Extra creation time
    uint32_t i_mtime_extra;            // Extra modification time
    uint32_t i_atime_extra;            // Extra access time
    uint32_t i_crtime;                 // Creation time
    uint32_t i_crtime_extra;           // Extra creation time
    uint32_t i_version_hi;             // High version bits
    uint32_t i_projid;                 // Project ID
} __attribute__((packed)) ext4_inode_t;

// Ext4 directory entry structure
typedef struct {
    uint32_t inode;                    // Inode number
    uint16_t rec_len;                  // Directory entry length
    uint8_t  name_len;                 // Name length
    uint8_t  file_type;                // File type
    char     name[];                   // File name (up to 255 bytes)
} __attribute__((packed)) ext4_dir_entry_t;

// Ext4 extent header
typedef struct {
    uint16_t eh_magic;                 // Magic number, should be 0xF30A
    uint16_t eh_entries;               // Number of valid entries
    uint16_t eh_max;                   // Maximum number of entries
    uint16_t eh_depth;                 // Has tree depth (0 = leaf node)
    uint32_t eh_generation;            // Generation of the tree
} __attribute__((packed)) ext4_extent_header_t;

// Ext4 extent index (internal node of the tree)
typedef struct {
    uint32_t ei_block;                 // First logical block covered
    uint32_t ei_leaf_lo;               // Lower 32-bits of physical block
    uint16_t ei_leaf_hi;               // Upper 16-bits of physical block
    uint16_t ei_unused;                // Unused
} __attribute__((packed)) ext4_extent_idx_t;

// Ext4 extent (leaf node)
typedef struct {
    uint32_t ee_block;                 // First logical block covered
    uint16_t ee_len;                   // Number of blocks covered
    uint16_t ee_start_hi;              // Upper 16-bits of physical block
    uint32_t ee_start_lo;              // Lower 32-bits of physical block
} __attribute__((packed)) ext4_extent_t;

// Ext4 extent tree
typedef struct {
    ext4_extent_header_t header;       // Extent tree header
    union {
        ext4_extent_idx_t index[4];    // Index nodes
        ext4_extent_t extent[4];       // Leaf nodes
    };
} __attribute__((packed)) ext4_extent_tree_t;

// Storage device type (SATA or NVMe)
typedef enum {
    STORAGE_TYPE_NONE = 0,
    STORAGE_TYPE_NVME,
    STORAGE_TYPE_SATA
} storage_type_t;

// Storage device structure (abstraction over NVMe/SATA)
typedef struct storage_device {
    storage_type_t type;               // Device type
    uint32_t device_id;                // Device ID
    uint32_t namespace_id;             // Namespace ID (for NVMe)
    uint32_t sector_size;              // Sector size in bytes
    uint64_t sector_count;             // Total number of sectors
    
    // Function pointers for device operations
    bool (*read)(struct storage_device* dev, uint64_t lba, void* buffer, uint32_t sector_count);
    bool (*write)(struct storage_device* dev, uint64_t lba, const void* buffer, uint32_t sector_count);
    bool (*flush)(struct storage_device* dev);
} storage_device_t;

// Ext4 filesystem instance
typedef struct {
    storage_device_t* device;          // Storage device
    ext4_superblock_t superblock;      // Superblock
    uint32_t block_size;               // Block size in bytes
    uint64_t block_count;              // Total number of blocks
    uint32_t blocks_per_group;         // Blocks per group
    uint32_t inodes_per_group;         // Inodes per group
    uint32_t inode_size;               // Size of inode structure
    uint32_t groups_count;             // Number of block groups
    ext4_group_desc_t* group_desc;     // Group descriptors
    spinlock_t lock;                   // Filesystem lock
    
    // Cache structures
    void* block_buffer;                // Buffer for block I/O
    uint64_t block_buffer_block;       // Block number of the cached block
    bool block_buffer_dirty;           // Whether block buffer is dirty
} ext4_fs_t;

// File handle
typedef struct {
    ext4_fs_t* fs;                     // Filesystem
    uint32_t inode_num;                // Inode number
    ext4_inode_t inode;                // Inode
    uint64_t position;                 // Current position in file
    spinlock_t lock;                   // File lock
} ext4_file_t;

// Directory entry structure for readdir
typedef struct {
    uint32_t inode;                    // Inode number
    uint8_t type;                      // File type
    char name[256];                    // Filename (null-terminated)
} ext4_dirent_t;

// Function prototypes

// Filesystem operations
bool ext4_detect(storage_device_t* device);
bool ext4_mount(storage_device_t* device, ext4_fs_t** fs_out);
void ext4_unmount(ext4_fs_t* fs);

// File operations
bool ext4_open(ext4_fs_t* fs, const char* path, ext4_file_t** file_out);
void ext4_close(ext4_file_t* file);
int64_t ext4_read(ext4_file_t* file, void* buffer, uint64_t size);
int64_t ext4_write(ext4_file_t* file, const void* buffer, uint64_t size);
bool ext4_seek(ext4_file_t* file, int64_t offset, int whence);
uint64_t ext4_tell(ext4_file_t* file);
uint64_t ext4_size(ext4_file_t* file);

// Directory operations
bool ext4_opendir(ext4_fs_t* fs, const char* path, ext4_file_t** dir_out);
bool ext4_readdir(ext4_file_t* dir, ext4_dirent_t* dirent);
void ext4_closedir(ext4_file_t* dir);

// Path operations
bool ext4_stat(ext4_fs_t* fs, const char* path, ext4_inode_t* inode_out);
bool ext4_exists(ext4_fs_t* fs, const char* path);
bool ext4_is_directory(ext4_fs_t* fs, const char* path);
bool ext4_is_regular(ext4_fs_t* fs, const char* path);

// Block operations
bool ext4_read_block(ext4_fs_t* fs, uint64_t block_num, void* buffer);
bool ext4_write_block(ext4_fs_t* fs, uint64_t block_num, const void* buffer);
bool ext4_flush_blocks(ext4_fs_t* fs);

// Initialize storage device from SATA or NVMe
storage_device_t* storage_device_create(uint32_t device_id, storage_type_t type);
void storage_device_destroy(storage_device_t* device);

// Auto-detect function for NVMe and SATA devices
void storage_detect_all_devices(void);

void ext4_init();

#endif // _SYNCOS_FS_EXT4_H