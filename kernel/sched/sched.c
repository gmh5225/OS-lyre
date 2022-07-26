#include <stdbool.h>
#include <stdnoreturn.h>
#include <lib/errno.h>
#include <lib/print.h>
#include <lib/misc.h>
#include <lib/alloc.h>
#include <lib/vector.h>
#include <lib/resource.h>
#include <sched/sched.h>
#include <sys/timer.h>
#include <sys/cpu.h>
#include <mm/mmap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <fs/vfs/vfs.h>

struct process *kernel_process;

static struct thread *running_queue[MAX_RUNNING_THREADS];
static size_t running_queue_i = 0;

// This is a very crappy algorithm
static int get_next_thread(int orig_i) {
    int cpu_number = this_cpu()->cpu_number;

    size_t index = orig_i + 1;

    for (;;) {
        if (index >= running_queue_i) {
            index = 0;
        }

        struct thread *thread = running_queue[index];

        if (thread != NULL) {
            if (thread->running_on == cpu_number || spinlock_test_and_acq(&thread->lock) == true) {
                return index;
            }
        }

        if (index == (size_t)orig_i) {
            break;
        }

        index++;
    }

    return -1;
}

#if defined (__x86_64__)

static noreturn void thread_spinup(struct cpu_ctx *ctx) {
    asm volatile (
        "mov %0, %%rsp\n\t"
        "pop %%rax\n\t"
        "mov %%eax, %%ds\n\t"
        "pop %%rax\n\t"
        "mov %%eax, %%es\n\t"
        "pop %%rax\n\t"
        "pop %%rbx\n\t"
        "pop %%rcx\n\t"
        "pop %%rdx\n\t"
        "pop %%rsi\n\t"
        "pop %%rdi\n\t"
        "pop %%rbp\n\t"
        "pop %%r8\n\t"
        "pop %%r9\n\t"
        "pop %%r10\n\t"
        "pop %%r11\n\t"
        "pop %%r12\n\t"
        "pop %%r13\n\t"
        "pop %%r14\n\t"
        "pop %%r15\n\t"
        "add $8, %%rsp\n\t"
        "swapgs\n\t"
        "iretq\n\t"
        :
        : "rm" (ctx)
        : "memory"
    );
    __builtin_unreachable();
}

#endif

static void sched_entry(int vector, struct cpu_ctx *ctx) {
    (void)vector;

    struct cpu_local *cpu = this_cpu();

    cpu->active = true;

    struct thread *current_thread = sched_current_thread();

    int new_index = get_next_thread(cpu->last_run_queue_index);

    if (current_thread != cpu->idle_thread) {
        spinlock_release(&current_thread->yield_await);

        if (new_index == cpu->last_run_queue_index) {
            sys_timer_oneshot(current_thread->timeslice, sched_entry);
            return;
        }

        current_thread->ctx = *ctx;

#if defined (__x86_64__)
        current_thread->gs_base = get_kernel_gs_base();
        current_thread->fs_base = get_fs_base();
        current_thread->cr3 = read_cr3();
        fpu_save(current_thread->fpu_storage);
#endif

        current_thread->running_on = -1;
        spinlock_release(&current_thread->lock);
    }

    if (new_index == -1) {
#if defined (__x86_64__)
        set_gs_base(cpu->idle_thread);
        set_kernel_gs_base(cpu->idle_thread);
#endif
        cpu->last_run_queue_index = 0;
        cpu->active = false;
        sched_await();
    }

    current_thread = running_queue[new_index];
    cpu->last_run_queue_index = new_index;

#if defined (__x86_64__)
    set_gs_base(current_thread);
    if (current_thread->ctx.cs == 0x4b) {
        set_kernel_gs_base(current_thread->gs_base);
    } else {
        set_kernel_gs_base(current_thread);
    }
    set_fs_base(current_thread->fs_base);

    wrmsr(0x175, (uint64_t)current_thread->kernel_stack);

    cpu->tss.ist2 = (uint64_t)current_thread->pf_stack;

    if (read_cr3() != current_thread->cr3) {
        write_cr3(current_thread->cr3);
    }

    fpu_restore(current_thread->fpu_storage);
#endif

    current_thread->running_on = cpu->cpu_number;
    current_thread->this_cpu = cpu;

    sys_timer_oneshot(current_thread->timeslice, sched_entry);

    struct cpu_ctx *new_ctx = &current_thread->ctx;

    thread_spinup(new_ctx);
}

noreturn void sched_await(void) {
    interrupt_toggle(false);
    sys_timer_oneshot(20000, sched_entry);
    interrupt_toggle(true);
    for (;;) {
        halt();
    }
    __builtin_unreachable();
}

void sched_yield(bool save_ctx) {
    interrupt_toggle(false);

    struct thread *thread = sched_current_thread();

    if (save_ctx) {
        spinlock_acquire(&thread->yield_await);
    }

    sys_timer_oneshot(1, sched_entry);
    interrupt_toggle(true);

    if (save_ctx) {
        spinlock_acquire(&thread->yield_await);
        spinlock_release(&thread->yield_await);
    } else {
        for (;;) {
            halt();
        }
    }
}

bool sched_enqueue_thread(struct thread *thread, bool by_signal) {
    if (thread->enqueued == true) {
        return true;
    }

    thread->enqueued_by_signal = by_signal;

    for (size_t i = 0; i < MAX_RUNNING_THREADS; i++) {
        if (CAS(&running_queue[i], NULL, thread)) {
            thread->enqueued = true;

            // TODO cpu wakey wakey thing

            if (i >= running_queue_i) {
                running_queue_i = i + 1;
            }

            return true;
        }
    }

    return false;
}

bool sched_dequeue_thread(struct thread *thread) {
    if (!thread->enqueued) {
        return true;
    }

    for (size_t i = 0; i < running_queue_i; i++) {
        if (CAS(&running_queue[i], thread, NULL)) {
            thread->enqueued = false;
            return true;
        }
    }
    return false;
}

noreturn void sched_dequeue_and_die(void) {
    interrupt_toggle(false);

    struct thread *thread = sched_current_thread();

    sched_dequeue_thread(thread);

    // TODO: Free stacks

    sched_yield(false);
    __builtin_unreachable();
}

struct process *sched_new_process(struct process *old_proc, struct pagemap *pagemap) {
    struct process *new_proc = ALLOC(struct process);
    if (new_proc == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    new_proc->threads = (typeof(new_proc->threads))VECTOR_INIT;

    if (old_proc != NULL) {
        new_proc->pagemap = vmm_fork_pagemap(old_proc->pagemap);
        if (new_proc->pagemap == NULL) {
            goto cleanup;
        }

        new_proc->thread_stack_top = old_proc->thread_stack_top;
        new_proc->mmap_anon_base = old_proc->mmap_anon_base;
        new_proc->cwd = old_proc->cwd;
    } else {
        new_proc->pagemap = pagemap;
        new_proc->thread_stack_top = 0x70000000000;
        new_proc->mmap_anon_base = 0x80000000000;
        new_proc->cwd = vfs_root;
    }

    struct vfs_node *dev_tty1 = vfs_get_node(vfs_root, "/dev/console", true);

    fdnum_create_from_resource(new_proc, dev_tty1->resource, 0, 0, true);
    fdnum_create_from_resource(new_proc, dev_tty1->resource, 0, 1, true);
    fdnum_create_from_resource(new_proc, dev_tty1->resource, 0, 2, true);

    return new_proc;

cleanup:
    if (new_proc != NULL) {
        free(new_proc);
    }
    return NULL;
}

#define STACK_SIZE 0x40000

struct thread *sched_new_kernel_thread(void *pc, void *arg, bool enqueue) {
    struct thread *thread = ALLOC(struct thread);

    thread->lock = SPINLOCK_INIT;
    thread->yield_await = SPINLOCK_INIT;
    thread->stacks = (typeof(thread->stacks))VECTOR_INIT;

    void *stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    VECTOR_PUSH_BACK(thread->stacks, stack_phys);
    void *stack = stack_phys + STACK_SIZE + VMM_HIGHER_HALF;

#if defined (__x86_64__)
    thread->ctx.cs = 0x28;
    thread->ctx.ds = thread->ctx.es = thread->ctx.ss = 0x30;
    thread->ctx.rflags = 0x202;
    thread->ctx.rip = (uint64_t)pc;
    thread->ctx.rdi = (uint64_t)arg;
    thread->ctx.rsp = (uint64_t)stack;

    thread->cr3 = (uint64_t)kernel_process->pagemap->top_level - VMM_HIGHER_HALF;
    thread->gs_base = thread;
#endif

    thread->process = kernel_process;
    thread->timeslice = 5000;
    thread->running_on = -1;
    thread->fpu_storage = pmm_alloc(DIV_ROUNDUP(fpu_storage_size, PAGE_SIZE))
                        + VMM_HIGHER_HALF;
    thread->self = thread;

    if (enqueue) {
        sched_enqueue_thread(thread, false);
    }

    return thread;
}

struct thread *sched_new_user_thread(struct process *proc, void *pc, void *arg, void *sp,
                                     const char **argv, const char **envp, struct auxval *auxval, bool enqueue) {
    struct thread *thread = ALLOC(struct thread);
    if (thread == NULL) {
        errno = ENOMEM;
        goto fail;
    }

    thread->lock = SPINLOCK_INIT;
    thread->yield_await = SPINLOCK_INIT;
    thread->enqueued = false;
    thread->stacks = (typeof(thread->stacks))VECTOR_INIT;

    uintptr_t *stack, *stack_vma;
    if (sp == NULL) {
        void *stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
        if (stack_phys == NULL) {
            errno = ENOMEM;
            goto fail;
        }

        stack = stack_phys + STACK_SIZE + VMM_HIGHER_HALF;
        stack_vma = (void *)proc->thread_stack_top;
        if (!mmap_range(proc->pagemap, proc->thread_stack_top - STACK_SIZE, (uintptr_t)stack_phys,
                        STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS)) {
            pmm_free(stack_phys, STACK_SIZE / PAGE_SIZE);
            goto fail;
        }

        proc->thread_stack_top -= STACK_SIZE - PAGE_SIZE;
    } else {
        stack = sp;
        stack_vma = sp;
    }

    void *kernel_stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    VECTOR_PUSH_BACK(thread->stacks, kernel_stack_phys);
    thread->kernel_stack = kernel_stack_phys + STACK_SIZE + VMM_HIGHER_HALF;

    void *pf_stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    VECTOR_PUSH_BACK(thread->stacks, pf_stack_phys);
    thread->pf_stack = kernel_stack_phys + STACK_SIZE + VMM_HIGHER_HALF;

#if defined (__x86_64__)
    thread->ctx.cs = 0x4b;
    thread->ctx.ds = thread->ctx.es = thread->ctx.ss = 0x53;
    thread->ctx.rflags = 0x202;
    thread->ctx.rip = (uint64_t)pc;
    thread->ctx.rdi = (uint64_t)arg;
    thread->ctx.rsp = (uint64_t)stack_vma;
    thread->cr3 = (uint64_t)proc->pagemap->top_level - VMM_HIGHER_HALF;
#endif

    thread->self = thread;
    thread->process = proc;
    thread->timeslice = 5000;
    thread->running_on = -1;
    thread->fpu_storage = pmm_alloc(DIV_ROUNDUP(fpu_storage_size, PAGE_SIZE))
                          + VMM_HIGHER_HALF;

    if (proc->threads.length == 0) {
        void *stack_top = stack;

        int envp_len;
        for (envp_len = 0; envp[envp_len] != NULL; envp_len++) {
            size_t length = strlen(envp[envp_len]);
            stack = (void *)stack - length - 1;
            memcpy(stack, envp[envp_len], length);
        }

        int argv_len;
        for (argv_len = 0; argv[argv_len] != NULL; argv_len++) {
            size_t length = strlen(argv[argv_len]);
            stack = (void *)stack - length - 1;
            memcpy(stack, argv[argv_len], length);
        }

        stack = (uintptr_t *)ALIGN_DOWN((uintptr_t)stack, 16);
        if (((argv_len + envp_len + 1) & 1) != 0) {
            stack--;
        }

        // Auxilary vector
        *(--stack) = 0, *(--stack) = 0;
        stack -= 2; stack[0] = AT_ENTRY, stack[1] = auxval->at_entry;
        stack -= 2; stack[0] = AT_PHDR,  stack[1] = auxval->at_phdr;
        stack -= 2; stack[0] = AT_PHENT, stack[1] = auxval->at_phent;
        stack -= 2; stack[0] = AT_PHNUM, stack[1] = auxval->at_phnum;

        uintptr_t old_rsp = thread->ctx.rsp;

        // Environment variables
        *(--stack) = 0;
        stack -= envp_len;
        for (int i = 0; i < envp_len; i++) {
            old_rsp -= strlen(envp[i]) + 1;
            stack[i] = old_rsp;
        }

        // Arguments
        *(--stack) = 0;
        stack -= argv_len;
        for (int i = 0; i < argv_len; i++) {
            old_rsp -= strlen(argv[i]) + 1;
            stack[i] = old_rsp;
        }

        *(--stack) = argv_len;

        thread->ctx.rsp -= stack_top - (void *)stack;
    }

    VECTOR_PUSH_BACK(proc->threads, thread);

    if (enqueue) {
        sched_enqueue_thread(thread, false);
    }

    return thread;

fail:
    if (thread != NULL) {
        free(thread);
    }
    return NULL;
}

void syscall_set_fs_base(void *_, void *base) {
    (void)_;
    set_fs_base(base);
}

void syscall_set_gs_base(void *_, void *base) {
    (void)_;
    set_gs_base(base);
}

int syscall_fork(struct cpu_ctx *ctx) {
    print("syscall: fork()");

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;
    struct process *new_proc = sched_new_process(proc, NULL);

    for (int i = 0; i < MAX_FDS; i++) {
        if (proc->fds[i] == NULL) {
            continue;
        }

        if (fdnum_dup(proc, i, new_proc, i, 0, true, false) != i) {
            goto fail;
        }
    }

    struct thread *new_thread = ALLOC(struct thread);
    if (new_thread == NULL) {
        errno = ENOMEM;
        goto fail;
    }

    memcpy(&new_thread->ctx, ctx, sizeof(struct cpu_ctx));

    void *kernel_stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    VECTOR_PUSH_BACK(new_thread->stacks, kernel_stack_phys);
    new_thread->kernel_stack = kernel_stack_phys + STACK_SIZE + VMM_HIGHER_HALF;

    void *pf_stack_phys = pmm_alloc(STACK_SIZE / PAGE_SIZE);
    VECTOR_PUSH_BACK(new_thread->stacks, pf_stack_phys);
    new_thread->pf_stack = kernel_stack_phys + STACK_SIZE + VMM_HIGHER_HALF;

#if defined (__x86_64__)
    new_thread->cr3 = (uint64_t)new_proc->pagemap->top_level - VMM_HIGHER_HALF;
#endif

    new_thread->self = new_thread;
    new_thread->process = new_proc;
    new_thread->timeslice = thread->timeslice;
    new_thread->gs_base = get_kernel_gs_base();
    new_thread->fs_base = get_fs_base();
    new_thread->running_on = -1;
    new_thread->fpu_storage = pmm_alloc(DIV_ROUNDUP(fpu_storage_size, PAGE_SIZE))
                              + VMM_HIGHER_HALF;

    memcpy(new_thread->fpu_storage, thread->fpu_storage, fpu_storage_size);

    new_thread->ctx.rax = 0;
    new_thread->ctx.rbx = 0;

    VECTOR_PUSH_BACK(new_proc->threads, new_thread);

    sched_enqueue_thread(new_thread, false);

    return 69;

fail:
    // TODO: Properly clean up
    free(new_proc);
    return -1;
}

int syscall_exec(void *_, const char *path, const char **argv, const char **envp) {
    (void)_;

    print("syscall: exec(%s, %lx, %lx)", path, argv, envp);

    struct thread *thread = sched_current_thread();
    struct process *proc = thread->process;

    struct pagemap *new_pagemap = vmm_new_pagemap();
    struct auxval auxv, ld_auxv;
    const char *ld_path;

    struct vfs_node *node = vfs_get_node(proc->cwd, path, true);
    if (node == NULL || !elf_load(new_pagemap, node->resource, 0x0, &auxv, &ld_path)) {
        goto fail;
    }

    struct vfs_node *ld_node = vfs_get_node(vfs_root, ld_path, true);
    if (ld_node == NULL || !elf_load(new_pagemap, ld_node->resource, 0x40000000, &ld_auxv, NULL)) {
        goto fail;
    }

    struct pagemap *old_pagemap = proc->pagemap;

    proc->pagemap = new_pagemap;
    proc->thread_stack_top = 0x70000000000;
    proc->mmap_anon_base = 0x80000000000;

    // TODO: Kill old threads
    proc->threads = (typeof(proc->threads))VECTOR_INIT;

    uint64_t entry = ld_path == NULL ? auxv.at_entry : ld_auxv.at_entry;

    struct thread *new_thread = sched_new_user_thread(proc, (void *)entry, NULL, NULL, argv, envp, &auxv, true);

    if (new_thread == NULL) {
        goto fail;
    }

    vmm_destroy_pagemap(old_pagemap);
    sched_dequeue_and_die();

fail:
    return -1;
}
