// SPDX-License-Identifier: GPL-2.0-only
/* Exercise the real bootstrap parser with complete and malformed exchanges. */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../../../drivers/bluetooth/brahma_peripheral.h"

static const unsigned char replies[][25] = {
 {0xfc},
 {0x34,0,0xff,21,0xf0,1,1,0xf0,0xff,0,5,0,0x21,0,0,0,0,0,0,2,0,2,0x21,0x40,0},
 {0x34,0,0xff,7,0xf0,1,1,0xf1,0xff,1,2},
 {0x34,0,0xff,8,0xf0,1,1,0xf0,0xff,0,8,0},
 {0x34,0,0xff,8,0xf0,1,1,0xf0,0xff,0,6,0},
 {0x34,0,0xff,10,0xf0,1,1,0xf0,0xff,0,10,2,0,0},
 {0x34,0,0xff,8,0xf0,1,1,0xf0,0xff,0,6,0},
 {0x34,0,0xff,7,0xf0,1,1,0xf1,0xff,0,3},
 {0x34,0,0xff,4,0xf0,3,0,0},
 {0xfc},
 {0x34,0,0xff,8,0xf0,1,1,0xf0,0xff,0,8,3},
 {0x34,0,0xff,21,0xf0,1,1,0xf0,0xff,0,5,3,0x21,0,0,0,0,0,0,2,0,2,0x21,0x40,0},
 {0x34,0,0xff,8,0xf0,1,1,0xf0,0xff,0,6,3},
 {0x34,0,0xff,4,0xf0,3,0,3},
 {0x34,0,0xff,6,0xf0,0,0,1,0xf1,0xff},
 {0x34,0,0xff,4,0xf0,2,1,1},
};
static const int lengths[] = {1,25,11,12,12,14,12,11,8,1,12,25,12,8,10,8};
struct context { int received, sent, stages, baud, flows; int corrupt, fail_rx, fail_tx; int alter_reply, alter_offset, alter_value; };
static int send_packet(void *ctx, const unsigned char *data, size_t n)
{
 struct context *c = ctx;
 assert(n && n <= 260 && (data[0] == 0xfd || data[0] == 0x31));
 if (data[0] == 0x31) assert(n == (size_t)data[4] + 5);
 return c->sent++ == c->fail_tx ? -EIO : 0;
}
static int receive_packet(void *ctx, unsigned char *data, size_t capacity)
{
 struct context *c = ctx;
 int i = c->received++;
 assert(i < 16 && capacity >= (size_t)lengths[i]);
 if (i == c->fail_rx) return -ETIMEDOUT;
 memcpy(data, replies[i], lengths[i]);
 if (i == c->corrupt) data[0] ^= 0x80;
 if (i == c->alter_reply) data[c->alter_offset] = c->alter_value;
 return lengths[i];
}
static int baud(void *ctx) { ((struct context *)ctx)->baud++; return 0; }
static void flow(void *ctx, bool off)
{
 struct context *c = ctx;
 assert(off == (c->flows == 0)); c->flows++;
}
static void stage(void *ctx, const char *name)
{
 assert(name && *name); ((struct context *)ctx)->stages++;
}
static int run(struct context *c)
{
 unsigned char elf[520] = {0x7f,'E','L','F',1,1,1};
 unsigned char nv[8] = {2,4,0,0};
 struct brahma_image patch = {elf,sizeof(elf)}, nvm = {nv,sizeof(nv)};
 struct brahma_io io = {c,send_packet,receive_packet,baud,flow,stage};
 return brahma_peripheral_boot(&io, &patch, &nvm, &patch);
}
static struct context initial(void)
{
 return (struct context){.corrupt=-1,.fail_rx=-1,.fail_tx=-1,.alter_reply=-1};
}
int main(void)
{
 struct context good = initial();
 assert(run(&good) == 0);
 assert(good.received == 16 && good.stages == 5 && good.baud == 1 && good.flows == 2);
 for (int i=0;i<16;i++) {
  struct context c=initial(); c.corrupt=i;
  assert(run(&c) == -EPROTO && c.received == i+1);
  c=initial(); c.fail_rx=i;
  assert(run(&c) == -ETIMEDOUT && c.received == i+1);
 }
 for (int i=0;i<good.sent;i++) {
  struct context c=initial(); c.fail_tx=i;
  assert(run(&c) == -EIO && c.sent == i+1);
 }
 /* Correct framing must still reject the wrong ROM, status and board. */
 const int corrupt_fields[][3] = {{1,12,0x22},{11,19,3},{2,9,0},{5,12,1},{12,11,0},{15,7,0}};
 for (unsigned i=0;i<sizeof(corrupt_fields)/sizeof(corrupt_fields[0]);i++) {
  struct context c=initial();
  c.alter_reply=corrupt_fields[i][0]; c.alter_offset=corrupt_fields[i][1]; c.alter_value=corrupt_fields[i][2];
  assert(run(&c) < 0 && c.received == c.alter_reply+1);
 }
 {
  struct context c=initial();
  struct brahma_io io={&c,send_packet,receive_packet,baud,flow,stage};
  unsigned char bad[52]={0}; struct brahma_image img={bad,sizeof(bad)};
  assert(brahma_download(&io,0,&img,false) == -EINVAL && !c.sent);
  bad[0]=2; bad[1]=47;
  assert(brahma_download(&io,0,&img,true) == -EINVAL && !c.sent);
  img.size=1024*1024+1;
  assert(brahma_download(&io,0,&img,false) == -EINVAL && !c.sent);
 }
 puts("Brahma: complete bootstrap, 16 malformed replies, 16 receive timeouts and every send failure passed");
 return 0;
}
