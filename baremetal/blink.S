/* Banana Pi M2 Zero (H2+/H3): ARM code loaded by BROM into SRAM A1. */
.syntax unified
.cpu cortex-a7
.arm
.section .text.boot, "ax"
.global _start
_start:
    b reset
    .ascii "eGON.BT0"
    .word 0                       /* checksum filled by image.py */
    .word 0                       /* padded length filled by image.py */
    .space 76, 0                  /* total header: 96 bytes */
reset:
    cpsid if                      /* no interrupt handlers or stack */

    /* R_PIO APB gate: PRCM, bit 0. R_PIO has no APB reset bit on H3. */
    ldr r0, =0x01f01400
    ldr r1, [r0, #0x28]
    orr r1, r1, #1
    str r1, [r0, #0x28]
    dsb sy

    /* PL10 is active-low. Set data low before switching to output. */
    ldr r0, =0x01f02c00
    ldr r1, [r0, #0x10]           /* PL_DAT */
    bic r1, r1, #0x400
    str r1, [r0, #0x10]
    ldr r1, [r0, #0x04]           /* PL_CFG1: PL10 field [11:8] */
    bic r1, r1, #0xf00
    orr r1, r1, #0x100            /* function 1: GPIO output */
    str r1, [r0, #0x04]
    dsb sy
blink:
    /* Busy wait: intentionally no timer, PLL, DDR or stack dependency.
     * Duration depends on the CPU clock left by BROM. */
    ldr r2, =2000000
delay:
    subs r2, r2, #1
    bne delay
    ldr r1, [r0, #0x10]
    eor r1, r1, #0x400
    str r1, [r0, #0x10]
    dsb sy
    b blink
    .ltorg
