/* Fixture PE only. .s7site starts at RVA 0x4ecc000. */
.section .s7site,"xr"
.org 0x92c
.globl fixture_row_va,fixture_row_return
fixture_row_va:
    subq $40,%rsp
    movq (%rcx),%rax
    call *0x58(%rax)
fixture_row_return:
    addq $40,%rsp
    ret
