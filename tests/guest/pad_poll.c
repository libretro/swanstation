/* Pad polling test, run in place of the BIOS.
 *
 * Reads both controller ports with command 0x42 and memory card sector 1 on
 * port 1, back to back, forever, at a slow baud rate, so that frames end
 * with a controller or card part way through a command and often with a
 * byte still being transmitted. The card read sends 0x01 (the sector's low
 * byte) in the middle of the card's command. RAM[0x100 + port * 0x10] holds
 * the last response from that port, RAM[0x120] the first bytes of the card
 * reply and RAM[0] counts completed cycles. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

__attribute__((section(".params"))) const volatile u32 param_seed = 1;
__attribute__((section(".params"))) const volatile u32 param_mode = 0;

#define IO8(a) (*(volatile u8*)(0xBF800000u + (a)))
#define IO16(a) (*(volatile u16*)(0xBF800000u + (a)))
#define RAM8(a) (*(volatile u8*)(0xA0000000u + (a)))
#define RAM32(a) (*(volatile u32*)(0xA0000000u + (a)))

#define JOY_DATA 0x1040
#define JOY_STAT 0x1044
#define JOY_MODE 0x1048
#define JOY_CTRL 0x104A
#define JOY_BAUD 0x104E

#define CTRL_TXEN 0x0001u
#define CTRL_SELECT 0x0002u
#define CTRL_ACK 0x0010u
#define CTRL_PORT2 0x2000u

#define STAT_RX_READY 0x0002u
#define STAT_ACK_LOW 0x0080u

#define RESPONSE_BYTES 9
#define CARD_BYTES 16
/* Eight cycles per bit; 0x88 is what games use. */
#define SLOW_BAUD 0x0400

static void delay(u32 n)
{
  u32 i;
  for (i = 0; i < n; i++)
  {
    __asm__ volatile("nop");
  }
}

/* Sends one byte and returns the reply; *acked is nonzero when the device acknowledged it. */
static u8 exchange(u8 out, u32* acked)
{
  u32 i;
  u8 in;

  IO8(JOY_DATA) = out;
  for (i = 0; i < 200000u && !(IO16(JOY_STAT) & STAT_RX_READY); i++)
  {
  }
  in = IO8(JOY_DATA);

  *acked = 0;
  for (i = 0; i < 2000u; i++)
  {
    if (IO16(JOY_STAT) & STAT_ACK_LOW)
    {
      *acked = 1;
      break;
    }
  }
  return in;
}

static void poll_port(u32 port)
{
  const u16 ctrl = (u16)(CTRL_TXEN | CTRL_SELECT | (port ? CTRL_PORT2 : 0u));
  u32 i, acked;
  u8 reply;

  IO16(JOY_CTRL) = (u16)(ctrl | CTRL_ACK);
  delay(200);

  exchange(0x01, &acked);
  for (i = 0; i < RESPONSE_BYTES && acked; i++)
  {
    reply = exchange(i == 0 ? 0x42 : 0x00, &acked);
    RAM8(0x100 + port * 0x10 + i) = reply;
    IO16(JOY_CTRL) = (u16)(ctrl | CTRL_ACK);
  }

  IO16(JOY_CTRL) = 0;
  delay(100);
}

/* Starts a read of sector 1 and keeps the first bytes of the reply. */
static void read_card(void)
{
  static const u8 command[] = {0x81, 0x52, 0x00, 0x00, 0x00, 0x01};
  const u16 ctrl = (u16)(CTRL_TXEN | CTRL_SELECT);
  u32 i, acked = 1;
  u8 reply;

  IO16(JOY_CTRL) = (u16)(ctrl | CTRL_ACK);
  delay(200);

  for (i = 0; i < CARD_BYTES && acked; i++)
  {
    reply = exchange(i < sizeof(command) ? command[i] : 0x00, &acked);
    RAM8(0x120 + i) = reply;
    IO16(JOY_CTRL) = (u16)(ctrl | CTRL_ACK);
  }

  IO16(JOY_CTRL) = 0;
  delay(100);
}

void test_main(void)
{
  IO16(JOY_MODE) = 0x000D;
  IO16(JOY_BAUD) = SLOW_BAUD;
  RAM32(0) = 0;

  for (;;)
  {
    poll_port(0);
    poll_port(1);
    read_card();
    RAM32(0) = RAM32(0) + 1;
  }
}
