sum_loop:
    mov %eax, 0
    mov %ebx, 1
    mov %ecx, 10

loop_start:
    cmp %ebx, %ecx
    jg loop_end
    add %eax, %ebx
    add %ebx, 1
    jmp loop_start

loop_end:
    ret

main:
    call sum_loop
    ret
