#include <syncos/process.h>
#include <syncos/elf.h>
#include <syncos/vmm.h>
#include <syncos/pmm.h>
#include <syncos/spinlock.h>
#include <syncos/timer.h>
#include <syncos/idt.h>
#include <kstd/string.h>
#include <kstd/stdio.h>

// Debug option
#define PROCESS_DEBUG 1
#ifdef PROCESS_DEBUG
#define PROCESS_LOG(fmt, ...) printf("PROCESS: " fmt "\n", ##__VA_ARGS__)
#else
#define PROCESS_LOG(fmt, ...)
#endif

// Default time quantum in timer ticks
#define DEFAULT_TIME_QUANTUM 20

// Process table
static process_t process_table[PROCESS_MAX_COUNT];
static uint32_t next_pid = 1;
static process_t* current_process = NULL;
static process_t* idle_process = NULL;

// Process queues
static process_t* ready_queue_head = NULL;
static process_t* ready_queue_tail = NULL;
static process_t* blocked_queue_head = NULL;

// Process table lock
static spinlock_t process_lock;

// Forward declarations for internal functions
static void context_switch(process_t* next);
static void schedule_next(void);
static void timer_callback(uint64_t tick_count, void* context);
static void init_process_context(process_t* process, uint64_t entry, uint64_t stack);
static void add_to_ready_queue(process_t* process);
static void remove_from_ready_queue(process_t* process);
static void add_to_blocked_queue(process_t* process);
static void remove_from_blocked_queue(process_t* process);
static void* process_alloc_pages(size_t page_count);
static void process_free_pages(void* addr, size_t page_count);
static uint32_t allocate_pid(void);
static uintptr_t create_process_address_space(void);
static void* create_process_stack(size_t stack_size, uintptr_t page_table);
static void free_process_resources(process_t* process);

// External assembly functions for context switching
extern void process_switch_context(uint64_t* old_ctx, uint64_t* new_ctx);
extern void process_enter_usermode(uint64_t entry, uint64_t stack, uint64_t page_table, 
                                  int argc, char** argv, char** envp);

// Memory allocation wrapper functions for ELF loader
static void* process_alloc_pages(size_t page_count) {
    uintptr_t phys = pmm_alloc_pages(page_count);
    return (void*)phys;
}

static void process_free_pages(void* addr, size_t page_count) {
    pmm_free_pages((uintptr_t)addr, page_count);
}

// Schedule the next process to run
static void schedule_next(void) {
    // Disable interrupts while scheduling
    idt_disable_interrupts();
    
    // Try to get next process from ready queue
    process_t* next = NULL;
    
    // Simple round-robin scheduler
    if (ready_queue_head) {
        next = ready_queue_head;
        ready_queue_head = ready_queue_head->next;
        
        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            ready_queue_tail = NULL;
        }
        
        next->next = NULL;
        next->prev = NULL;
    } else {
        // No ready processes, use idle process
        next = idle_process;
    }
    
    // Switch to the next process
    if (next) {
        context_switch(next);
    }
    
    // Interrupts will be re-enabled by the context switch
}

// Initialize the process manager
bool process_init(void) {
    PROCESS_LOG("Initializing process manager");
    
    // Initialize process table
    memset(process_table, 0, sizeof(process_table));
    
    // Initialize spinlock
    spinlock_init(&process_lock);
    spinlock_set_name(&process_lock, "process_lock");
    
    // Initialize process scheduler
    if (!process_scheduler_init()) {
        PROCESS_LOG("Failed to initialize process scheduler");
        return false;
    }
    
    PROCESS_LOG("Process manager initialized successfully");
    return true;
}

// Initialize the process scheduler
bool process_scheduler_init(void) {
    PROCESS_LOG("Initializing process scheduler");
    
    // Register timer callback for preemptive scheduling
    if (!timer_register_callback(timer_callback, NULL, 1)) {
        PROCESS_LOG("Failed to register timer callback");
        return false;
    }
    
    PROCESS_LOG("Process scheduler initialized successfully");
    return true;
}

// Register the kernel thread as the idle process
bool process_register_kernel_idle(void) {
    spinlock_acquire(&process_lock);
    
    // Create a process entry for the idle process
    process_t* idle = &process_table[0]; // Reserve slot 0 for idle
    memset(idle, 0, sizeof(process_t));
    
    idle->pid = 0;
    idle->state = PROCESS_STATE_READY;
    strncpy(idle->name, "kernel_idle", sizeof(idle->name) - 1);
    
    // The idle process already has a context (current kernel execution context)
    idle->context.cr3 = vmm_get_current_address_space();
    
    // Set up other fields
    idle->quantum = UINT64_MAX; // Idle process runs until another process is ready
    idle->base_priority = -20;  // Lowest priority
    idle->dynamic_priority = -20;
    
    // Set as idle process
    idle_process = idle;
    current_process = idle;
    
    spinlock_release(&process_lock);
    
    PROCESS_LOG("Registered kernel idle process (PID 0)");
    return true;
}

// Timer callback for process scheduling
static void timer_callback(uint64_t tick_count, void* context) {
    (void)context; // Unused
    
    // Check if interrupts are enabled and we're not in a critical section
    if (!idt_are_interrupts_enabled()) {
        return;
    }
    
    // Check if we need to schedule another process
    if (current_process && current_process->state == PROCESS_STATE_RUNNING) {
        current_process->cpu_time++;
        
        // If the process has used its time quantum, reschedule
        if (current_process->cpu_time - current_process->last_schedule >= current_process->quantum) {
            // Move current process back to ready queue
            spinlock_acquire(&process_lock);
            
            if (current_process != idle_process) {
                current_process->state = PROCESS_STATE_READY;
                add_to_ready_queue(current_process);
            }
            
            spinlock_release(&process_lock);
            
            // Schedule next process
            schedule_next();
        }
    } else {
        // No current process or process is not running, schedule next
        schedule_next();
    }
}

// Allocate a new process ID
static uint32_t allocate_pid(void) {
    // Simple PID allocation: increment and wrap around if needed
    uint32_t pid = next_pid++;
    
    // Skip 0 (reserved for idle)
    if (next_pid == 0) {
        next_pid = 1;
    }
    
    return pid;
}

// Create a new page table for a process
static uintptr_t create_process_address_space(void) {
    // Create a new page table (returns physical address)
    uintptr_t page_table = vmm_create_address_space();
    
    if (page_table == 0) {
        PROCESS_LOG("Failed to create process address space");
        return 0;
    }
    
    return page_table;
}

// Create a stack for a process
static void* create_process_stack(size_t stack_size, uintptr_t page_table) {
    // Align stack size to page boundary
    stack_size = (stack_size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    
    // Allocate physical memory for the stack
    size_t page_count = stack_size / PAGE_SIZE_4K;
    uintptr_t stack_phys = pmm_alloc_pages(page_count);
    if (stack_phys == 0) {
        PROCESS_LOG("Failed to allocate physical memory for process stack");
        return NULL;
    }
    
    // Define a virtual address for the stack (just below 2GB marker for user space)
    uint64_t stack_virt = 0x00000000EFFFF000ULL - stack_size + PAGE_SIZE_4K;
    
    // Save current address space
    uintptr_t old_cr3 = vmm_get_current_address_space();
    
    // Switch to process address space
    vmm_switch_address_space(page_table);
    
    // Map stack pages into process address space
    for (size_t i = 0; i < stack_size; i += PAGE_SIZE_4K) {
        if (!vmm_map_page(stack_virt + i, stack_phys + i, 
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
            PROCESS_LOG("Failed to map process stack page at 0x%lx", stack_virt + i);
            
            // Clean up already mapped pages
            for (size_t j = 0; j < i; j += PAGE_SIZE_4K) {
                vmm_unmap_page(stack_virt + j);
            }
            
            // Switch back to original address space
            vmm_switch_address_space(old_cr3);
            
            // Free physical memory
            pmm_free_pages(stack_phys, page_count);
            
            return NULL;
        }
    }
    
    // Add a guard page below the stack
    uint64_t guard_virt = stack_virt - PAGE_SIZE_4K;
    if (!vmm_map_page(guard_virt, 0, VMM_FLAG_PRESENT | VMM_FLAG_NO_EXECUTE)) {
        PROCESS_LOG("Warning: Failed to create stack guard page");
        // Continue anyway, this is just a protection feature
    }
    
    // Switch back to original address space
    vmm_switch_address_space(old_cr3);
    
    // Return the stack top (stacks grow downward)
    return (void*)(stack_virt + stack_size);
}

// Initialize a process's CPU context
static void init_process_context(process_t* process, uint64_t entry, uint64_t stack) {
    // Clear the context
    memset(&process->context, 0, sizeof(process->context));
    
    // Set up the initial CPU context for the process
    process->context.rip = entry;
    process->context.rsp = stack;
    process->context.rflags = 0x202; // IF flag set (interrupts enabled)
    
    // Set up segment registers for user mode
    process->context.cs = 0x1B | 3; // User code segment with RPL=3
    process->context.ss = 0x23 | 3; // User data segment with RPL=3
    process->context.ds = 0x23 | 3;
    process->context.es = 0x23 | 3;
    process->context.fs = 0x23 | 3;
    process->context.gs = 0x23 | 3;
    
    // Set page table
    process->context.cr3 = process->page_table;
}

// Create a new process from an ELF executable
uint32_t process_create(const void* elf_data, size_t elf_size, const process_params_t* params) {
    if (!elf_data || elf_size == 0 || !params || !params->name) {
        PROCESS_LOG("Invalid parameters for process creation");
        return 0;
    }
    
    PROCESS_LOG("Creating process '%s'", params->name);
    
    // Check if ELF format is valid
    if (!elf_is_valid(elf_data, elf_size)) {
        PROCESS_LOG("Invalid ELF format");
        return 0;
    }
    
    spinlock_acquire(&process_lock);
    
    // Find a free process table entry
    process_t* process = NULL;
    for (size_t i = 1; i < PROCESS_MAX_COUNT; i++) { // Skip index 0 (idle)
        if (process_table[i].state == PROCESS_STATE_TERMINATED || 
            process_table[i].pid == 0) {
            process = &process_table[i];
            break;
        }
    }
    
    if (!process) {
        PROCESS_LOG("Process table full");
        spinlock_release(&process_lock);
        return 0;
    }
    
    // Clear the process entry
    memset(process, 0, sizeof(process_t));
    
    // Allocate PID
    process->pid = allocate_pid();
    process->parent_pid = params->parent_pid;
    
    // Set process name
    strncpy(process->name, params->name, sizeof(process->name) - 1);
    
    // Set process state
    process->state = PROCESS_STATE_NEW;
    
    // Set up priority and time quantum
    process->base_priority = params->priority;
    process->dynamic_priority = process->base_priority;
    process->quantum = params->time_quantum ? params->time_quantum : DEFAULT_TIME_QUANTUM;
    
    // Create process address space
    process->page_table = create_process_address_space();
    if (process->page_table == 0) {
        PROCESS_LOG("Failed to create process address space");
        spinlock_release(&process_lock);
        return 0;
    }
    
    // Create process stack
    size_t stack_size = params->stack_size ? params->stack_size : PROCESS_DEFAULT_STACK_SIZE;
    process->stack_top = create_process_stack(stack_size, process->page_table);
    process->stack_size = stack_size;
    
    if (!process->stack_top) {
        PROCESS_LOG("Failed to create process stack");
        vmm_delete_address_space(process->page_table);
        process->pid = 0; // Mark as free
        spinlock_release(&process_lock);
        return 0;
    }
    
    // Initialize ELF context
    if (!elf_init(&process->elf_ctx, elf_data, elf_size, 
                 process_alloc_pages, process_free_pages)) {
        PROCESS_LOG("Failed to initialize ELF context");
        
        // Clean up resources
        vmm_delete_address_space(process->page_table);
        process->pid = 0; // Mark as free
        spinlock_release(&process_lock);
        return 0;
    }
    
    // Save current address space
    uintptr_t old_cr3 = vmm_get_current_address_space();
    
    // Switch to process address space
    vmm_switch_address_space(process->page_table);
    
    // Load ELF segments
    if (!elf_load(&process->elf_ctx, 0)) {
        PROCESS_LOG("Failed to load ELF segments");
        
        // Switch back to original address space
        vmm_switch_address_space(old_cr3);
        
        // Clean up resources
        elf_cleanup(&process->elf_ctx);
        vmm_delete_address_space(process->page_table);
        process->pid = 0; // Mark as free
        
        spinlock_release(&process_lock);
        return 0;
    }
    
    // Get entry point
    process->entry_point = elf_get_entry_point(&process->elf_ctx);
    PROCESS_LOG("Process entry point: 0x%lx", process->entry_point);
    
    // Initialize process context
    init_process_context(process, process->entry_point, (uint64_t)process->stack_top);
    
    // Switch back to original address space
    vmm_switch_address_space(old_cr3);
    
    // Set process as ready
    process->state = PROCESS_STATE_READY;
    
    // Add to ready queue
    add_to_ready_queue(process);
    
    // Set start time
    process->start_time = timer_get_ticks();
    
    PROCESS_LOG("Created process '%s' (PID %u)", process->name, process->pid);
    
    spinlock_release(&process_lock);
    
    return process->pid;
}

// Execute a loaded process
bool process_execute(uint32_t pid, int argc, char* argv[], char* envp[]) {
    spinlock_acquire(&process_lock);
    
    // Find the process
    process_t* process = process_get_by_id(pid);
    if (!process || process->state != PROCESS_STATE_READY) {
        spinlock_release(&process_lock);
        return false;
    }
    
    PROCESS_LOG("Executing process '%s' (PID %u)", process->name, process->pid);
    
    // Switch to process address space
    uintptr_t old_cr3 = vmm_get_current_address_space();
    vmm_switch_address_space(process->page_table);
    
    // Pass argv and envp into user space
    // TODO: Implement argv and envp passing when process loader is enhanced
    
    // Jump to entry point (this doesn't return)
    process_enter_usermode(process->entry_point, (uint64_t)process->stack_top, 
                          process->page_table, argc, argv, envp);
    
    // Should never reach here
    vmm_switch_address_space(old_cr3);
    spinlock_release(&process_lock);
    
    return true;
}

// Terminate a process
bool process_terminate(uint32_t pid, int exit_code) {
    if (pid == 0) {
        return false; // Can't terminate idle process
    }
    
    spinlock_acquire(&process_lock);
    
    // Find the process
    process_t* process = process_get_by_id(pid);
    if (!process) {
        spinlock_release(&process_lock);
        return false;
    }
    
    PROCESS_LOG("Terminating process '%s' (PID %u) with exit code %d",
               process->name, process->pid, exit_code);
    
    // Remove from queues
    if (process->state == PROCESS_STATE_READY) {
        remove_from_ready_queue(process);
    } else if (process->state == PROCESS_STATE_BLOCKED) {
        remove_from_blocked_queue(process);
    }
    
    // Set exit code and state
    process->exit_code = exit_code;
    process->state = PROCESS_STATE_TERMINATED;
    
    // Free resources
    free_process_resources(process);
    
    // If this was the current process, schedule another
    if (current_process == process) {
        current_process = NULL;
        spinlock_release(&process_lock);
        schedule_next();
    } else {
        spinlock_release(&process_lock);
    }
    
    return true;
}

// Add a process to the ready queue
static void add_to_ready_queue(process_t* process) {
    if (!process) return;
    
    // Initialize links
    process->next = NULL;
    
    if (!ready_queue_head) {
        // Queue is empty
        ready_queue_head = process;
        ready_queue_tail = process;
        process->prev = NULL;
    } else {
        // Add to end of queue
        ready_queue_tail->next = process;
        process->prev = ready_queue_tail;
        ready_queue_tail = process;
    }
    
    process->state = PROCESS_STATE_READY;
}

// Remove a process from the ready queue
static void remove_from_ready_queue(process_t* process) {
    if (!process) return;
    
    if (process == ready_queue_head) {
        // Process is at head of queue
        ready_queue_head = process->next;
        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            // Queue is now empty
            ready_queue_tail = NULL;
        }
    } else if (process == ready_queue_tail) {
        // Process is at tail of queue
        ready_queue_tail = process->prev;
        if (ready_queue_tail) {
            ready_queue_tail->next = NULL;
        } else {
            // Queue is now empty
            ready_queue_head = NULL;
        }
    } else {
        // Process is in middle of queue
        if (process->prev) {
            process->prev->next = process->next;
        }
        if (process->next) {
            process->next->prev = process->prev;
        }
    }
    
    process->next = NULL;
    process->prev = NULL;
}

// Add a process to the blocked queue
static void add_to_blocked_queue(process_t* process) {
    if (!process) return;
    
    // Simple linked list implementation (no need for doubly-linked here)
    process->next = blocked_queue_head;
    blocked_queue_head = process;
    
    process->state = PROCESS_STATE_BLOCKED;
}

// Remove a process from the blocked queue
static void remove_from_blocked_queue(process_t* process) {
    if (!process) return;
    
    process_t* current = blocked_queue_head;
    process_t* prev = NULL;
    
    while (current) {
        if (current == process) {
            if (prev) {
                prev->next = current->next;
            } else {
                blocked_queue_head = current->next;
            }
            process->next = NULL;
            break;
        }
        
        prev = current;
        current = current->next;
    }
}

// Free process resources
static void free_process_resources(process_t* process) {
    if (!process) return;
    
    // Clean up ELF context
    elf_cleanup(&process->elf_ctx);
    
    // Free page table (if any)
    if (process->page_table) {
        vmm_delete_address_space(process->page_table);
        process->page_table = 0;
    }
    
    // Zero out memory regions
    process->memory_region_count = 0;
    
    // Process state & struct itself remains in the process table
    // They'll be reused when a new process is created
}

// Context switch to another process
static void context_switch(process_t* next) {
    if (!next || next == current_process) {
        return;
    }
    
    process_t* prev = current_process;
    current_process = next;
    
    // Update process states
    if (prev) {
        if (prev->state == PROCESS_STATE_RUNNING) {
            prev->state = PROCESS_STATE_READY;
        }
    }
    
    next->state = PROCESS_STATE_RUNNING;
    next->last_schedule = next->cpu_time;
    
    // Perform the actual context switch
    if (prev) {
        // Save current context and switch to new one
        vmm_switch_address_space(next->page_table);
        process_switch_context((uint64_t*)&prev->context, (uint64_t*)&next->context);
    } else {
        // No previous context, just restore new one
        vmm_switch_address_space(next->page_table);
        process_restore_context((uint64_t*)&next->context);
    }
}

// Set up simple idle process
bool process_arch_init(void) {
    PROCESS_LOG("Initializing process architecture");

    // Nothing needed for x86_64 if we use the bootstrap context
    return true;
}

// Save context for switching (assembly implementation)
void process_save_context(void* context) {
    // Implemented in assembly
    __asm__ volatile(
        "movq %%rax, 0(%0)\n"
        "movq %%rbx, 8(%0)\n"
        "movq %%rcx, 16(%0)\n"
        "movq %%rdx, 24(%0)\n"
        "movq %%rsi, 32(%0)\n"
        "movq %%rdi, 40(%0)\n"
        "movq %%rbp, 48(%0)\n"
        "movq %%rsp, 56(%0)\n"
        "movq %%r8,  64(%0)\n"
        "movq %%r9,  72(%0)\n"
        "movq %%r10, 80(%0)\n"
        "movq %%r11, 88(%0)\n"
        "movq %%r12, 96(%0)\n"
        "movq %%r13, 104(%0)\n"
        "movq %%r14, 112(%0)\n"
        "movq %%r15, 120(%0)\n"
        "pushfq\n"
        "popq 128(%0)\n"
        :
        : "r"(context)
        : "memory"
    );
}

// Restore context for switching (assembly implementation)
void process_restore_context(void* context) {
    // Implemented in assembly
    __asm__ volatile(
        "movq 0(%0),   %%rax\n"
        "movq 8(%0),   %%rbx\n"
        "movq 16(%0),  %%rcx\n"
        "movq 24(%0),  %%rdx\n"
        "movq 32(%0),  %%rsi\n"
        "movq 40(%0),  %%rdi\n"
        "movq 48(%0),  %%rbp\n"
        "movq 56(%0),  %%rsp\n"
        "movq 64(%0),  %%r8\n"
        "movq 72(%0),  %%r9\n"
        "movq 80(%0),  %%r10\n"
        "movq 88(%0),  %%r11\n"
        "movq 96(%0),  %%r12\n"
        "movq 104(%0), %%r13\n"
        "movq 112(%0), %%r14\n"
        "movq 120(%0), %%r15\n"
        "pushq 128(%0)\n"
        "popfq\n"
        :
        : "r"(context)
        : "memory"
    );
}

// Get current process
process_t* process_get_current(void) {
    return current_process;
}

// Get process by ID
process_t* process_get_by_id(uint32_t pid) {
    if (pid >= PROCESS_MAX_COUNT) {
        return NULL;
    }
    
    process_t* process = &process_table[pid];
    return process->pid == pid ? process : NULL;
}

// Yield CPU to another process
void process_yield(void) {
    // Disable interrupts while modifying the scheduler state
    idt_disable_interrupts();
    
    if (current_process) {
        // Add current process back to ready queue
        current_process->state = PROCESS_STATE_READY;
        add_to_ready_queue(current_process);
    }
    
    // Schedule the next process
    schedule_next();
    
    // Interrupts will be re-enabled by the context switch
}

// Block the current process
void process_block(process_state_t state) {
    if (!current_process || state == PROCESS_STATE_RUNNING) {
        return;
    }
    
    // Disable interrupts while modifying the scheduler state
    idt_disable_interrupts();
    
    // Set the process state and add to blocked queue
    current_process->state = state;
    add_to_blocked_queue(current_process);
    
    // Schedule the next process
    schedule_next();
    
    // Interrupts will be re-enabled by the context switch
}

// Unblock a process
bool process_unblock(uint32_t pid) {
    spinlock_acquire(&process_lock);
    
    process_t* process = process_get_by_id(pid);
    if (!process || process->state != PROCESS_STATE_BLOCKED) {
        spinlock_release(&process_lock);
        return false;
    }
    
    // Remove from blocked queue and add to ready queue
    remove_from_blocked_queue(process);
    process->state = PROCESS_STATE_READY;
    add_to_ready_queue(process);
    
    spinlock_release(&process_lock);
    return true;
}

// Change process priority
bool process_set_priority(uint32_t pid, int priority) {
    spinlock_acquire(&process_lock);
    
    process_t* process = process_get_by_id(pid);
    if (!process) {
        spinlock_release(&process_lock);
        return false;
    }
    
    process->base_priority = priority;
    process->dynamic_priority = priority;
    
    spinlock_release(&process_lock);
    return true;
}

// Get process statistics
bool process_get_stats(uint32_t pid, uint64_t* cpu_time, process_state_t* state) {
    spinlock_acquire(&process_lock);
    
    process_t* process = process_get_by_id(pid);
    if (!process) {
        spinlock_release(&process_lock);
        return false;
    }
    
    if (cpu_time) {
        *cpu_time = process->cpu_time;
    }
    
    if (state) {
        *state = process->state;
    }
    
    spinlock_release(&process_lock);
    return true;
}

// Get list of processes
int process_get_list(uint32_t* pids, int max_count) {
    if (!pids || max_count <= 0) {
        return 0;
    }
    
    spinlock_acquire(&process_lock);
    
    int count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT && count < max_count; i++) {
        if (process_table[i].pid != 0) {
            pids[count++] = process_table[i].pid;
        }
    }
    
    spinlock_release(&process_lock);
    return count;
}

// Add memory region info
bool process_add_memory_region(uint32_t pid, uint64_t start, uint64_t end, 
                             uint32_t flags, const char* name) {
    process_t* process;
    
    if (pid == 0) {
        process = current_process;
    } else {
        process = process_get_by_id(pid);
    }
    
    if (!process || !name || start >= end) {
        return false;
    }
    
    spinlock_acquire(&process_lock);
    
    // Find empty region slot
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (process->memory_regions[i].end == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        spinlock_release(&process_lock);
        return false;
    }
    
    // Fill region info
    process->memory_regions[slot].start = start;
    process->memory_regions[slot].end = end;
    process->memory_regions[slot].flags = flags;
    strncpy(process->memory_regions[slot].name, name, 15);
    process->memory_regions[slot].name[15] = '\0';
    
    if (process->memory_region_count <= slot) {
        process->memory_region_count = slot + 1;
    }
    
    spinlock_release(&process_lock);
    return true;
}