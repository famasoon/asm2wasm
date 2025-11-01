fibonacci:
    mov %ebx, 1
    cmp %eax, %ebx
    jle base_case
    push %eax
    sub %eax, 1
    call fibonacci
    mov %ecx, %eax
    pop %eax
    push %ecx
    sub %eax, 2
    call fibonacci
    mov %edx, %eax
    pop %ecx
    add %ecx, %edx
    mov %eax, %ecx
    ret

base_case:
    cmp %eax, 0
    je zero_case
    mov %eax, 1
    ret

zero_case:
    mov %eax, 0
    ret

main:
    mov %eax, 10
    call fibonacci
    ret
