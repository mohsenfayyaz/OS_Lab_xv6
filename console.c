// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct
{
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if (sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do
  {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if (locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint *)(void *)(&fmt + 1);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
  {
    if (c != '%')
    {
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0)
      break;
    switch (c)
    {
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if ((s = (char *)*argp++) == 0)
        s = "(null)";
      for (; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if (locking)
    release(&cons.lock);
}

void panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for (i = 0; i < 10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for (;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort *)P2V(0xb8000); // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  if (c == '\n')
    pos += 80 - pos % 80;
  else if (c == BACKSPACE || c == '\b')
  {
    if (pos > 0)
      --pos;
  }
  else
    crt[pos++] = (c & 0xff) | 0x0700; // black on white

  if (pos < 0 || pos > 25 * 80)
    panic("pos under/overflow");

  if ((pos / 80) >= 24)
  { // Scroll up.
    memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
    pos -= 80;
    memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  if (c != '\b')
  {
    crt[pos] = ' ' | 0x0700;
  }
}

void consputc(int c)
{
  if (panicked)
  {
    cli();
    for (;;)
      ;
  }

  if (c == BACKSPACE)
  {
    uartputc('\b');
    uartputc(' ');
    uartputc('\b');
  }
  else
    uartputc(c);
  cgaputc(c);
}

//move back one cursor
void vga_move_back_cursor()
{
  int pos;

  // get cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  // move back
  pos--;

  // reset cursor
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  //crt[pos] = ' ' | 0x0700;
}

void vga_move_forward_cursor()
{
  int pos;

  // get cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  // move back
  pos++;

  // reset cursor
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  //crt[pos] = ' ' | 0x0700;
}

void vga_insert_char(int c, int back_counter)
{
  int pos;

  // get cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  //move back crt buffer
  for (int i = pos + back_counter; i >= pos; i--)
  {
    crt[i + 1] = crt[i];
  }
  crt[pos] = (c & 0xff) | 0x0700;

  // move cursor to next position
  pos += 1;

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  crt[pos + back_counter] = ' ' | 0x0700;
}

void shift_right(int back_counter)
{
  int pos;

  // get cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  //move back crt buffer
  for (int i = pos + back_counter; i >= pos; i--)
  {
    crt[i + 1] = crt[i];
  }
  crt[pos] = ('_' & 0xff) | 0x0700;

  // move cursor to next position
  //pos += 1;

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  // crt[pos+back_counter] = ' ' | 0x0700;
}

#define INPUT_BUF 128
struct
{
  char buf[INPUT_BUF];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
  uint size;
} input;

uint end_pos = -1; // line last char position

#define C(x) ((x) - '@') // Control-x

void consoleintr(int (*getc)(void))
{
  if (end_pos == -1)
  {
    end_pos = input.e;
  }
  int c, doprocdump = 0;
  int i;

  acquire(&cons.lock);
  while ((c = getc()) >= 0)
  {
    switch (c)
    {
    case C('P'): // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'): // Kill line.
      while (input.e != input.w &&
             input.buf[(input.e - 1) % INPUT_BUF] != '\n')
      {
        input.e--;
        consputc(BACKSPACE);
      }
      end_pos = input.e;
      break;
    case C('H'):
    case '\x7f': // Backspace
      if (input.e != input.r + input.size)
      {
        if (input.e > input.w)
        {
          input.e--;
          input.size--;
          consputc('\b');
          for (i = 0; i < (input.r + input.size - input.e); i++) // shift all characters left
          {
            input.buf[(input.e + i) % INPUT_BUF] = input.buf[(input.e + i + 1) % INPUT_BUF];
          }
          input.buf[(input.r + input.size) % INPUT_BUF] = ' ';
          for (i = 0; i < (input.r + input.size - input.e); i++) // write all characters again
          {
            consputc(input.buf[(input.e + i) % INPUT_BUF]);
          }
          consputc(' ');
          consputc('\b');
          for (i = 0; i < (input.r + input.size - input.e); i++) // return cursor back to edit index
          {
            consputc('\b');
          }
        }
      }
      else
      {
        if (input.e != input.w)
        {
          input.e--;
          consputc(BACKSPACE);
          input.size--;
        }
      }
      end_pos--;
      break;
    case C('C'): // remove the whole input
      for(i = 0; i < input.w + input.size - input.e; i++) {
        consputc(' ');
      }
      for(int i = 0; i < input.size; i++) {
          consputc(BACKSPACE);
      }
      input.e = input.w;
      input.size = 0;
      break;
    case '{':
      while (input.e != input.w &&
             input.buf[(input.e - 1) % INPUT_BUF] != '\n')
      {
        input.e--;
        consputc('\b');
      }

      break;
    case '}':
      while (input.e != input.r + input.size)
      {
        consputc(input.buf[input.e]);
        input.e++;
      }

      break;
    default:
      if (c != 0 && input.e - input.r < INPUT_BUF)
      {
        c = (c == '\r') ? '\n' : c;
        if (input.e != input.r + input.size)
        {
          if (c == '\n' || c == C('D'))
          {
            while (input.e != input.r + input.size)
            {
              consputc(input.buf[input.e]);
              input.e++;
            }
            input.buf[input.e++ % INPUT_BUF] = c;
            input.size++;
            consputc(c);
          }
          else {
            for (i = (input.r + input.size - input.e); i > 0; i--)
            {
              input.buf[(input.e + i) % INPUT_BUF] = input.buf[(input.e + i - 1) % INPUT_BUF];
            }
            input.buf[input.e++ % INPUT_BUF] = c;
            input.size++;
            consputc(c);
            for (i = 0; i < (input.r + input.size - input.e); i++)
            {
              consputc(input.buf[(input.e + i) % INPUT_BUF]);
            }
            for (i = 0; i < (input.r + input.size - input.e); i++)
            {
              consputc('\b');
            }
          }
        }
        else
        {
          input.buf[input.e++ % INPUT_BUF] = c;
          input.size++;
          consputc(c);
        }
        if (c == '\n' || c == C('D') || input.size == INPUT_BUF)
        {
          input.w = input.e;
          input.size = 0;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if (doprocdump)
  {
    procdump(); // now call procdump() wo. cons.lock held
  }
}

int consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while (n > 0)
  {
    while (input.r == input.w)
    {
      if (myproc()->killed)
      {
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if (c == C('D'))
    { // EOF
      if (n < target)
      {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if (c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for (i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}
