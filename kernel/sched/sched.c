#include <stdbool.h>
#include <stdnoreturn.h>
#include <lib/print.h>
#include <lib/misc.h>
#include <lib/alloc.h>
#include <lib/vector.h>
#include <sched/sched.h>
#include <sys/timer.h>
#include <sys/cpu.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

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
    if (current_thread->ctx.cs == 0x43) {
        set_kernel_gs_base(current_thread->gs_base);
    } else {
        set_kernel_gs_base(current_thread);
    }
    set_fs_base(current_thread->fs_base);

    cpu->tss.ist2 = (uint64_t)current_thread->pf_stack;

    if (read_cr3() != current_thread->cr3) {
        write_cr3(current_thread->cr3 - VMM_HIGHER_HALF);
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

#define STACK_SIZE 0x10000

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

    thread->cr3 = (uint64_t)kernel_process->pagemap->top_level;
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
