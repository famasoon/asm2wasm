complex_calculation:
    mov %eax, 100
    mov %ebx, 25
    mov %ecx, 10
    add %eax, %ebx
    mul %eax, %ecx
    sub %eax, 50
    mov %edx, %eax
    ret

main:
    call complex_calculation
    ret
