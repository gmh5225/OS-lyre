.section .rodata

.global syscall_table
syscall_table:
    .quad syscall_debug       // 0
    .quad syscall_mmap        // 1
    .quad syscall_openat      // 2
    .quad syscall_close       // 3
    .quad syscall_read        // 4
    .quad syscall_write       // 5
    .quad syscall_seek        // 6
    .quad syscall_set_fs_base // 7
    .quad syscall_set_gs_base // 8
    .quad syscall_stat        // 9
    .quad syscall_fcntl       // 10
    .quad syscall_dup3        // 11
    .quad syscall_ioctl       // 12
    .quad syscall_fork        // 13
    .quad syscall_exec        // 14
    .quad syscall_getpid      // 15
    .quad syscall_waitpid     // 16
    .quad syscall_exit        // 17
    .quad syscall_getcwd      // 18
    .quad syscall_chdir       // 19
    .quad syscall_munmap      // 20
    .quad syscall_pipe        // 21
syscall_table_end:

.global syscall_count
syscall_count:
    .quad (syscall_table_end - syscall_table) / 8
