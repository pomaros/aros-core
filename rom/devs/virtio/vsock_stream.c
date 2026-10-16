/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.
   Independently implemented from OASIS Virtio 1.2 section 5.10. */
#include "vsock_stream.h"
#include <string.h>
static uint64_t getle(const uint8_t *p,unsigned n)
{ uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(8*i);return v; }
static void putle(uint8_t *p,uint64_t v,unsigned n)
{ for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i)); }
static void ring_in(uint8_t *ring,unsigned head,unsigned count,const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;++i)ring[(head+count+i)%VSOCK_STREAM_BUFFER]=p[i]; }
static void ring_out(const uint8_t *ring,unsigned head,uint8_t *p,size_t n)
{ for(size_t i=0;i<n;++i)p[i]=ring[(head+i)%VSOCK_STREAM_BUFFER]; }
static int fault(struct VsockStream *s)
{ s->state=VS_FAILED;s->failed=1;s->reset=1;return 0; }
int VsockStreamInit(struct VsockStream *s,uint64_t cid,uint32_t port,uint64_t peer,uint32_t peer_port)
{
    if(!s || cid<=2 || cid>=UINT32_MAX || peer<2 || peer>=UINT32_MAX || !port || !peer_port)return 0;
    memset(s,0,sizeof(*s));s->cid=cid;s->peer=peer;s->port=port;s->peer_port=peer_port;
    s->state=VS_CONNECTING;s->request=1;return 1;
}
size_t VsockStreamWrite(struct VsockStream *s,const void *p,size_t n)
{
    if(!s || !p || s->state!=VS_OPEN || (s->peer_shutdown&1))return 0;
    if(n>VSOCK_STREAM_BUFFER-s->tx_count)n=VSOCK_STREAM_BUFFER-s->tx_count;
    ring_in(s->tx,s->tx_head,s->tx_count,p,n);s->tx_count+=(unsigned)n;return n;
}
size_t VsockStreamRead(struct VsockStream *s,void *p,size_t n)
{
    if(!s || !p)return 0;
    if(n>s->rx_count)n=s->rx_count;
    ring_out(s->rx,s->rx_head,p,n);s->rx_head=(s->rx_head+n)%VSOCK_STREAM_BUFFER;
    s->rx_count-=(unsigned)n;s->rx_fwd+=(uint32_t)n;
    if(n && s->state!=VS_CLOSED && s->state!=VS_FAILED)s->credit=1;
    return n;
}
int VsockStreamFeed(struct VsockStream *s,const uint8_t *p,size_t n)
{
    if(!s || !p)return 0;
    if(n<44 || n>VSOCK_STREAM_PACKET)return fault(s);
    if(getle(p,8)!=s->peer || getle(p+8,8)!=s->cid ||
       getle(p+16,4)!=s->peer_port || getle(p+20,4)!=s->port)return -1;
    uint32_t len=getle(p+24,4),op=getle(p+30,2),flags=getle(p+32,4);
    if(getle(p+28,2)!=1 || len!=n-44 || op<1 || op>7 ||
       (op!=5 && len) || (op!=4 && flags) || (op==4 && (flags&~3u)))return fault(s);
    if(op==3) { if(s->state!=VS_CLOSING)s->failed=1;s->state=VS_CLOSED;s->reset=s->credit=s->request=0;s->tx_count=0;return 1; }
    if(s->state==VS_CLOSED || s->state==VS_FAILED)return fault(s);
    uint32_t alloc=getle(p+36,4),fwd=getle(p+40,4);
    /* Unsigned subtraction handles wrap, but never credit bytes not sent. */
    if(alloc>INT32_MAX || (uint32_t)(fwd-s->peer_fwd)>(uint32_t)(s->tx_sent-s->peer_fwd))return fault(s);
    if(alloc!=s->peer_alloc || fwd!=s->peer_fwd)s->queried=0;
    s->peer_alloc=alloc;s->peer_fwd=fwd;
    if(op==2) {
        if(s->state!=VS_CONNECTING || s->request)return fault(s);
        s->state=VS_OPEN;return 1;
    }
    if(s->state!=VS_OPEN && s->state!=VS_CLOSING)return fault(s);
    switch(op) {
    case 5:
        if((s->peer_shutdown&2) || len>VSOCK_STREAM_BUFFER-s->rx_count)return fault(s);
        ring_in(s->rx,s->rx_head,s->rx_count,p+44,len);s->rx_count+=len;return 1;
    case 6:s->queried=0;return 1;
    case 7:s->credit=1;return 1;
    case 4:
        s->peer_shutdown|=flags;
        if(s->peer_shutdown&1)s->tx_count=0;
        if(s->peer_shutdown==3) { s->reset=1;s->state=VS_CLOSING; }
        return 1;
    default:return fault(s);
    }
}
void VsockStreamShutdown(struct VsockStream *s)
{ if(s && s->state==VS_OPEN) { s->state=VS_CLOSING;s->shutdown=1; } }
void VsockStreamAbort(struct VsockStream *s)
{ if(s) { s->state=VS_FAILED;s->failed=1;s->reset=1; } }
int VsockStreamPump(struct VsockStream *s,int (*emit)(void *,const uint8_t *,size_t),void *context)
{
    if(!s || !emit)return -1;
    uint8_t p[VSOCK_STREAM_PACKET];unsigned op=0,len=0;
    uint32_t outstanding=s->tx_sent-s->peer_fwd;
    uint32_t available=outstanding<s->peer_alloc?s->peer_alloc-outstanding:0;
    if(s->reset)op=3;
    else if(s->state==VS_CLOSED || s->state==VS_FAILED)return 0;
    else if(s->request)op=1;
    else if(s->tx_count && available && !(s->peer_shutdown&1)) {
        op=5;len=s->tx_count;
        if(len>VSOCK_STREAM_PACKET-44)len=VSOCK_STREAM_PACKET-44;
        if(len>available)len=available;
    } else if(s->credit)op=6;
    else if(s->tx_count && !s->queried)op=7;
    else if(s->shutdown && !s->tx_count)op=4;
    if(!op)return 0;
    memset(p,0,44);putle(p,s->cid,8);putle(p+8,s->peer,8);
    putle(p+16,s->port,4);putle(p+20,s->peer_port,4);putle(p+24,len,4);
    putle(p+28,1,2);putle(p+30,op,2);putle(p+32,op==4?3:0,4);
    putle(p+36,VSOCK_STREAM_BUFFER,4);putle(p+40,s->rx_fwd,4);
    if(len)ring_out(s->tx,s->tx_head,p+44,len);
    int result=emit(context,p,44+len);
    if(result<0) { s->state=VS_FAILED;s->failed=1;return -1; }
    if(!result)return 0;
    switch(op) {
    case 1:s->request=0;break;
    case 3:s->reset=0;s->state=VS_CLOSED;s->tx_count=0;break;
    case 4:s->shutdown=0;break;
    case 5:s->tx_head=(s->tx_head+len)%VSOCK_STREAM_BUFFER;s->tx_count-=len;s->tx_sent+=len;s->credit=0;break;
    case 6:s->credit=0;break;
    case 7:s->queried=1;break;
    }
    return 1;
}
