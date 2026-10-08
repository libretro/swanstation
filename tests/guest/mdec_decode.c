/* MDEC decode test, run in place of the BIOS.
 *
 * Decodes a pseudo-random macroblock stream through DMA the way games do,
 * once per case: both orders of starting MDEC out and MDEC in, 24-bit and
 * 15-bit output, with and without AC coefficients, from a stream that just
 * fills the input FIFO or one four times that size. Cases 16-19 decode the
 * streams of cases 0, 4, 8 and 12 again but read the output with the CPU.
 * For case n, RAM[0x100 + n * 4] holds a checksum of the decoded output,
 * RAM[0x200 + n * 4] the busy bits left in MDEC out (bit 0) and MDEC in
 * (bit 1) CHCR plus bit 2 for a CPU-read mismatch, all of which must be
 * zero, and RAM[0x300 + n * 4] the MDEC status register afterwards.
 * RAM[0] is 0xC0FFEE when all cases ran. */
typedef unsigned int u32;
typedef unsigned short u16;

__attribute__((section(".params"))) const volatile u32 param_seed = 1;
__attribute__((section(".params"))) const volatile u32 param_mode = 0;

#define IO32(a) (*(volatile u32*)(0xBF800000u + (a)))
#define RAM32(a) (*(volatile u32*)(0xA0000000u + (a)))
#define RAM16(a) (*(volatile u16*)(0xA0000000u + (a)))

#define NUM_CASES 20
#define NUM_DMA_CASES 16
#define IN_ADDR 0x100000u
#define OUT_ADDR 0x140000u

static u32 rng_state;
static u32 rng(void)
{
  rng_state = rng_state * 1103515245u + 12345u;
  return rng_state >> 8;
}

static u32 put_block(u32 pos, u32 with_ac)
{
  u32 r = rng();
  u32 k, n;

  /* DC: quantizer scale in bits 10-15, value in bits 0-9. */
  RAM16(IN_ADDR + 4 + pos * 2) = (u16)(((1u + (r & 15u)) << 10) | ((r >> 4) & 0x3FFu));
  pos++;

  n = with_ac ? (rng() % 7u) : 0;
  for (k = 0; k < n; k++)
  {
    /* AC: zero run in bits 10-15 (at most 7, so the block cannot overrun), level in bits 0-9. */
    r = rng();
    RAM16(IN_ADDR + 4 + pos * 2) = (u16)(((r & 7u) << 10) | ((r >> 3) & 0x3FFu));
    pos++;
  }

  RAM16(IN_ADDR + 4 + pos * 2) = 0xFE00u;
  return pos + 1;
}

void test_main(void)
{
  u32 c, i, j, sum;

  IO32(0x10F0) = 0x0FFFFFFFu; /* DPCR: all channels on */
  IO32(0x1824) = 0x80000000u; /* MDEC reset */

  IO32(0x1820) = 0x40000001u; /* set luma and chroma quant tables */
  for (i = 0; i < 32; i++)
    IO32(0x1820) = 0x01010101u * (1u + (i & 7u));
  IO32(0x1820) = 0x60000000u; /* set scale table */
  for (i = 0; i < 32; i++)
    IO32(0x1820) = 0x5A825A82u;

  for (c = 0; c < NUM_CASES; c++)
  {
    /* Cases from 16 on read MDEC out with the CPU instead of DMA, decoding the stream of an even DMA case. */
    const u32 cpu_read = c >= NUM_DMA_CASES;
    const u32 v = cpu_read ? (c - NUM_DMA_CASES) * 4u : c;
    /* Colour output as movies use it: depth 2 is 24-bit, 3 is 15-bit; six blocks per macroblock. */
    const u32 depth = 2u + ((v >> 1) & 1u);
    const u32 with_ac = (v >> 2) & 1u;
    const u32 blocks = 6u;
    const u32 out_words_per_mb = (depth == 2) ? 192u : 128u;
    /* AC coefficients make blocks up to 8 halfwords; keep the stream within its length. */
    const u32 num_macroblocks = (((v >> 3) & 1u) || !with_ac) ? 40u : 8u;
    const u32 out_words = num_macroblocks * out_words_per_mb;
    const u32 out_block = 32u;
    /* A 256-word stream exactly fills the MDEC input FIFO; 1024 words keep MDEC in running. */
    const u32 in_words = ((v >> 3) & 1u) ? 1024u : 256u;
    u32 pos = 0;

    /* Both DMA orders decode the same stream, so case pairs must match. */
    rng_state = param_seed + (v >> 1);

    RAM32(IN_ADDR) = 0x20000000u | (depth << 27) | (in_words - 1u);
    for (i = 0; i < num_macroblocks * blocks; i++)
      pos = put_block(pos, with_ac);
    for (; pos < (in_words - 1u) * 2u; pos++)
      RAM16(IN_ADDR + 4 + pos * 2) = 0xFE00u;
    for (i = 0; i < out_words; i++)
      RAM32(OUT_ADDR + i * 4) = 0;

    if (cpu_read)
    {
      IO32(0x1824) = 0x40000000u; /* DMA in only */
      IO32(0x1080) = IN_ADDR;
      IO32(0x1084) = ((in_words / 32u) << 16) | 32u;
      IO32(0x1088) = 0x01000201u;
      /* Each read waits for the decoder when the output FIFO is empty. */
      for (i = 0; i < out_words; i++)
        RAM32(OUT_ADDR + i * 4) = IO32(0x1820);
    }
    else
    {
      IO32(0x1824) = 0x60000000u; /* DMA in and out */
      if (v & 1u)
      {
        IO32(0x1080) = IN_ADDR;
        IO32(0x1084) = ((in_words / 32u) << 16) | 32u;
        IO32(0x1088) = 0x01000201u;
        IO32(0x1090) = OUT_ADDR;
        IO32(0x1094) = ((out_words / out_block) << 16) | out_block;
        IO32(0x1098) = 0x01000200u;
      }
      else
      {
        IO32(0x1090) = OUT_ADDR;
        IO32(0x1094) = ((out_words / out_block) << 16) | out_block;
        IO32(0x1098) = 0x01000200u;
        IO32(0x1080) = IN_ADDR;
        IO32(0x1084) = ((in_words / 32u) << 16) | 32u;
        IO32(0x1088) = 0x01000201u;
      }
    }

    for (j = 0; j < 3000000u && ((IO32(0x1098) | IO32(0x1088)) & 0x01000000u); j++)
    {
    }

    sum = 0;
    for (i = 0; i < out_words; i++)
      sum = sum * 31u + RAM32(OUT_ADDR + i * 4);
    RAM32(0x100 + c * 4) = sum;
    /* bit 0/1: MDEC out/in still busy; bit 2: CPU read output differs from the DMA case's */
    RAM32(0x200 + c * 4) = ((IO32(0x1098) >> 24) & 1u) | (((IO32(0x1088) >> 24) & 1u) << 1) |
                           ((cpu_read && sum != RAM32(0x100 + v * 4)) ? 4u : 0u);
    RAM32(0x300 + c * 4) = IO32(0x1824);
  }

  RAM32(0) = 0xC0FFEEu;
  for (;;)
  {
  }
}
