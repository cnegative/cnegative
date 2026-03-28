#if defined(__APPLE__)
.section __TEXT,__text,regular,pure_instructions
#define CN_GLOBAL(name) _##name
#define CN_DECLARE_GLOBAL(name) .globl CN_GLOBAL(name)
#define CN_DECLARE_TYPE(name)
#define CN_LABEL(name) CN_GLOBAL(name):
#else
.text
#define CN_GLOBAL(name) name
#define CN_DECLARE_GLOBAL(name) .globl CN_GLOBAL(name)
#define CN_DECLARE_TYPE(name) .type CN_GLOBAL(name), @function
#define CN_LABEL(name) CN_GLOBAL(name):
#endif

CN_DECLARE_GLOBAL(cn_scan_number_tail)
CN_DECLARE_TYPE(cn_scan_number_tail)
CN_LABEL(cn_scan_number_tail)
    movq %rdx, %rax
.Lscan_number_loop:
    cmpq %rsi, %rax
    jae .Lscan_number_done
    movzbq (%rdi,%rax,1), %rcx
    cmpb $'0', %cl
    jb .Lscan_number_done
    cmpb $'9', %cl
    ja .Lscan_number_done
    incq %rax
    jmp .Lscan_number_loop
.Lscan_number_done:
    ret

CN_DECLARE_GLOBAL(cn_scan_identifier_tail)
CN_DECLARE_TYPE(cn_scan_identifier_tail)
CN_LABEL(cn_scan_identifier_tail)
    movq %rdx, %rax
.Lscan_ident_loop:
    cmpq %rsi, %rax
    jae .Lscan_ident_done
    movzbq (%rdi,%rax,1), %rcx
    cmpb $'_', %cl
    je .Lscan_ident_advance
    cmpb $'0', %cl
    jb .Lscan_ident_upper
    cmpb $'9', %cl
    jbe .Lscan_ident_advance
.Lscan_ident_upper:
    cmpb $'A', %cl
    jb .Lscan_ident_lower
    cmpb $'Z', %cl
    jbe .Lscan_ident_advance
.Lscan_ident_lower:
    cmpb $'a', %cl
    jb .Lscan_ident_done
    cmpb $'z', %cl
    ja .Lscan_ident_done
.Lscan_ident_advance:
    incq %rax
    jmp .Lscan_ident_loop
.Lscan_ident_done:
    ret

#if !defined(__APPLE__)
.section .note.GNU-stack,"",@progbits
#endif
