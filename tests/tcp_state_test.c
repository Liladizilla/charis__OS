/* Focused host-side tests for TCP ACK and FIN sequencing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/tcp.c"

static net_interface_t iface;
static int connect_count, close_count;
static bool connected, closed_cleanly;
net_interface_t* net_get_interface(void) { return &iface; }
int net_ipv4_send(const u8 ip[4], u8 proto, const void* p, usize n) {
    (void)ip; (void)proto; (void)p; return (int)n;
}
void* kmemset(void* d, u8 v, usize n) { return memset(d, v, n); }
void* kmemcpy(void* d, const void* s, usize n) { return memcpy(d, s, n); }
static void on_connect(bool ok, void* ctx) { (void)ctx; connect_count++; connected=ok; }
static void on_close(bool ok, void* ctx) { (void)ctx; close_count++; closed_cleanly=ok; }
static void put16(u8* p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void put32(u8* p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static void check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static usize segment(u8* p,u32 seq,u32 ack,u8 flags){
    memset(p,0,20); put16(p,80); put16(p+2,local_port);
    put32(p+4,seq); put32(p+8,ack); p[12]=0x50; p[13]=flags;
    put16(p+14,4096); put16(p+16,tcp_checksum_value(remote_ip,iface.ip_addr,p,20));
    return 20;
}
static void connect_peer(void){
    memset(&iface,0,sizeof(iface)); iface.initialized=true;
    iface.ip_addr[0]=192;iface.ip_addr[1]=168;iface.ip_addr[2]=1;iface.ip_addr[3]=20;
    remote_ip[0]=203;remote_ip[1]=0;remote_ip[2]=113;remote_ip[3]=9;
    state=TCP_CLOSED;initialized=false;next_local_port=49152;
    connect_count=close_count=0;connected=closed_cleanly=false;
    check(net_tcp_init(),"init");
    check(net_tcp_connect(remote_ip,80,on_connect,0,on_close,0),"connect start");
    net_tcp_poll();
    u8 p[20]; usize n=segment(p,7000,local_seq,TCP_FLAG_SYN|TCP_FLAG_ACK);
    net_tcp_input(remote_ip,iface.ip_addr,p,n);
    check(state==TCP_ESTABLISHED && connect_count==1 && connected,"handshake");
}
static void test_ack_must_match_outstanding_segment(void){
    connect_peer(); const char data[]="hello";
    check(net_tcp_send(data,5)==5 && tx_pending,"send pending");
    u8 p[20]; usize n=segment(p,remote_seq,local_seq-1,TCP_FLAG_ACK);
    net_tcp_input(remote_ip,iface.ip_addr,p,n);
    check(tx_pending,"partial ACK must not clear the segment");
    n=segment(p,remote_seq,local_seq,TCP_FLAG_ACK);
    net_tcp_input(remote_ip,iface.ip_addr,p,n);
    check(!tx_pending,"exact ACK clears the segment");
}
static void test_fin_must_be_in_order(void){
    connect_peer();u8 p[20];
    usize n=segment(p,remote_seq+3,local_seq,TCP_FLAG_FIN|TCP_FLAG_ACK);
    net_tcp_input(remote_ip,iface.ip_addr,p,n);
    check(state==TCP_ESTABLISHED && close_count==0,"out-of-order FIN ignored");
    n=segment(p,remote_seq,local_seq,TCP_FLAG_FIN|TCP_FLAG_ACK);
    net_tcp_input(remote_ip,iface.ip_addr,p,n);
    check(state==TCP_CLOSED && close_count==1 && closed_cleanly,"in-order FIN accepted");
}
int main(void){
    test_ack_must_match_outstanding_segment();
    test_fin_must_be_in_order();
    puts("TCP sequence tests passed"); return 0;
}
