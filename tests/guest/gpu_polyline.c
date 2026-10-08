/* GPU poly-line test, run in place of the BIOS.
 *
 * Draws long poly-lines (far more vertices than the core buffers at once)
 * flat and shaded, opaque and semi-transparent, then reads the area back
 * through GPUREAD. For case n, RAM[0x100 + n * 4] holds a checksum of the
 * 256x256 area. RAM[0] is 0xC0FFEE when all cases ran. */
typedef unsigned int u32;

__attribute__((section(".params"))) const volatile u32 param_seed = 1;
__attribute__((section(".params"))) const volatile u32 param_mode = 0;

#define IO32(a) (*(volatile u32*)(0xBF800000u + (a)))
#define RAM32(a) (*(volatile u32*)(0xA0000000u + (a)))
#define GP0(v) (IO32(0x1810) = (v))
#define GP1(v) (IO32(0x1814) = (v))

#define NUM_CASES 4
#define NUM_VERTICES 3000u

static u32 rng_state;
static u32 rng(void)
{
  rng_state = rng_state * 1103515245u + 12345u;
  return rng_state >> 8;
}

static void wait_gpu_idle(void)
{
  u32 i;
  /* GPUSTAT bit 26: ready for a command. */
  for (i = 0; i < 1000000u && !(IO32(0x1814) & 0x04000000u); i++)
  {
  }
}

static u32 position(void)
{
  const u32 r = rng();
  return (r & 0xFFu) | (((r >> 8) & 0xFFu) << 16);
}

void test_main(void)
{
  u32 c, i, sum;

  GP1(0x00000000u); /* reset */
  GP0(0xE1000400u); /* draw to the display area allowed */
  GP0(0xE3000000u); /* drawing area top-left 0,0 */
  GP0(0xE4000000u | (511u << 10) | 1023u);
  GP0(0xE5000000u); /* no offset */
  GP0(0xE6000000u); /* no mask */

  for (c = 0; c < NUM_CASES; c++)
  {
    const u32 shaded = c & 1u;
    const u32 semi = (c >> 1) & 1u;

    rng_state = param_seed + c;

    /* Clear the area with a fill. */
    GP0(0x02102030u);
    GP0(0x00000000u);
    GP0((256u << 16) | 256u);
    wait_gpu_idle();

    GP0(((shaded ? 0x58u : 0x48u) | (semi ? 0x02u : 0u)) << 24 | (rng() & 0xFFFFFFu));
    GP0(position());
    for (i = 1; i < NUM_VERTICES; i++)
    {
      if (shaded)
        GP0(rng() & 0xFFFFFFu);
      GP0(position());
    }
    GP0(0x55555555u);
    wait_gpu_idle();

    /* Read the 256x256 area back: 2 pixels per word. */
    GP0(0xC0000000u);
    GP0(0x00000000u);
    GP0((256u << 16) | 256u);
    sum = 0;
    for (i = 0; i < 256u * 256u / 2u; i++)
      sum = sum * 31u + IO32(0x1810);
    RAM32(0x100 + c * 4) = sum;
  }

  RAM32(0) = 0xC0FFEEu;
  for (;;)
  {
  }
}
