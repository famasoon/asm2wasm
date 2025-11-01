factorial:
    cmp %eax, 1
    jle base_case
    mov %ebx, %eax
    sub %eax, 1
    call factorial
    mul %eax, %ebx
    ret

base_case:
    mov %eax, 1
    ret

main:
    mov %eax, 5
    call factorial
    ret
