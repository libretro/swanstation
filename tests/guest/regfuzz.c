/* Hardware register fuzzer, run in place of the BIOS.
 *
 * Writes and reads pseudo-random values in the GPU, DMA, CD-ROM, SPU,
 * MDEC, timer and SIO registers forever. The seed parameter picks the
 * sequence; bits of the mode parameter enable each group (bit 0 GP0,
 * 1 GP1, 2 DMA, 3 CD-ROM, 4 SPU, 5 MDEC, 6 timers, 7 SIO). RAM[0] holds
 * the iteration count unless a DMA overwrites it. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

__attribute__((section(".params"))) const volatile u32 param_seed = 1;
__attribute__((section(".params"))) const volatile u32 param_mode = 0xFFFFFFFFu;

#define IO32(a) (*(volatile u32*)(0xBF800000u + (a)))
#define IO16(a) (*(volatile u16*)(0xBF800000u + (a)))
#define IO8(a) (*(volatile u8*)(0xBF800000u + (a)))

static u32 rng_state;
static u32 rng(void)
{
  rng_state = rng_state * 1103515245u + 12345u;
  return (rng_state >> 8) ^ (rng_state << 21);
}

static u32 dma_base(void)
{
  /* Half the transfers start near the end of RAM so they wrap. */
  if (rng() & 1)
    return 0x1F0000u + (rng() & 0xFFFCu);
  return rng() & 0xFFFFFFu;
}

static u32 gp0_word(void)
{
  const u32 r = rng();
  switch (rng() & 7)
  {
    case 0:
      return (r & 0xF000F000u) ? r : 0x50005000u; /* sometimes a polyline terminator */
    case 1:
      return r & 0x07FF07FFu; /* in-range coordinates */
    case 2:
      return (r & 0x00FFFFFFu) | ((0x20u + (rng() % 0x60u)) << 24); /* draw */
    case 3:
      return (r & 0x00FFFFFFu) | ((0xA0u + (rng() % 0x20u)) << 24); /* CPU to VRAM */
    case 4:
      return (r & 0x00FFFFFFu) | ((0x80u + (rng() % 0x20u)) << 24); /* VRAM to VRAM */
    case 5:
      return (r & 0x00FFFFFFu) | ((0xE0u + (rng() % 8u)) << 24); /* environment */
    default:
      return r;
  }
}

static void poke_dma(void)
{
  const u32 ch = rng() % 7u;
  const u32 base = 0x1080u + ch * 0x10u;
  u32 chcr = rng();

  IO32(base) = dma_base();
  IO32(base + 4) = ((rng() & 7) != 0) ? (rng() & 0x000F001Fu) : rng();
  if (rng() & 1)
    chcr |= 0x01000000u;
  if (rng() & 1)
    chcr |= 0x10000000u;
  if (ch == 6)
    chcr = (chcr & ~0x600u) | 0x2u; /* OTC: manual, backwards */
  if (ch == 2 && (rng() & 1))
  {
    chcr = (chcr & ~0x600u) | 0x400u | 1u; /* GPU linked list */
    IO32(0x1814) = 0x04000002u;
  }
  IO32(base + 8) = chcr;
}

static void poke_cdrom(void)
{
  IO8(0x1800) = rng() & 3;
  if (rng() & 1)
    IO8(0x1801 + (rng() % 3u)) = (u8)rng();
  else
    (void)IO8(0x1801 + (rng() % 3u));

  if ((rng() & 7) == 0)
  {
    /* A command with parameters. */
    const u32 np = rng() & 7;
    u32 k;
    IO8(0x1800) = 0;
    for (k = 0; k < np; k++)
      IO8(0x1802) = (u8)rng();
    IO8(0x1801) = (u8)(rng() % 0x40u);
  }
}

void test_main(void)
{
  u32 i;

  rng_state = param_seed;
  IO32(0x1074) = 0;           /* no interrupts */
  IO32(0x10F0) = 0x0FFFFFFFu; /* DPCR: all channels on */

  for (i = 0;; i++)
  {
    const u32 op = rng() % 100u;
    const u32 mode = param_mode;

    *(volatile u32*)0xA0000000u = i;
    if (op < 35 && (mode & 1))
    {
      const u32 n = 1 + (rng() & 15);
      u32 k;
      for (k = 0; k < n; k++)
        IO32(0x1810) = gp0_word();
    }
    else if (op < 42 && (mode & 2))
    {
      u32 v = rng();
      if ((v >> 24) == 0)
        v |= 0x01000000u; /* not a reset every time */
      IO32(0x1814) = v;
    }
    else if (op < 60 && (mode & 4))
    {
      poke_dma();
    }
    else if (op < 72 && (mode & 8))
    {
      poke_cdrom();
    }
    else if (op < 84 && (mode & 16))
    {
      IO16(0x1C00 + ((rng() % 0x200u) & ~1u)) = (u16)rng();
      if ((rng() & 3) == 0)
        IO16(0x1DA8) = (u16)rng(); /* transfer FIFO */
    }
    else if (op < 90 && (mode & 32))
    {
      if (rng() & 1)
        IO32(0x1820) = rng();
      else
        IO32(0x1824) = rng();
      (void)IO32(0x1820);
    }
    else if (op < 94 && (mode & 64))
    {
      IO16(0x1100 + (rng() % 3u) * 0x10u + (rng() % 3u) * 4u) = (u16)rng();
    }
    else if (op < 97 && (mode & 128))
    {
      IO16(0x1040 + ((rng() % 0x10u) & ~1u)) = (u16)rng();
      (void)IO8(0x1040);
    }
    else
    {
      (void)IO32(0x1810);
      (void)IO32(0x1814);
      (void)IO16(0x1DAE);
      (void)IO32(0x1824);
    }
  }
}
