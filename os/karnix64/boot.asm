bits 32

section .multiboot2
align 8
header_start:
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd -(0xE85250D6 + (header_end - header_start))
    dw 5
    dw 1
    dd 20
    dd 1024
    dd 768
    dd 32
align 8
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 8
boot_magic:
    resd 1
boot_info_address:
    resd 1

alignb 4096
pml4:
    resq 512
pdpt:
    resq 512
page_directory:
    resq 2048

align 16
stack_bottom:
    resb 65536
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    mov [boot_magic], eax
    mov [boot_info_address], ebx

    mov eax, pdpt
    or eax, 0x03
    mov dword [pml4], eax
    mov dword [pml4 + 4], 0

    mov edi, pdpt
    mov eax, page_directory
    mov ecx, 4
.map_page_directories:
    mov ebx, eax
    or ebx, 0x03
    mov dword [edi], ebx
    mov dword [edi + 4], 0
    add eax, 4096
    add edi, 8
    loop .map_page_directories

    mov edi, page_directory
    xor eax, eax
    mov ecx, 2048
.map_first_four_gib:
    mov ebx, eax
    or ebx, 0x83
    mov dword [edi], ebx
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .map_first_four_gib

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    lgdt [gdt64_pointer]
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    jmp 0x08:long_mode_start

align 8
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt64_end:
gdt64_pointer:
    dw gdt64_end - gdt64 - 1
    dd gdt64

bits 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top
    and rsp, -16
    xor rbp, rbp
    mov edi, dword [rel boot_magic]
    mov esi, dword [rel boot_info_address]
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
