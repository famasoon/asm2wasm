array_operations:
    mov %esi, 1000
    mov %eax, 10
    mov (%esi), %eax
    mov %eax, 20
    mov (%esi+4), %eax
    mov %eax, 30
    mov (%esi+8), %eax
    mov %eax, (%esi)
    mov %ebx, (%esi+4)
    add %eax, %ebx
    mov %ebx, (%esi+8)
    add %eax, %ebx
    ret

main:
    call array_operations
    ret
