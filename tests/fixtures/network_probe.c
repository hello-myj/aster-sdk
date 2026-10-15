/* SPDX-License-Identifier: MIT. ABI boundary/lifecycle hardware fixture.
 * Build with NET_PROBE_MODE: 0=bounds/foreign+clean exit, 1=permission denied,
 * 2=init failure, 3=trap, 4=DNS/raw TCP, 5=TCP partial acceptance/cancel. CLI_HANDLE and
 * OLD_HANDLE optionally test both ownership directions and a retired APP.
 * The test driver supplies manifest/configuration outside the source tree.
 */
#include <aster_imports.h>
#ifndef NET_PROBE_MODE
#define NET_PROBE_MODE 0
#endif
#ifndef CLI_HANDLE
#define CLI_HANDLE 0
#endif
#ifndef OLD_HANDLE
#define OLD_HANDLE 0
#endif
static int32_t handle;
static unsigned checks,failed,step,read_count,write_count;
#ifndef NETWORK_HOST
#define NETWORK_HOST "127.0.0.1"
#endif
static const char request[]="GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
static uint8_t data[2048];
static aster_network_status_t status;
static aster_network_packet_t packet;
static aster_network_info_t info;
#define CHECK(x) do {++checks;if(!(x)){++failed;aster_logf(ASTER_LOG_ERROR,"NETPROBE bad line=%u",__LINE__);}} while(0)
int aster_app_init(void)
{
 if(NET_PROBE_MODE==4||NET_PROBE_MODE==5){
  const char *host=NET_PROBE_MODE==4?"example.com":NETWORK_HOST;
  handle=aster_network_open(1,host,strlen(host),NET_PROBE_MODE==4?80:17780,5000);
  CHECK(handle>0);aster_logf(ASTER_LOG_INFO,"NETPROBE handle=%d",handle);
  return failed?ASTER_EINVAL:ASTER_OK;
 }
 if(NET_PROBE_MODE==1){
  CHECK(aster_network_get_info(&info,sizeof(info))==ASTER_EPERM);
  CHECK(aster_network_open(2,NULL,0,0,0)==ASTER_EPERM);
  CHECK(aster_network_status(1,&status,sizeof(status))==ASTER_EPERM);
  CHECK(aster_network_send(1,data,1,NULL,0,0)==ASTER_EPERM);
  CHECK(aster_network_recv(1,data,1,&packet,sizeof(packet))==ASTER_EPERM);
  CHECK(aster_network_cancel(1)==ASTER_EPERM);
  CHECK(aster_network_close(1)==ASTER_EPERM);
 }else{
  CHECK(aster_network_get_info(&info,sizeof(info)-1)==ASTER_EINVAL);
  CHECK(aster_network_get_info((void *)UINT32_MAX,sizeof(info))==ASTER_EINVAL);
  int info_result=aster_network_get_info(&info,sizeof(info));CHECK(info_result==0||info_result==ASTER_EAGAIN);
  if(info_result==0){CHECK(info.size==44&&info.version==1&&info.max_connections==2&&info.udp_payload_max==1200);
    aster_logf(ASTER_LOG_INFO,"NETPROBE info=%s flags=%u",info.ipv4,info.flags);}

  if(CLI_HANDLE){CHECK(aster_network_status(CLI_HANDLE,&status,sizeof(status))==ASTER_EBADF);CHECK(aster_network_close(CLI_HANDLE)==ASTER_EBADF);}
  if(OLD_HANDLE)CHECK(aster_network_status(OLD_HANDLE,&status,sizeof(status))==ASTER_EBADF);
  CHECK(aster_network_open(1,"localhost",UINT32_MAX,80,1)==ASTER_EINVAL);
  CHECK(aster_network_open(1,(void *)UINT32_MAX,9,80,1)==ASTER_EINVAL);
  CHECK(aster_network_open(2,NULL,0,0,1)==ASTER_EINVAL);
  handle=aster_network_open(2,NULL,0,0,0);CHECK(handle>0);aster_logf(ASTER_LOG_INFO,"NETPROBE handle=%d",handle);
  CHECK(aster_network_status(handle,(void *)UINT32_MAX,sizeof(status))==ASTER_EINVAL);
  CHECK(aster_network_status(handle,&status,sizeof(status)-1)==ASTER_EINVAL);
  CHECK(aster_network_recv(handle,data,sizeof(data),(void *)UINT32_MAX,sizeof(packet))==ASTER_EINVAL);
  CHECK(aster_network_recv(handle,data,sizeof(data),&packet,sizeof(packet)-1)==ASTER_EINVAL);
  CHECK(aster_network_recv(handle,data,4,(void *)data,sizeof(packet))==ASTER_EINVAL);
  CHECK(aster_network_send(handle,data,UINT32_MAX,"localhost",9,1)==ASTER_EMSGSIZE);
  aster_wifi_info_t wifi;CHECK(aster_wifi_status(&wifi,sizeof(wifi))==ASTER_EPERM);
  if(NET_PROBE_MODE==2){int h=aster_network_open(2,NULL,0,0,0);CHECK(h>0);aster_logf(ASTER_LOG_INFO,"NETPROBE initfail checks=%u failed=%u",checks,failed);return ASTER_EINVAL;}
 }
 aster_logf(ASTER_LOG_INFO,"NETPROBE mode=%u checks=%u failed=%u",NET_PROBE_MODE,checks,failed);
 return failed?ASTER_EINVAL:ASTER_OK;
}
void aster_app_event(const aster_event_t *event)
{
 if(event->type!=ASTER_EVENT_FRAME)return;
 if(NET_PROBE_MODE==4||NET_PROBE_MODE==5){
  int rc=aster_network_status(handle,&status,sizeof(status));if(rc==ASTER_EAGAIN)return;
  if(rc||status.state==ASTER_NETWORK_FAILED){aster_logf(ASTER_LOG_ERROR,"NETPROBE transport=%d",rc?rc:status.error);aster_app_exit(1);return;}
  if(NET_PROBE_MODE==5){
   if(step==0){if(status.state!=ASTER_NETWORK_READY)return;
    CHECK(aster_network_send(handle,data,1500,NULL,0,0)==1500);
    CHECK(aster_network_send(handle,data,1500,NULL,0,0)==548);
    CHECK(aster_network_send(handle,data,1,NULL,0,0)==ASTER_EAGAIN);
    CHECK(aster_network_cancel(handle)==0);step=1;return;
   }
   if(status.state!=ASTER_NETWORK_CANCELLED)return;
   CHECK(status.error==ASTER_ECANCELLED);
   CHECK(aster_network_recv(handle,data,sizeof(data),&packet,sizeof(packet))==ASTER_ECANCELLED);
  }else{
   if(status.state!=ASTER_NETWORK_READY&&status.state!=ASTER_NETWORK_EOF)return;
   if(write_count<sizeof(request)-1){rc=aster_network_send(handle,request+write_count,sizeof(request)-1-write_count,NULL,0,0);if(rc==ASTER_EAGAIN)return;if(rc<0){aster_app_exit(1);return;}write_count+=rc;}
   rc=aster_network_recv(handle,data+read_count,sizeof(data)-read_count,&packet,sizeof(packet));if(rc==ASTER_EAGAIN)return;if(rc<=0){aster_app_exit(1);return;}read_count+=rc;if(read_count<5)return;CHECK(memcmp(data,"HTTP/",5)==0);
  }
  aster_logf(ASTER_LOG_INFO,"NETPROBE mode=%u PASS checks=%u failed=%u bytes=%u",NET_PROBE_MODE,checks,failed,read_count);aster_app_exit(failed?1:0);return;
 }
 if(NET_PROBE_MODE==3){int rc=aster_network_status(handle,&status,sizeof(status));if(rc==ASTER_EAGAIN)return;if(rc==0&&status.state==ASTER_NETWORK_OPENING)return;aster_logf(ASTER_LOG_INFO,"NETPROBE deliberate trap");__builtin_trap();}
 aster_logf(ASTER_LOG_INFO,"NETPROBE PASS checks=%u",checks);aster_app_exit(failed?1:0);
}
/* Intentionally no stop/destroy cleanup: exercise runtime-owned reclamation. */
