.syntax unified
.cpu arm7tdmi
.arm
.section .gbaheader,"ax",%progbits
.global _start
_start:
    b reset
    .space 188
.section .text.start,"ax",%progbits
reset:
    mov r0, #0x12
    msr cpsr_c, r0
    ldr sp, =0x03007fa0
    mov r0, #0x1f
    msr cpsr_c, r0
    ldr sp, =0x03007e00
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 1b
    ldr r0, =__iwram_load
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
4:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 4b
    ldr r1, =__bss_start
    ldr r2, =__bss_end
    mov r3, #0
2:
    cmp r1, r2
    strlo r3, [r1], #4
    blo 2b
    ldr r0, =main
    mov lr, pc
    bx r0
3:
    b 3b
.pool
