/* 512 KB BIOS image; "OpenBIOS" at 0x78 makes the core accept it as a BIOS. */
ENTRY(_reset)
SECTIONS
{
  . = 0xBFC00000;
  .reset : { KEEP(*(.reset)) }
  . = 0xBFC00078;
  .magic : { BYTE(0x4F) BYTE(0x70) BYTE(0x65) BYTE(0x6E) BYTE(0x42) BYTE(0x49) BYTE(0x4F) BYTE(0x53) }
  . = 0xBFC00100;
  .params : { KEEP(*(.params)) }
  . = 0xBFC00180;
  .exc : { KEEP(*(.exc)) }
  . = 0xBFC00200;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  /DISCARD/ : { *(.data*) *(.sdata*) *(.MIPS.*) *(.reginfo) *(.pdr) *(.comment) }
  . = 0x1F800000;
  .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) }
}
