/* Reset and exception entry for the test ROMs, which run in place of the BIOS. */
  .set noreorder
  .section .reset, "ax"
  .globl _reset
_reset:
  lui $sp, 0x1F80
  j test_main
  ori $sp, $sp, 0x3F0

  /* Any exception: skip the faulting instruction and return (rfe). */
  .section .exc, "ax"
  mfc0 $k0, $14
  nop
  addiu $k0, $k0, 4
  jr $k0
  .word 0x42000010
