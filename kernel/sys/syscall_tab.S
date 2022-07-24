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
syscall_table_end:

.global syscall_count
syscall_count:
    .quad (syscall_table_end - syscall_table) / 8
