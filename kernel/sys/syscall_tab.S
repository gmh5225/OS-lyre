.section .rodata

.global syscall_table
syscall_table:
    .quad syscall_debug     // 0
    .quad syscall_mmap      // 1
syscall_table_end:

.global syscall_count
syscall_count:
    .quad (syscall_table_end - syscall_table) / 8
