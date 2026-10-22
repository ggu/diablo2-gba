ENTRY(_start)
MEMORY {
 ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
 EWRAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
 IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 31K
}
SECTIONS {
 .text : {
  KEEP(*(.gbaheader))
  *(.text.start)
  *(.text*)
  KEEP(*(.rodata.gba_save_signature))
  *(.rodata*)
  . = ALIGN(4);
 } > ROM
 .data : {
  __data_start = .;
  *(.data*)
  . = ALIGN(4);
  __data_end = .;
 } > EWRAM AT > ROM
 __data_load = LOADADDR(.data);
 .iwram : {
  __iwram_start = .;
  *(.iwram*)
  . = ALIGN(4);
  __iwram_end = .;
 } > IWRAM AT > ROM
 __iwram_load = LOADADDR(.iwram);
 .bss (NOLOAD) : {
  __bss_start = .;
  *(.bss*) *(COMMON)
  . = ALIGN(4);
  __bss_end = .;
 } > EWRAM
 /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
 ASSERT(__iwram_end < 0x03005000, "IWRAM code exhausts the 11 KiB stack reserve")
 ASSERT(__bss_end < 0x0203e000, "GBA working RAM exhausted; leave stack/safety margin")
}
