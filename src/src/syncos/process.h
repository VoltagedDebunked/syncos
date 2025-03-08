#ifndef _SYNCOS_PROCESS_H
#define _SYNCOS_PROCESS_H

#include <syncos/elf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Maximum number of processes in the system
#define PROCESS_MAX_COUNT 64

// Default stack size for processes (2MB)
#define PROCESS_DEFAULT_STACK_SIZE (2 * 1024 * 1024)

// Process states
typedef enum {
    PROCESS_STATE_NEW,         // Process created but not ready
    PROCESS_STATE_READY,       // Process ready to run
    PROCESS_STATE_RUNNING,     // Currently running process
    PROCESS_STATE_BLOCKED,     // Process blocked on I/O or other resources
    PROCESS_STATE_SUSPENDED,   // Process suspended by user/system
    PROCESS_STATE_TERMINATED   // Process has terminated
} process_state_t;

// Process control block
typedef struct process {
    uint32_t pid;              // Process ID
    uint32_t parent_pid;       // Parent process ID
    char name[32];             // Process name
    process_state_t state;     // Process state
    int exit_code;             // Exit code for terminated processes
    
    // CPU context for scheduling
    struct {
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi, rbp, rsp;
        uint64_t r8, r9, r10, r11;
        uint64_t r12, r13, r14, r15;
        uint64_t rip, rflags;
        uint64_t cs, ss, ds, es, fs, gs;
        uint64_t cr3;          // Page directory base
    } context;
    
    // Memory management
    uintptr_t page_table;      // Physical address of process page table
    void* stack_top;           // Virtual address of stack top
    size_t stack_size;         // Stack size
    
    // Memory regions - for virtual memory management
    struct {
        uint64_t start;        // Start virtual address
        uint64_t end;          // End virtual address
        uint32_t flags;        // Memory protection flags
        char name[16];         // Region name (for debugging)
    } memory_regions[16];
    int memory_region_count;
    
    // ELF executable information
    elf_context_t elf_ctx;     // ELF context for the executable
    uint64_t entry_point;      // Entry point address
    
    // Time accounting
    uint64_t start_time;       // Process start time (ticks)
    uint64_t cpu_time;         // CPU time used (ticks)
    uint64_t last_schedule;    // Last time scheduled (ticks)
    uint64_t quantum;          // Time quantum for this process
    
    // Priority information
    int base_priority;         // Base priority level
    int dynamic_priority;      // Dynamic priority (can change)
    
    // Links for queues
    struct process* next;      // Next process in queue
    struct process* prev;      // Previous process in queue
} process_t;

// Process creation parameters
typedef struct {
    const char* name;          // Process name
    uint32_t parent_pid;       // Parent process ID (0 for kernel)
    size_t stack_size;         // Stack size (0 for default)
    int priority;              // Initial priority
    uint64_t time_quantum;     // Time quantum in timer ticks (0 for default)
} process_params_t;

/**
 * Initialize the process manager
 * @return true if initialized successfully, false otherwise
 */
bool process_init(void);

/**
 * Shutdown the process manager
 */
void process_shutdown(void);

/**
 * Create a new process from an ELF executable in memory
 * @param elf_data Pointer to ELF executable data
 * @param elf_size Size of ELF data
 * @param params Process creation parameters
 * @return Process ID if successful, 0 otherwise
 */
uint32_t process_create(const void* elf_data, size_t elf_size, const process_params_t* params);

/**
 * Execute a loaded process
 * @param pid Process ID
 * @param argc Argument count
 * @param argv Argument vector
 * @param envp Environment pointer
 * @return true if successful, false otherwise
 */
bool process_execute(uint32_t pid, int argc, char* argv[], char* envp[]);

/**
 * Terminate a process
 * @param pid Process ID
 * @param exit_code Exit code
 * @return true if successful, false otherwise
 */
bool process_terminate(uint32_t pid, int exit_code);

/**
 * Get current process
 * @return Pointer to current process control block, NULL if no current process
 */
process_t* process_get_current(void);

/**
 * Get process by ID
 * @param pid Process ID
 * @return Pointer to process control block, NULL if not found
 */
process_t* process_get_by_id(uint32_t pid);

/**
 * Schedule the next process to run
 * This is called by the timer interrupt handler
 */
void process_schedule(void);

/**
 * Initialize the process scheduler
 * @return true if initialized successfully, false otherwise
 */
bool process_scheduler_init(void);

/**
 * Register the kernel thread as the idle process
 * @return true if successful, false otherwise
 */
bool process_register_kernel_idle(void);

/**
 * Yield the CPU to another process
 * This can be called by a process voluntarily
 */
void process_yield(void);

/**
 * Block the current process
 * @param state The state to set (must be a blocked state)
 */
void process_block(process_state_t state);

/**
 * Unblock a process
 * @param pid Process ID
 * @return true if successful, false otherwise
 */
bool process_unblock(uint32_t pid);

/**
 * Change process priority
 * @param pid Process ID
 * @param priority New priority
 * @return true if successful, false otherwise
 */
bool process_set_priority(uint32_t pid, int priority);

/**
 * Get process statistics
 * @param pid Process ID
 * @param cpu_time Pointer to store CPU time
 * @param state Pointer to store process state
 * @return true if successful, false otherwise
 */
bool process_get_stats(uint32_t pid, uint64_t* cpu_time, process_state_t* state);

/**
 * Get list of processes
 * @param pids Array to store process IDs
 * @param max_count Maximum number of processes to retrieve
 * @return Number of processes retrieved
 */
int process_get_list(uint32_t* pids, int max_count);

/**
 * Add debugging information about a memory region
 * @param pid Process ID (0 for current process)
 * @param start Start address of the region
 * @param end End address of the region
 * @param flags Memory protection flags
 * @param name Name of the region
 * @return true if successful, false otherwise
 */
bool process_add_memory_region(uint32_t pid, uint64_t start, uint64_t end, 
                             uint32_t flags, const char* name);

/**
 * Process initialization for architecture-specific code
 * Assembly code should call this after setting up the initial execution environment
 * @return true if successful, false otherwise
 */
bool process_arch_init(void);

/**
 * Save current CPU context for context switching
 * Called from assembly during context switch
 * @param context Pointer to context structure
 */
void process_save_context(void* context);

/**
 * Restore CPU context for context switching
 * Called from assembly during context switch
 * @param context Pointer to context structure
 */
void process_restore_context(void* context);

#endif // _SYNCOS_PROCESS_H