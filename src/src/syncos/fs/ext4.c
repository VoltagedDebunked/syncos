#include <syncos/fs/ext4.h>
#include <core/drivers/nvme.h>
#include <core/drivers/sata.h>
#include <syncos/spinlock.h>
#include <kstd/stdio.h>
#include <kstd/string.h>
#include <syncos/vmm.h>
#include <syncos/pmm.h>

static bool nvme_storage_read(storage_device_t* dev, uint64_t lba, void* buffer, uint32_t sector_count);
static bool nvme_storage_write(storage_device_t* dev, uint64_t lba, const void* buffer, uint32_t sector_count);
static bool nvme_storage_flush(storage_device_t* dev);
static bool sata_storage_read(storage_device_t* dev, uint64_t lba, void* buffer, uint32_t sector_count);
static bool sata_storage_write(storage_device_t* dev, uint64_t lba, const void* buffer, uint32_t sector_count);
static bool sata_storage_flush(storage_device_t* dev);

// Debug macro
#define EXT4_DEBUG 1
#ifdef EXT4_DEBUG
#define EXT4_TRACE(fmt, ...) printf("EXT4: " fmt "\n", ##__VA_ARGS__)
#else
#define EXT4_TRACE(fmt, ...)
#endif

// Number of detected devices
static uint32_t storage_device_count = 0;
static storage_device_t detected_devices[16]; // Max 16 storage devices

// Helper functions

// Convert sectors to blocks
static uint64_t sector_to_block(ext4_fs_t* fs, uint64_t sector) {
    return sector * fs->device->sector_size / fs->block_size;
}

// Convert blocks to sectors
static uint64_t block_to_sector(ext4_fs_t* fs, uint64_t block) {
    return block * fs->block_size / fs->device->sector_size;
}

// Read blocks from device
static bool read_blocks(ext4_fs_t* fs, uint64_t block, uint32_t count, void* buffer) {
    uint64_t start_sector = block_to_sector(fs, block);
    uint32_t sector_count = (count * fs->block_size + fs->device->sector_size - 1) / fs->device->sector_size;
    
    return fs->device->read(fs->device, start_sector, buffer, sector_count);
}

// Write blocks to device
static bool write_blocks(ext4_fs_t* fs, uint64_t block, uint32_t count, const void* buffer) {
    uint64_t start_sector = block_to_sector(fs, block);
    uint32_t sector_count = (count * fs->block_size + fs->device->sector_size - 1) / fs->device->sector_size;
    
    return fs->device->write(fs->device, start_sector, buffer, sector_count);
}

// Initialize storage device abstraction
storage_device_t* storage_device_create(uint32_t device_id, storage_type_t type) {
    storage_device_t* device = NULL;
    
    // Find an empty slot in the device array
    for (uint32_t i = 0; i < 16; i++) {
        if (detected_devices[i].type == STORAGE_TYPE_NONE) {
            device = &detected_devices[i];
            break;
        }
    }
    
    if (!device) {
        EXT4_TRACE("No free device slots available");
        return NULL;
    }
    
    // Set device type and ID
    device->type = type;
    device->device_id = device_id;
    
    // Initialize device-specific parameters
    if (type == STORAGE_TYPE_NVME) {
        // Use NVMe namespace 1 by default
        device->namespace_id = 1;
        
        // Get NVMe device parameters
        if (!nvme_get_namespace_info(device_id, device->namespace_id, 
                                    &device->sector_count, &device->sector_size)) {
            EXT4_TRACE("Failed to get NVMe namespace info");
            device->type = STORAGE_TYPE_NONE;
            return NULL;
        }
        
        // Set operation functions - need to define wrapper functions
        device->read = nvme_storage_read;
        device->write = nvme_storage_write;
        device->flush = nvme_storage_flush;
        
        EXT4_TRACE("Created NVMe storage device: id=%u, ns=%u, sectors=%lu, sector_size=%u",
                  device_id, device->namespace_id, device->sector_count, device->sector_size);
    } else if (type == STORAGE_TYPE_SATA) {
        // Get SATA device parameters
        if (device_id >= sata_get_port_count()) {
            EXT4_TRACE("Invalid SATA port ID: %u", device_id);
            device->type = STORAGE_TYPE_NONE;
            return NULL;
        }
        
        // Get sector size (fixed as 512 bytes if cannot be determined)
        char info_buffer[256];
        if (sata_get_port_info(device_id, info_buffer, sizeof(info_buffer))) {
            // Try to parse the info to get sector size and count
            // For now, we'll use defaults
            device->sector_size = 512;
            device->sector_count = 0; // Will be determined during filesystem detection
        } else {
            device->sector_size = 512;
            device->sector_count = 0;
        }
        
        // Set operation functions
        device->read = sata_storage_read;
        device->write = sata_storage_write;
        device->flush = sata_storage_flush;
        
        EXT4_TRACE("Created SATA storage device: id=%u, sector_size=%u",
                  device_id, device->sector_size);
    } else {
        EXT4_TRACE("Unknown storage device type: %d", type);
        device->type = STORAGE_TYPE_NONE;
        return NULL;
    }
    
    return device;
}

// Destroy storage device
void storage_device_destroy(storage_device_t* device) {
    if (device) {
        device->type = STORAGE_TYPE_NONE;
    }
}

// NVMe storage wrappers implementation
static bool nvme_storage_read(storage_device_t* dev, uint64_t lba, void* buffer, uint32_t sector_count) {
    return nvme_read(dev->device_id, dev->namespace_id, lba, buffer, sector_count);
}

static bool nvme_storage_write(storage_device_t* dev, uint64_t lba, const void* buffer, uint32_t sector_count) {
    return nvme_write(dev->device_id, dev->namespace_id, lba, buffer, sector_count);
}

static bool nvme_storage_flush(storage_device_t* dev) {
    return nvme_flush(dev->device_id, dev->namespace_id);
}

// SATA storage wrappers implementation
static bool sata_storage_read(storage_device_t* dev, uint64_t lba, void* buffer, uint32_t sector_count) {
    return sata_read(dev->device_id, lba, buffer, sector_count);
}

static bool sata_storage_write(storage_device_t* dev, uint64_t lba, const void* buffer, uint32_t sector_count) {
    return sata_write(dev->device_id, lba, buffer, sector_count);
}

static bool sata_storage_flush(storage_device_t* dev) {
    return sata_flush(dev->device_id);
}

// Detect all storage devices (NVMe and SATA)
void storage_detect_all_devices(void) {
    EXT4_TRACE("Detecting storage devices...");
    
    // Initialize device array
    for (uint32_t i = 0; i < 16; i++) {
        detected_devices[i].type = STORAGE_TYPE_NONE;
    }
    
    // Detect NVMe devices
    uint32_t nvme_count = nvme_get_device_count();
    EXT4_TRACE("Found %u NVMe device(s)", nvme_count);
    
    for (uint32_t i = 0; i < nvme_count; i++) {
        storage_device_t* dev = storage_device_create(i, STORAGE_TYPE_NVME);
        if (dev) {
            storage_device_count++;
        }
    }
    
    // Detect SATA devices
    uint32_t sata_count = sata_get_port_count();
    EXT4_TRACE("Found %u SATA device(s)", sata_count);
    
    for (uint32_t i = 0; i < sata_count; i++) {
        storage_device_t* dev = storage_device_create(i, STORAGE_TYPE_SATA);
        if (dev) {
            storage_device_count++;
        }
    }
    
    EXT4_TRACE("Total storage devices detected: %u", storage_device_count);
}

// Check if a device contains an Ext4 filesystem
bool ext4_detect(storage_device_t* device) {
    if (!device || device->type == STORAGE_TYPE_NONE) {
        return false;
    }
    
    // Allocate buffer for superblock
    ext4_superblock_t* superblock = (ext4_superblock_t*)vmm_allocate(4096, VMM_FLAG_WRITABLE);
    if (!superblock) {
        EXT4_TRACE("Failed to allocate memory for superblock");
        return false;
    }
    
    // Read the superblock (located at byte offset 1024 in block 0)
    uint64_t sb_sector = EXT4_SUPERBLOCK_OFFSET / device->sector_size;
    uint32_t sector_count = (sizeof(ext4_superblock_t) + device->sector_size - 1) / device->sector_size;
    
    if (!device->read(device, sb_sector, superblock, sector_count)) {
        EXT4_TRACE("Failed to read superblock");
        vmm_free(superblock, 4096);
        return false;
    }
    
    // Check for Ext4 magic number
    bool is_ext4 = (superblock->s_magic == EXT4_SUPER_MAGIC);
    
    if (is_ext4) {
        EXT4_TRACE("Ext4 filesystem detected on device");
        EXT4_TRACE("  Volume: %s", superblock->s_volume_name);
        EXT4_TRACE("  Last mounted: %s", superblock->s_last_mounted);
        EXT4_TRACE("  Block size: %u bytes", 1024 << superblock->s_log_block_size);
        EXT4_TRACE("  Inode count: %u", superblock->s_inodes_count);
        EXT4_TRACE("  Block count: %llu", 
                 (uint64_t)superblock->s_blocks_count_lo | 
                 ((uint64_t)superblock->s_blocks_count_hi << 32));
    } else {
        EXT4_TRACE("No Ext4 filesystem detected on device (magic=0x%04x, expected=0x%04x)",
                 superblock->s_magic, EXT4_SUPER_MAGIC);
    }
    
    vmm_free(superblock, 4096);
    return is_ext4;
}

// Mount an Ext4 filesystem
bool ext4_mount(storage_device_t* device, ext4_fs_t** fs_out) {
    if (!device || !fs_out || device->type == STORAGE_TYPE_NONE) {
        return false;
    }
    
    // Allocate memory for filesystem structure
    ext4_fs_t* fs = (ext4_fs_t*)vmm_allocate(sizeof(ext4_fs_t), VMM_FLAG_WRITABLE);
    if (!fs) {
        EXT4_TRACE("Failed to allocate memory for filesystem structure");
        return false;
    }
    
    // Initialize filesystem structure
    memset(fs, 0, sizeof(ext4_fs_t));
    fs->device = device;
    spinlock_init(&fs->lock);
    
    // Read the superblock
    uint64_t sb_sector = EXT4_SUPERBLOCK_OFFSET / device->sector_size;
    uint32_t sector_count = (sizeof(ext4_superblock_t) + device->sector_size - 1) / device->sector_size;
    
    if (!device->read(device, sb_sector, &fs->superblock, sector_count)) {
        EXT4_TRACE("Failed to read superblock");
        vmm_free(fs, sizeof(ext4_fs_t));
        return false;
    }
    
    // Verify magic number
    if (fs->superblock.s_magic != EXT4_SUPER_MAGIC) {
        EXT4_TRACE("Invalid Ext4 magic number: 0x%04x", fs->superblock.s_magic);
        vmm_free(fs, sizeof(ext4_fs_t));
        return false;
    }
    
    // Calculate filesystem parameters
    fs->block_size = 1024 << fs->superblock.s_log_block_size;
    fs->block_count = (uint64_t)fs->superblock.s_blocks_count_lo | 
                     ((uint64_t)fs->superblock.s_blocks_count_hi << 32);
    fs->blocks_per_group = fs->superblock.s_blocks_per_group;
    fs->inodes_per_group = fs->superblock.s_inodes_per_group;
    fs->inode_size = fs->superblock.s_inode_size;
    
    // Calculate number of block groups
    fs->groups_count = (fs->block_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    
    // Allocate memory for group descriptors
    uint32_t group_desc_size = fs->groups_count * sizeof(ext4_group_desc_t);
    fs->group_desc = (ext4_group_desc_t*)vmm_allocate(group_desc_size, VMM_FLAG_WRITABLE);
    if (!fs->group_desc) {
        EXT4_TRACE("Failed to allocate memory for group descriptors");
        vmm_free(fs, sizeof(ext4_fs_t));
        return false;
    }
    
    // Determine group descriptor table location
    uint64_t gdt_block;
    if (fs->superblock.s_first_data_block == 0) {
        gdt_block = 1; // Superblock is in block 0
    } else {
        gdt_block = fs->superblock.s_first_data_block + 1;
    }
    
    // Read group descriptor table
    if (!read_blocks(fs, gdt_block, 
                    (group_desc_size + fs->block_size - 1) / fs->block_size, 
                    fs->group_desc)) {
        EXT4_TRACE("Failed to read group descriptor table");
        vmm_free(fs->group_desc, group_desc_size);
        vmm_free(fs, sizeof(ext4_fs_t));
        return false;
    }
    
    // Allocate block buffer
    fs->block_buffer = vmm_allocate(fs->block_size, VMM_FLAG_WRITABLE);
    if (!fs->block_buffer) {
        EXT4_TRACE("Failed to allocate block buffer");
        vmm_free(fs->group_desc, group_desc_size);
        vmm_free(fs, sizeof(ext4_fs_t));
        return false;
    }
    
    fs->block_buffer_block = (uint64_t)-1; // Invalid block number
    fs->block_buffer_dirty = false;
    
    EXT4_TRACE("Ext4 filesystem mounted successfully");
    EXT4_TRACE("  Volume: %s", fs->superblock.s_volume_name);
    EXT4_TRACE("  Block size: %u bytes", fs->block_size);
    EXT4_TRACE("  Block count: %lu", fs->block_count);
    EXT4_TRACE("  Groups: %u", fs->groups_count);
    EXT4_TRACE("  Inodes per group: %u", fs->inodes_per_group);
    
    *fs_out = fs;
    return true;
}

// Unmount an Ext4 filesystem
void ext4_unmount(ext4_fs_t* fs) {
    if (!fs) {
        return;
    }
    
    // Flush blocks if needed
    if (fs->block_buffer_dirty) {
        write_blocks(fs, fs->block_buffer_block, 1, fs->block_buffer);
    }
    
    // Flush device cache
    if (fs->device && fs->device->flush) {
        fs->device->flush(fs->device);
    }
    
    // Free allocated memory
    if (fs->block_buffer) {
        vmm_free(fs->block_buffer, fs->block_size);
    }
    
    if (fs->group_desc) {
        vmm_free(fs->group_desc, fs->groups_count * sizeof(ext4_group_desc_t));
    }
    
    vmm_free(fs, sizeof(ext4_fs_t));
}

// Read a block from the filesystem
bool ext4_read_block(ext4_fs_t* fs, uint64_t block_num, void* buffer) {
    if (!fs || !buffer || block_num >= fs->block_count) {
        return false;
    }
    
    // Check if the block is in the cache
    if (block_num == fs->block_buffer_block) {
        memcpy(buffer, fs->block_buffer, fs->block_size);
        return true;
    }
    
    // Flush cached block if it's dirty
    if (fs->block_buffer_dirty) {
        if (!write_blocks(fs, fs->block_buffer_block, 1, fs->block_buffer)) {
            return false;
        }
        fs->block_buffer_dirty = false;
    }
    
    // Read the block from the device
    if (!read_blocks(fs, block_num, 1, buffer)) {
        return false;
    }
    
    // Cache the block
    memcpy(fs->block_buffer, buffer, fs->block_size);
    fs->block_buffer_block = block_num;
    
    return true;
}

// Write a block to the filesystem
bool ext4_write_block(ext4_fs_t* fs, uint64_t block_num, const void* buffer) {
    if (!fs || !buffer || block_num >= fs->block_count) {
        return false;
    }
    
    // Update the block buffer
    memcpy(fs->block_buffer, buffer, fs->block_size);
    fs->block_buffer_block = block_num;
    fs->block_buffer_dirty = true;
    
    // Write through to the device
    return write_blocks(fs, block_num, 1, buffer);
}

// Flush any pending writes
bool ext4_flush_blocks(ext4_fs_t* fs) {
    if (!fs) {
        return false;
    }
    
    // Write cached block if it's dirty
    if (fs->block_buffer_dirty) {
        if (!write_blocks(fs, fs->block_buffer_block, 1, fs->block_buffer)) {
            return false;
        }
        fs->block_buffer_dirty = false;
    }
    
    // Flush device cache
    if (fs->device && fs->device->flush) {
        return fs->device->flush(fs->device);
    }
    
    return true;
}

// Read an inode from the filesystem
static bool read_inode(ext4_fs_t* fs, uint32_t inode_num, ext4_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) {
        return false;
    }
    
    // Adjust inode number (inodes start at 1)
    inode_num--;
    
    // Calculate block group
    uint32_t group = inode_num / fs->inodes_per_group;
    if (group >= fs->groups_count) {
        EXT4_TRACE("Inode group out of bounds: %u >= %u", group, fs->groups_count);
        return false;
    }
    
    // Calculate inode index within the group
    uint32_t index = inode_num % fs->inodes_per_group;
    
    // Get inode table block for this group
    uint64_t inode_table_block = 
        (uint64_t)fs->group_desc[group].bg_inode_table_lo |
        ((uint64_t)fs->group_desc[group].bg_inode_table_hi << 32);
    
    // Calculate block and offset within the block
    uint64_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint64_t inode_block = inode_table_block + block_offset;
    uint32_t offset = (index * fs->inode_size) % fs->block_size;
    
    // Allocate buffer for the block
    void* block_data = vmm_allocate(fs->block_size, VMM_FLAG_WRITABLE);
    if (!block_data) {
        EXT4_TRACE("Failed to allocate memory for inode block");
        return false;
    }
    
    // Read the block containing the inode
    if (!ext4_read_block(fs, inode_block, block_data)) {
        EXT4_TRACE("Failed to read inode block %lu", inode_block);
        vmm_free(block_data, fs->block_size);
        return false;
    }
    
    // Copy the inode data
    memcpy(inode, (uint8_t*)block_data + offset, sizeof(ext4_inode_t));
    
    vmm_free(block_data, fs->block_size);
    return true;
}

// Write an inode to the filesystem
static bool write_inode(ext4_fs_t* fs, uint32_t inode_num, const ext4_inode_t* inode) {
    if (!fs || !inode || inode_num == 0) {
        return false;
    }
    
    // Adjust inode number (inodes start at 1)
    inode_num--;
    
    // Calculate block group
    uint32_t group = inode_num / fs->inodes_per_group;
    if (group >= fs->groups_count) {
        EXT4_TRACE("Inode group out of bounds: %u >= %u", group, fs->groups_count);
        return false;
    }
    
    // Calculate inode index within the group
    uint32_t index = inode_num % fs->inodes_per_group;
    
    // Get inode table block for this group
    uint64_t inode_table_block = 
        (uint64_t)fs->group_desc[group].bg_inode_table_lo |
        ((uint64_t)fs->group_desc[group].bg_inode_table_hi << 32);
    
    // Calculate block and offset within the block
    uint64_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint64_t inode_block = inode_table_block + block_offset;
    uint32_t offset = (index * fs->inode_size) % fs->block_size;
    
    // Allocate buffer for the block
    void* block_data = vmm_allocate(fs->block_size, VMM_FLAG_WRITABLE);
    if (!block_data) {
        EXT4_TRACE("Failed to allocate memory for inode block");
        return false;
    }
    
    // Read the block containing the inode
    if (!ext4_read_block(fs, inode_block, block_data)) {
        EXT4_TRACE("Failed to read inode block %lu", inode_block);
        vmm_free(block_data, fs->block_size);
        return false;
    }
    
    // Update the inode data
    memcpy((uint8_t*)block_data + offset, inode, sizeof(ext4_inode_t));
    
    // Write the block back
    if (!ext4_write_block(fs, inode_block, block_data)) {
        EXT4_TRACE("Failed to write inode block %lu", inode_block);
        vmm_free(block_data, fs->block_size);
        return false;
    }
    
    vmm_free(block_data, fs->block_size);
    return true;
}

// Find an inode by path
static bool find_inode_by_path(ext4_fs_t* fs, const char* path, uint32_t* inode_num_out) {
    if (!fs || !path || !inode_num_out) {
        return false;
    }
    
    // Start at the root inode
    uint32_t current_inode = 2; // Root inode is always 2 in ext4
    
    // Handle root directory case
    if (path[0] == '/' && path[1] == '\0') {
        *inode_num_out = current_inode;
        return true;
    }
    
    // Skip leading slash
    if (path[0] == '/') {
        path++;
    }
    
    // Parse path components
    char component[256];
    int pos = 0;
    
    while (*path) {
        // Extract next path component
        pos = 0;
        while (*path && *path != '/') {
            if (pos < 255) {
                component[pos++] = *path;
            }
            path++;
        }
        component[pos] = '\0';
        
        // Skip empty components
        if (pos == 0) {
            if (*path == '/') path++;
            continue;
        }
        
        // Get the current inode data
        ext4_inode_t inode;
        if (!read_inode(fs, current_inode, &inode)) {
            return false;
        }
        
        // Verify it's a directory
        if ((inode.i_mode & 0xF000) != 0x4000) {
            return false; // Not a directory
        }
        
        // Check if directory uses extents
        bool has_extents = (inode.i_flags & EXT4_EXTENTS_FL) != 0;
        
        // Allocate buffer for directory data
        void* dir_data = vmm_allocate(fs->block_size, VMM_FLAG_WRITABLE);
        if (!dir_data) {
            return false;
        }
        
        bool found = false;
        
        if (has_extents) {
            // Handle directories with extents
            ext4_extent_header_t* header = (ext4_extent_header_t*)&inode.i_block[0];
            
            // Make sure it's a valid extent header
            if (header->eh_magic != 0xF30A) {
                vmm_free(dir_data, fs->block_size);
                return false;
            }
            
            // For simplicity, we only handle leaf nodes here
            if (header->eh_depth == 0 && header->eh_entries > 0) {
                ext4_extent_t* extents = (ext4_extent_t*)((uint8_t*)header + sizeof(ext4_extent_header_t));
                
                // Iterate through each extent
                for (uint16_t i = 0; i < header->eh_entries && !found; i++) {
                    uint64_t extent_block = 
                        (uint64_t)extents[i].ee_start_lo | 
                        ((uint64_t)extents[i].ee_start_hi << 32);
                    
                    // Iterate through blocks in this extent
                    for (uint16_t j = 0; j < extents[i].ee_len && !found; j++) {
                        if (!ext4_read_block(fs, extent_block + j, dir_data)) {
                            continue;
                        }
                        
                        // Scan directory entries
                        uint32_t offset = 0;
                        while (offset < fs->block_size) {
                            ext4_dir_entry_t* entry = (ext4_dir_entry_t*)((uint8_t*)dir_data + offset);
                            
                            // Check for end of entries
                            if (entry->rec_len == 0) {
                                break;
                            }
                            
                            // Check if this is the component we're looking for
                            if (entry->inode != 0 && entry->name_len == strlen(component)) {
                                if (memcmp(entry->name, component, entry->name_len) == 0) {
                                    current_inode = entry->inode;
                                    found = true;
                                    break;
                                }
                            }
                            
                            offset += entry->rec_len;
                        }
                    }
                }
            }
        } else {
            // Handle directories with direct blocks
            for (int i = 0; i < 12 && !found; i++) {
                if (inode.i_block[i] == 0) {
                    continue;
                }
                
                if (!ext4_read_block(fs, inode.i_block[i], dir_data)) {
                    continue;
                }
                
                // Scan directory entries
                uint32_t offset = 0;
                while (offset < fs->block_size) {
                    ext4_dir_entry_t* entry = (ext4_dir_entry_t*)((uint8_t*)dir_data + offset);
                    
                    // Check for end of entries
                    if (entry->rec_len == 0) {
                        break;
                    }
                    
                    // Check if this is the component we're looking for
                    if (entry->inode != 0 && entry->name_len == strlen(component)) {
                        if (memcmp(entry->name, component, entry->name_len) == 0) {
                            current_inode = entry->inode;
                            found = true;
                            break;
                        }
                    }
                    
                    offset += entry->rec_len;
                }
            }
            
            // TODO: Handle indirect blocks if needed
        }
        
        vmm_free(dir_data, fs->block_size);
        
        if (!found) {
            return false;
        }
        
        // Move to next path component
        if (*path == '/') path++;
    }
    
    *inode_num_out = current_inode;
    return true;
}

// Open a file
bool ext4_open(ext4_fs_t* fs, const char* path, ext4_file_t** file_out) {
    if (!fs || !path || !file_out) {
        return false;
    }
    
    // Find the inode for the path
    uint32_t inode_num;
    if (!find_inode_by_path(fs, path, &inode_num)) {
        EXT4_TRACE("File not found: %s", path);
        return false;
    }
    
    // Allocate file handle
    ext4_file_t* file = (ext4_file_t*)vmm_allocate(sizeof(ext4_file_t), VMM_FLAG_WRITABLE);
    if (!file) {
        EXT4_TRACE("Failed to allocate file handle");
        return false;
    }
    
    // Initialize file handle
    file->fs = fs;
    file->inode_num = inode_num;
    file->position = 0;
    spinlock_init(&file->lock);
    
    // Read the inode
    if (!read_inode(fs, inode_num, &file->inode)) {
        EXT4_TRACE("Failed to read inode %u", inode_num);
        vmm_free(file, sizeof(ext4_file_t));
        return false;
    }
    
    *file_out = file;
    return true;
}

// Close a file
void ext4_close(ext4_file_t* file) {
    if (!file) {
        return;
    }
    
    vmm_free(file, sizeof(ext4_file_t));
}

// Get file size
uint64_t ext4_size(ext4_file_t* file) {
    if (!file) {
        return 0;
    }
    
    // Calculate 64-bit size
    return (uint64_t)file->inode.i_size_lo | ((uint64_t)file->inode.i_size_high << 32);
}

// Initialize the Ext4 system
void ext4_init(void) {
    // Detect storage devices
    storage_detect_all_devices();
}