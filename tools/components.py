"""Download pinned firmware and desktop tools from their component Releases."""
import argparse,hashlib,json,os,platform,subprocess,sys,urllib.request,zipfile,stat
from pathlib import Path,PurePosixPath
from runtime_paths import sdk_root

def family():return json.loads((sdk_root()/'contract/release-family.json').read_text(encoding='utf-8'))
def cache():return Path(os.environ.get('LOCALAPPDATA',Path.home()/'.cache'))/'Aster/components'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while data:=f.read(1024*1024):h.update(data)
    return h.hexdigest()
class SecureRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,request,fp,code,msg,headers,url):
        if not url.startswith('https://'):raise ValueError('Component download cannot downgrade HTTPS')
        return super().redirect_request(request,fp,code,msg,headers,url)
def fetch(record,directory=None):
    directory=Path(directory) if directory else cache();directory.mkdir(parents=True,exist_ok=True)
    name=record['file']
    if Path(name).name!=name or '\\' in name:raise ValueError('Invalid component filename')
    target=directory/name
    if target.is_file() and digest(target)==record['sha256']:return target
    error=None;opener=urllib.request.build_opener(SecureRedirect())
    for url in [record['url']]+record.get('mirrors',[]):
        if not url.startswith('https://'):raise ValueError('Component URL must use HTTPS')
        temporary=target.with_suffix(target.suffix+'.partial')
        try:
            with opener.open(url,timeout=90) as response,temporary.open('wb') as output:
                while data:=response.read(1024*1024):output.write(data)
            if digest(temporary)!=record['sha256']:raise ValueError('Component SHA256 mismatch')
            temporary.replace(target);return target
        except Exception as e:error=e
        finally:temporary.unlink(missing_ok=True)
    raise RuntimeError('Component download failed: '+str(error))
def firmware(argv):
    parser=argparse.ArgumentParser(prog='aster firmware');parser.add_argument('action',choices=['list','download','flash']);parser.add_argument('--board');parser.add_argument('--port');parser.add_argument('--output',type=Path);parser.add_argument('--baud',type=int,default=460800);args=parser.parse_args(argv)
    component=family()['components']['firmware'];images=component['images']
    if args.action=='list':
        for board,record in images.items():print(board,component['tag'],record['chip'])
        return 0
    if args.board not in images:parser.error('Choose --board from: '+', '.join(images))
    if args.action=='flash' and not args.port:parser.error('flash requires --port')
    record=images[args.board];path=fetch(record,args.output);print('Verified firmware:',path,flush=True)
    if args.action=='flash':
        command=[sys.executable,'-m','esptool','--chip',record['chip'],'--port',args.port,'--baud',str(args.baud)]
        if record.get('no_stub'):command+=['--no-stub']
        return subprocess.call(command+['write-flash','--flash-mode','dio','--flash-freq','80m','--flash-size','16MB',record['flash_address'],str(path)])
    return 0
def simulator(argv):
    host=('windows' if os.name=='nt' else 'linux')+'-x64';component=family()['components']['simulator']
    if host not in component['hosts']:raise RuntimeError('No desktop binary for '+host+'; see the simulator source Release')
    record=component['hosts'][host];archive=fetch(record);directory=cache()/('simulator-'+record['sha256'][:16]);entry=directory/record['entry']
    marker=directory/'verified.json'
    if not entry.is_file() or not marker.is_file():
        directory.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(archive) as z:
            seen=set()
            for item in z.infolist():
                name=PurePosixPath(item.filename)
                if name.is_absolute() or '..' in name.parts or '\\' in item.filename or ':' in item.filename or item.filename.casefold() in seen or stat.S_ISLNK(item.external_attr>>16):raise ValueError('Unsafe simulator archive member')
                seen.add(item.filename.casefold())
            z.extractall(directory)
        marker.write_text(json.dumps({'sha256':record['sha256']}))
    return subprocess.call([str(entry),*argv])
