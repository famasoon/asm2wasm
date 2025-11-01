compare_values:
    mov %eax, 10
    mov %ebx, 15
    cmp %eax, %ebx
    jl less_than
    jg greater_than
    je equal

less_than:
    mov %ecx, 1
    jmp end

greater_than:
    mov %ecx, 2
    jmp end

equal:
    mov %ecx, 0

end:
    ret
