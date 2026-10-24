/* MDEC save state test, run in place of the BIOS.
 *
 * Resets MDEC, then writes its control register forever with DMA off, so
 * the command state machine runs on an empty input FIFO at every point a
 * state can be saved. RAM[0] counts loops. */
typedef unsigned int u32;

__attribute__((section(".params"))) const volatile u32 param_seed = 1;
__attribute__((section(".params"))) const volatile u32 param_mode = 0;

#define IO32(a) (*(volatile u32*)(0xBF800000u + (a)))
#define RAM32(a) (*(volatile u32*)(0xA0000000u + (a)))

void test_main(void)
{
  IO32(0x1824) = 0x80000000u; /* MDEC reset */
  RAM32(0) = 0;
  for (;;)
  {
    IO32(0x1824) = 0;
    RAM32(0)++;
  }
}
