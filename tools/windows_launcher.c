/* Cross-compiled on Linux with MinGW; the SDK itself stays in shared Python sources. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#ifndef ASTER_ENTRY
#define ASTER_ENTRY L"aster.py"
#endif
int wmain(void) {
    wchar_t root[32768], command[32768];
    DWORD n=GetModuleFileNameW(NULL,root,32768);
    if(!n||n>=32768)return 1;
    wchar_t *last=wcsrchr(root,L'\\');if(!last)return 1;*last=0;
    const wchar_t *args=GetCommandLineW();
    if(*args==L'"'){args++;while(*args&&*args!=L'"')args++;if(*args)args++;}
    else while(*args&&*args!=L' '&&*args!=L'\t')args++;
    while(*args==L' '||*args==L'\t')args++;
    int written=swprintf(command,32768,L"\"%ls\\runtime\\python.exe\" \"%ls\\tools\\%ls\" %ls",root,root,ASTER_ENTRY,args);
    if(written<0||written>=32768)return 1;
    STARTUPINFOW start={0};PROCESS_INFORMATION process={0};start.cb=sizeof(start);
    if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&start,&process)) {
        fwprintf(stderr,L"Cannot start bundled Python (Windows error %lu). Keep the complete SDK directory.\n",GetLastError());return 1;
    }
    WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return (int)code;
}
