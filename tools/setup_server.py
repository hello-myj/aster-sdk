#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only first-boot resource server; publishing uses the local CLI only."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
from urllib.parse import parse_qs, urlsplit

SIZES=(12,14,16,18,20,24,28,32,40,48)
IDENTIFIER=re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,62}\Z')
DIGEST=re.compile(r'[a-f0-9]{64}\Z')

def encode(value):
    return (json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n').encode()

def digest_file(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def numeric_version(value):
    if not isinstance(value,str) or not re.fullmatch(r'\d+\.\d+(?:\.\d+)?',value):
        raise ValueError('Expected a numeric major.minor[.patch] version')
    parts=list(map(int,value.split('.')))
    if any(v>255 for v in parts):raise ValueError('Version component exceeds 255')
    return parts+[0]*(3-len(parts))

def base_url(value):
    url=urlsplit(value)
    if (url.scheme not in ('http','https') or not url.hostname or url.username or url.password
        or url.query or url.fragment or any(ord(c)<=32 or ord(c)>=127 for c in value)):
        raise ValueError('Expected an ASCII HTTP(S) server URL without credentials/query')
    return value.rstrip('/')

def font_metadata(path,px):
    """Validate v4 single-style cpfont spans without loading its bitmap payload."""
    size=path.stat().st_size
    with path.open('rb') as f:
        header=f.read(64)
        if len(header)!=64 or header[:8]!=b'CPFONT\0\0' or struct.unpack_from('<HH',header,8)!=(4,0) or header[12]!=1:
            raise ValueError(f'Invalid 4bpp cpfont v4: {path}')
        intervals,glyphs=struct.unpack_from('<II',header,36)
        offset=struct.unpack_from('<I',header,56)[0]
        if not intervals or not glyphs or glyphs>65536 or offset!=64 or offset+12*intervals+16*glyphs>size:
            raise ValueError(f'Invalid cpfont tables: {path}')
        ranges=f.read(12*intervals);last=-1;expected=0
        for i in range(intervals):
            first,end,index=struct.unpack_from('<III',ranges,i*12)
            if first<=last or end<first or index!=expected:raise ValueError('Invalid cpfont interval')
            last=end;expected+=end-first+1
        if expected!=glyphs:raise ValueError('Invalid cpfont glyph count')
        bitmap=64+12*intervals+16*glyphs
        for _ in range(glyphs):
            row=f.read(16)
            w,h,_,_,_,length,pos=struct.unpack('<BBHhhH2xI',row)
            if length!=(w*h+1)//2 or bitmap+pos+length>size:raise ValueError('Invalid cpfont glyph span')
    return {'kind':'font','path':f'fonts/ui_{px}.cpfont','bytes':size,'sha256':digest_file(path)}

def store_metadata(package,target):
    # The SDK verifier enforces the real ABI imports, section hashes and AOT
    # backend profile, rather than accepting a renamed file or CPU string.
    import verify_ast
    try:
        with contextlib.redirect_stdout(io.StringIO()):verify_ast.check(package,board=target)
    except (AssertionError,KeyError,ValueError) as error:
        raise ValueError(f'Invalid Store package for {target}: {error}') from error
    with package.open('rb') as f:
        header=f.read(64);offset,size=struct.unpack_from('<II',header,12)
        aot_size,aot_target=struct.unpack_from('<II',header,32)
        f.seek(offset);manifest=json.loads(f.read(size))
    if manifest.get('id')!='aster.store' or not aot_size or not aot_target:
        raise ValueError('First boot requires the matching aster.store AOT package')
    numeric_version(manifest['version'])
    return manifest,{'kind':'store','path':'store/package.ast','bytes':package.stat().st_size,'sha256':digest_file(package)}

def publish(data,fonts,package,board,target,version,market_url,screens):
    if not IDENTIFIER.fullmatch(board) or '..' in board:raise ValueError('Invalid board identifier')
    numeric_version(version);market_url=base_url(market_url)
    fonts=fonts.resolve(strict=True);package=package.resolve(strict=True)
    manifest,store=store_metadata(package,target)
    records=[];sources=[]
    for px in SIZES:
        path=fonts/f'ui_{px}.cpfont'
        if path.is_symlink() or not path.is_file():raise ValueError(f'Missing regular font: {path.name}')
        records.append(font_metadata(path,px));sources.append(path)
    records.append(store);sources.append(package)
    for i,record in enumerate(records):record['index']=i
    info={'schema':1,'resource_version':version,'board':board,'target':target,
          'abi_min':numeric_version(manifest['abi']['min']),
          'store_version':manifest['version'],'store_requires':manifest.get('requires',[]),
          'screens':screens,'market_url':market_url,'files':records,
          'store_initial_memory':manifest.get('memory',{}).get('initial',0),
          'total_bytes':sum(f['bytes'] for f in records)}
    key=hashlib.sha256(encode(info)).hexdigest()
    data=data.resolve();releases=data/'releases';releases.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='publish-',dir=data) as tmp:
        staging=Path(tmp);(staging/'payload').mkdir()
        for item,source in zip(records,sources):
            dest=staging/'payload'/str(item['index']);shutil.copyfile(source,dest)
            if digest_file(dest)!=item['sha256']:raise ValueError('Source changed during publication')
        (staging/'release.json').write_bytes(encode(info))
        if not (releases/key).exists():os.replace(staging,releases/key)
    profiles=data/'profiles';profiles.mkdir(exist_ok=True)
    # Profiles are the only mutable pointers. Interrupted publication leaves
    # either the previous complete release or the new complete release.
    with tempfile.NamedTemporaryFile(dir=profiles,delete=False) as f:
        pending=Path(f.name);f.write(encode({'release':key}))
    os.replace(pending,profiles/(board+'.json'))
    return key

class Resources:
    def __init__(self,root):self.root=root.resolve()
    def release(self,key):
        if not DIGEST.fullmatch(key):raise ValueError('Invalid release identifier')
        return json.loads((self.root/'releases'/key/'release.json').read_bytes())
    def resolve(self,query):
        def one(name):
            values=query.get(name,[])
            if len(values)!=1:raise ValueError('Missing/duplicate device field')
            return values[0]
        board,target=one('board'),one('target')
        if not IDENTIFIER.fullmatch(board) or '..' in board:raise ValueError('Invalid board')
        pointer=json.loads((self.root/'profiles'/(board+'.json')).read_bytes())
        key=pointer['release'];info=self.release(key)
        abi=int(one('abi'));width,height=int(one('width')),int(one('height'))
        features=int(one('features'))
        major,minor,patch=info['abi_min'];minimum=(major<<16)|(minor<<8)|patch
        if info['board']!=board or info['target']!=target or abi<minimum or abi>>16!=major:
            raise PermissionError('No compatible first-boot release')
        if info['screens'] and sorted([width,height]) not in [sorted(s) for s in info['screens']]:
            raise PermissionError('Display does not match the release')
        from aster_board import API
        bits=API['feature_bits']
        if any(name not in bits or not features&(1<<bits[name]) for name in info['store_requires']):
            raise PermissionError('Device lacks required Store capabilities')
        return {'schema':1,'release':key,'resource_version':info['resource_version'],
                'files':len(info['files']),'total_bytes':info['total_bytes'],
                'store_version':info['store_version'],'market_url':info['market_url']}

def handler(resources):
    class Handler(BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        server_version='AsterSetup/1';sys_version=''
        def setup(self):super().setup();self.connection.settimeout(20)
        def reply(self,status,value,extra=None):
            body=encode(value);self.send_response(status)
            for name,value in {'Content-Type':'application/json; charset=utf-8','Content-Length':str(len(body)),
                               'Connection':'close','X-Content-Type-Options':'nosniff',**(extra or {})}.items():self.send_header(name,value)
            self.end_headers();self.close_connection=True
            if self.command!='HEAD':self.wfile.write(body)
        def do_HEAD(self):self.do_GET()
        def do_GET(self):
            try:
                if len(self.path)>2048:raise ValueError('URL too long')
                url=urlsplit(self.path)
                if url.path=='/healthz':return self.reply(200,{'schema':1,'service':'aster-setup','ok':True})
                if url.path=='/v1/setup/resolve':
                    return self.reply(200,resources.resolve(parse_qs(url.query,keep_blank_values=True,max_num_fields=12)))
                match=re.fullmatch(r'/v1/setup/releases/([a-f0-9]{64})/(files|payload)/(\d+)',url.path)
                if not match:return self.reply(404,{'error':'not_found'})
                key,kind,index=match.groups();info=resources.release(key);number=int(index)
                if number>=len(info['files']):return self.reply(404,{'error':'not_found'})
                item=info['files'][number]
                if kind=='files':return self.reply(200,{'schema':1,**item,'url':f'/v1/setup/releases/{key}/payload/{number}'})
                self.send_payload(key,item)
            except FileNotFoundError:self.reply(404,{'error':'not_found'})
            except PermissionError:self.reply(409,{'error':'incompatible'})
            except (ValueError,KeyError,TypeError,OverflowError):self.reply(400,{'error':'invalid_request'})
            except (BrokenPipeError,ConnectionResetError,TimeoutError):self.close_connection=True
        def send_payload(self,key,item):
            path=resources.root/'releases'/key/'payload'/str(item['index'])
            size=path.stat().st_size
            if size!=item['bytes']:raise ValueError('Published payload changed')
            etag='"'+item['sha256']+'"';start,end=0,size-1;status=200
            value=self.headers.get('Range')
            if value and self.headers.get('If-Range',etag)==etag:
                match=re.fullmatch(r'bytes=(\d+)-(\d*)',value)
                if not match:return self.reply(416,{'error':'invalid_range'},{'Content-Range':f'bytes */{size}'})
                start=int(match[1]);end=min(int(match[2]),size-1) if match[2] else size-1
                if start>end or start>=size:return self.reply(416,{'error':'invalid_range'},{'Content-Range':f'bytes */{size}'})
                status=206
            self.send_response(status)
            headers={'Content-Type':'application/octet-stream','Content-Length':str(end-start+1),
                     'ETag':etag,'Accept-Ranges':'bytes','Connection':'close','X-Content-Type-Options':'nosniff'}
            if status==206:headers['Content-Range']=f'bytes {start}-{end}/{size}'
            for name,value in headers.items():self.send_header(name,value)
            self.end_headers();self.close_connection=True
            if self.command=='HEAD':return
            with path.open('rb') as f:
                f.seek(start);remaining=end-start+1
                while remaining:
                    chunk=f.read(min(65536,remaining))
                    if not chunk:raise ConnectionResetError('Truncated payload')
                    self.wfile.write(chunk);remaining-=len(chunk)
    return Handler

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,default=Path.home()/'.aster/setup-server')
    commands=parser.add_subparsers(dest='command',required=True)
    pub=commands.add_parser('publish')
    pub.add_argument('--fonts',type=Path,required=True);pub.add_argument('--store',type=Path,required=True)
    pub.add_argument('--board',required=True);pub.add_argument('--target',choices=['riscv','xtensa'],required=True)
    pub.add_argument('--version',default='1.0.0');pub.add_argument('--market-url',required=True)
    pub.add_argument('--screen',action='append',required=True)
    start=commands.add_parser('start');start.add_argument('--host',default='127.0.0.1');start.add_argument('--port',type=int,default=9081)
    args=parser.parse_args(argv)
    if args.command=='publish':
        screens=[]
        for value in args.screen:
            if not re.fullmatch(r'[1-9]\d{0,3}x[1-9]\d{0,3}',value):parser.error('screen must be WIDTHxHEIGHT')
            screens.append(list(map(int,value.split('x'))))
        print(publish(args.data,args.fonts,args.store,args.board,args.target,args.version,args.market_url,screens));return 0
    server=ThreadingHTTPServer((args.host,args.port),handler(Resources(args.data)));server.daemon_threads=True
    print(f'Aster setup server: http://{args.host}:{server.server_port}; data={args.data}',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
    return 0

if __name__=='__main__':raise SystemExit(main())
