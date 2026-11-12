#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/dhcp.h>

static void put32(u8* p, u32 v) {
    p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v;
}

static usize make_reply(u8* p, u8 type, u32 xid, const u8 mac[6],
                        bool with_router, bool with_timers) {
    memset(p,0,300);
    p[0]=2; p[1]=1; p[2]=6; put32(p+4,xid);
    p[16]=192; p[17]=168; p[18]=1; p[19]=120;
    memcpy(p+28,mac,6);
    p[236]=0x63; p[237]=0x82; p[238]=0x53; p[239]=0x63;
    usize n=240;
    p[n++]=53; p[n++]=1; p[n++]=type;
    p[n++]=54; p[n++]=4; p[n++]=192; p[n++]=168; p[n++]=1; p[n++]=1;
    p[n++]=1; p[n++]=4; p[n++]=255; p[n++]=255; p[n++]=255; p[n++]=0;
    if(with_router){p[n++]=3;p[n++]=4;p[n++]=192;p[n++]=168;p[n++]=1;p[n++]=1;}
    p[n++]=6;p[n++]=4;p[n++]=1;p[n++]=1;p[n++]=1;p[n++]=1;
    p[n++]=51;p[n++]=4;put32(p+n,3600);n+=4;
    if(with_timers){
      p[n++]=58;p[n++]=4;put32(p+n,1800);n+=4;
      p[n++]=59;p[n++]=4;put32(p+n,3150);n+=4;
    }
    p[n++]=255;
    return n;
}

int main(void) {
    const u8 mac[6]={0x52,0x54,0x00,0x12,0x34,0x56};
    const u32 xid=0x12345678u;
    u8 p[300]; dhcp_reply_t reply;
    usize n=make_reply(p,2,xid,mac,true,true);
    assert(dhcp_decode_reply(p,n,xid,mac,&reply));
    assert(reply.message_type==2);
    assert(reply.address[0]==192&&reply.address[1]==168&&reply.address[2]==1&&reply.address[3]==120);
    assert(reply.server[0]==192&&reply.server[3]==1);
    assert(reply.subnet_mask[0]==255&&reply.subnet_mask[3]==0);
    assert(reply.router[0]==192&&reply.router[3]==1);
    assert(reply.dns_server[0]==1&&reply.dns_server[1]==1);
    assert(reply.lease_seconds==3600&&reply.renewal_seconds==1800&&reply.rebinding_seconds==3150);

    n=make_reply(p,5,xid,mac,false,false);
    assert(dhcp_decode_reply(p,n,xid,mac,&reply));
    assert(reply.message_type==5);
    assert(reply.lease_seconds==3600);
    assert(reply.renewal_seconds==1800);
    assert(reply.rebinding_seconds==3150);
    assert(reply.router[0]==0);

    n=make_reply(p,6,xid,mac,false,false);
    assert(dhcp_decode_reply(p,n,xid,mac,&reply));
    assert(reply.message_type==6);

    assert(!dhcp_decode_reply(p,n,xid+1,mac,&reply));
    const u8 other_mac[6]={0x52,0x54,0,0x12,0x34,0x57};
    assert(!dhcp_decode_reply(p,n,xid,other_mac,&reply));
    assert(!dhcp_decode_reply(p,239,xid,mac,&reply));

    n=make_reply(p,2,xid,mac,true,true);
    p[241]=250; /* Truncated oversized first option length. */
    assert(!dhcp_decode_reply(p,n,xid,mac,&reply));

    n=make_reply(p,2,xid,mac,true,true);
    p[243]=52; /* Replace server identifier with an unknown bounded option. */
    assert(!dhcp_decode_reply(p,n,xid,mac,&reply));

    n=make_reply(p,2,xid,mac,true,true);
    p[n-1]=0; /* END option is mandatory. */
    assert(!dhcp_decode_reply(p,n,xid,mac,&reply));

    n=make_reply(p,2,xid,mac,true,true);
    p[16]=224; /* Multicast address cannot be offered to this client. */
    assert(!dhcp_decode_reply(p,n,xid,mac,&reply));

    puts("dhcp_packet_test: PASS");
    return 0;
}
