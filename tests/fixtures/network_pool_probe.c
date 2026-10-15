/* SPDX-License-Identifier: MIT
 * Hardware pool fixture: two live endpoints, echoing 2048-byte TCP / 1200-byte UDP rounds.
 * POOL_MODE=0 TCP+UDP, 1 TCP+TCP, 2 UDP+UDP. Peer keeps TCP connections open.
 * Idempotent UDP echo may rebuild at most 3 times on interface errors.
 * Allocation failures stay fatal; this is not general business replay.
 * Build manifests/peer addresses and raw evidence belong outside the repo.
 */
#include <aster_imports.h>
#ifndef NETWORK_HOST
#define NETWORK_HOST "127.0.0.1"
#endif
#ifndef POOL_MODE
#define POOL_MODE 0
#endif
static const char host[]=NETWORK_HOST;
static int32_t handles[2];
static uint8_t output[2][2048],input[2048];
static uint32_t sent[2],received[2],deadline[2],rounds[2],stale;
static uint32_t retries[2],retry_total,rebuilds[2],retry_at[2];
static unsigned kind(unsigned i) { return POOL_MODE==1?1:POOL_MODE==2?2:i+1; }
static void fail(const char *stage,int rc)
{
 aster_logf(ASTER_LOG_ERROR,"POOLAPP FAIL %s rc=%d rounds=%u/%u",stage,rc,rounds[0],rounds[1]);aster_app_exit(1);
}
int aster_app_init(void)
{
 for(unsigned i=0;i<2;++i)for(unsigned n=0;n<2048;++n)output[i][n]=(uint8_t)(n+i*17);
 aster_logf(ASTER_LOG_INFO,"POOLAPP mode=%u start",POOL_MODE);return 0;
}
void aster_app_event(const aster_event_t *event)
{
 if(event->type!=ASTER_EVENT_FRAME)return;
 uint32_t now=aster_time_now_ms();
 for(unsigned i=0;i<2;++i){
  unsigned k=kind(i);unsigned length=k==1?2048:1200;
  if(!handles[i]){
   if(rebuilds[i]){
    aster_network_info_t info;int rc=aster_network_get_info(&info,sizeof(info));
    if(now-deadline[i]>10000){fail("rebuild deadline",ASTER_ETIMEDOUT);return;}
    if(rc==ASTER_EAGAIN||(int32_t)(now-retry_at[i])<0)continue;
    if(rc){fail("interface",rc);return;}
    if(!(info.flags&ASTER_NETWORK_IF_IPV4))continue;
   }
   int rc=aster_network_open(k,k==1?host:NULL,k==1?sizeof(host)-1:0,k==1?17780:0,k==1?10000:0);
   if(rc==ASTER_EAGAIN||(rc==ASTER_ENOSPC&&rebuilds[i]&&now-deadline[i]<5000))continue;if(rc<0){fail("open",rc);return;}handles[i]=rc;deadline[i]=now;
  }
  aster_network_status_t status;int rc=aster_network_status(handles[i],&status,sizeof(status));
  if(rc==ASTER_EAGAIN)continue;
  if(!rc&&k==2&&status.state==ASTER_NETWORK_FAILED&&
     (status.error==ASTER_EINVAL||status.error==ASTER_ENETUNREACH||status.error==ASTER_ECONNRESET)){
   if(rebuilds[i]==3){fail("udp rebuilds",status.error);return;}
   rc=aster_network_close(handles[i]);if(rc==ASTER_EAGAIN)continue;
   if(rc){fail("rebuild close",rc);return;}
   ++rebuilds[i];handles[i]=0;sent[i]=received[i]=retries[i]=0;
   deadline[i]=now;retry_at[i]=now+250;
   aster_logf(ASTER_LOG_WARN,"POOLAPP UDP rebuild slot=%u error=%d count=%u",i,status.error,rebuilds[i]);continue;
  }
  if(rc||status.state==ASTER_NETWORK_FAILED||status.state==ASTER_NETWORK_CANCELLED){fail("state",rc?rc:status.error);return;}
  if(k==2&&now-deadline[i]>1500){
   if(retries[i]==3){fail("udp retries",ASTER_ETIMEDOUT);return;}
   ++retries[i];++retry_total;sent[i]=received[i]=0;deadline[i]=now;
  }
  if(now-deadline[i]>10000){fail("deadline",ASTER_ETIMEDOUT);return;}
  if(status.state!=ASTER_NETWORK_READY)continue;
  if(sent[i]<length){
   rc=aster_network_send(handles[i],output[i]+sent[i],length-sent[i],k==2?host:NULL,k==2?sizeof(host)-1:0,k==2?17781:0);
   if(rc==ASTER_EAGAIN)continue;if(rc<0){fail("send",rc);return;}sent[i]+=rc;
  }
  aster_network_packet_t packet;
  rc=aster_network_recv(handles[i],input,length-received[i],&packet,sizeof(packet));
  if(rc==ASTER_EAGAIN)continue;
  if(rc<=0){fail("recv",rc);return;}
  if(k==2&&(rc!=1200||memcmp(input+1196,output[i]+1196,4))){++stale;continue;}
  if(memcmp(input,output[i]+received[i],rc)){fail("content",rc);return;}
  received[i]+=rc;
  if(sent[i]==length&&received[i]==length){
   ++rounds[i];sent[i]=received[i]=0;deadline[i]=now;retries[i]=0;
   uint32_t tag=rounds[i];memcpy(output[i]+1196,&tag,sizeof(tag));
   if(rounds[i]%100==0)aster_logf(ASTER_LOG_INFO,"POOLAPP mode=%u rounds=%u/%u stale=%u retries=%u",POOL_MODE,rounds[0],rounds[1],stale,retry_total);
  }
 }
}
/* Runtime-owned close on stop is deliberately exercised by the test driver. */
