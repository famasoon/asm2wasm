array_sum:
    mov %eax, 0
    mov %ebx, 0

sum_loop:
    cmp %ebx, %ecx
    jge sum_done
    mov %edx, (%esi+%ebx*4)
    add %eax, %edx
    add %ebx, 1
    jmp sum_loop

sum_done:
    ret

main:
    mov %esi, 1000
    mov %ecx, 5
    mov (%esi), 10
    mov (%esi+4), 20
    mov (%esi+8), 30
    mov (%esi+12), 40
    mov (%esi+16), 50
    call array_sum
    ret
