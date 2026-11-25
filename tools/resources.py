"""Generate standard Win32 resources. MSVC consumes .res and TinyCC consumes COFF."""
from pathlib import Path
from struct import pack, pack_into, unpack_from
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.search(r'EDGE_DISPLAY_VERSION "([0-9.]+)"', (ROOT/'src/version.h').read_text()).group(1) + '.0'
PRODUCT = 'Edge Island'
NAMES = {'app':'EdgeIsland.exe', 'uninstall':'Uninstall.exe', 'setup':'EdgeIslandSetup.exe'}
def pad(data, alignment=4):
    return data + bytes((-len(data)) % alignment)
def wide(s):
    return (s+'\0').encode('utf-16-le')
def block(key, value=b'', children=(), string=False):
    size = len(value)//2 if string else len(value)
    body = pad(pack('<HHH',0,size,int(string))+wide(key)) + value
    for child in children:
        body = pad(body)+child
    return pack('<H',len(body))+body[2:]
def version_info(kind):
    a,b,c,d=map(int,VERSION.split('.'))
    ms=a<<16|b; ls=c<<16|d
    fixed=pack('<13I',0xfeef04bd,0x10000,ms,ls,ms,ls,0x3f,0,0x40004,1,0,0,0)
    strings={'CompanyName':'Edge Island contributors','FileDescription':{'app':'Edge Island desktop overlay','setup':'Edge Island Setup','uninstall':'Edge Island Uninstaller'}[kind],
             'FileVersion':VERSION,'InternalName':NAMES[kind][:-4],'OriginalFilename':NAMES[kind],
             'ProductName':PRODUCT,'ProductVersion':VERSION,'LegalCopyright':'Copyright 2026 Edge Island contributors'}
    table=block('040904B0',children=[block(k,wide(v),string=True) for k,v in strings.items()],string=True)
    return block('VS_VERSION_INFO',fixed,[block('StringFileInfo',children=[table],string=True),
          block('VarFileInfo',children=[block('Translation',pack('<HH',0x409,1200))],string=True)])

def wizard_dialog():
    """Build a standard DLGTEMPLATE and DWORD-aligned DLGITEMTEMPLATE records."""
    ids = {name: int(value) for name, value in
           re.findall(r'^#define (ID[CD]_SETUP_[A-Z]+) (\d+)$', (ROOT/'packaging/wizard.h').read_text(), re.M)}
    child, visible, tabstop, border = 0x40000000, 0x10000000, 0x00010000, 0x00800000
    # Built-in window class ordinals for BUTTON, EDIT, STATIC, and COMBOBOX.
    button, edit, static, combo = 0x80, 0x81, 0x82, 0x85
    left_no_prefix, dropdown_list, vscroll = 0x80, 3, 0x00200000
    readonly_edit, default_button, auto_checkbox = 0x0880, 1, 3
    controls = [
        (ids['IDC_SETUP_HEADING'], static, visible | left_no_prefix, (108, 22, 228, 40)),
        (ids['IDC_SETUP_BODY'], static, visible | left_no_prefix, (108, 68, 228, 50)),
        (ids['IDC_SETUP_DETAIL'], static, 0, (108, 122, 228, 13)),
        (ids['IDC_SETUP_LANGUAGE'], combo, tabstop | vscroll | dropdown_list, (108, 140, 148, 70)),
        (ids['IDC_SETUP_PATH'], edit, tabstop | border | readonly_edit, (108, 140, 228, 16)),
        (ids['IDC_SETUP_PROGRESS'], 'msctls_progress32', 0, (108, 142, 228, 12)),
        (ids['IDC_SETUP_LAUNCH'], button, tabstop | auto_checkbox, (108, 140, 228, 17)),
        (ids['IDC_SETUP_BACK'], button, visible | tabstop, (147, 199, 61, 16)),
        (1, button, visible | tabstop | default_button, (213, 199, 61, 16)),  # IDOK
        (2, button, visible | tabstop, (279, 199, 61, 16)),  # IDCANCEL
    ]
    # WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | DS_CENTER | DS_MODALFRAME | DS_SETFONT
    template = pack('<IIHhhhhHH', 0x82C808C0, 0, len(controls), 0, 0, 354, 225, 0, 0)
    template += wide('Edge Island Setup') + pack('<H', 9) + wide('Segoe UI')
    for identifier, kind, style, rect in controls:
        template = pad(template) + pack('<IIhhhhH', child | style, 0, *rect, identifier)
        template += pack('<HH', 0xFFFF, kind) if isinstance(kind, int) else wide(kind)
        template += wide('') + pack('<H', 0)
    return ids['IDD_SETUP_WIZARD'], template

def items(kind,payload):
    icon=(ROOT/'assets/island.ico').read_bytes()
    count=unpack_from('<H',icon,4)[0]; group=bytearray(icon[:6]); result={}
    for i in range(count):
        e=icon[6+16*i:22+16*i]; length,offset=unpack_from('<II',e,8)
        group.extend(e[:12]+pack('<H',i+1));result[3,i+1]=icon[offset:offset+length]
    result[14,1]=bytes(group);result[16,1]=version_info(kind)
    manifest=(ROOT/'assets/app.manifest').read_bytes().replace(b'@VERSION@', VERSION.encode('ascii'))
    if kind=='setup':
        if not payload: raise ValueError('setup requires --payload')
        dialog_id, dialog = wizard_dialog()
        result[5,dialog_id] = dialog
        manifest = manifest.replace(b'</assembly>', b'''  <dependency><dependentAssembly>
    <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0"
        processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
  </dependentAssembly></dependency>
</assembly>''')
        for i,f in enumerate([payload/NAMES['app'],payload/NAMES['uninstall'],ROOT/'LICENSE',ROOT/'PRIVACY.md']):
            result[10,101+i]=f.read_bytes()
    result[24,1] = manifest
    return dict(sorted(result.items()))
def res_record(typ,num,data,language=0x409,flags=0x1030):
    return pad(pack('<IIHHHHIHHII',len(data),32,0xffff,typ,0xffff,num,0,flags,language,0,0)+data)
def res(items):
    return res_record(0,0,b'',0,0)+b''.join(res_record(t,n,v) for (t,n),v in items.items())
def coff(items):
    # Data-entry RVA fields are relocated against the .rsrc section by the PE linker.
    tree={}
    for (typ,num),data in items.items(): tree.setdefault(typ,{})[num]={0x409:data}
    raw=bytearray(); leaves=[]; reloc=[]
    def directory(nodes):
        offset=len(raw); raw.extend(pack('<IIHHHH',0,0,0,0,0,len(nodes))+bytes(8*len(nodes)))
        for i,(key,val) in enumerate(sorted(nodes.items())):
            if isinstance(val,dict): target=directory(val)|0x80000000
            else: target=len(raw);raw.extend(bytes(16));leaves.append((target,val))
            pack_into('<II',raw,offset+16+i*8,key,target)
        return offset
    directory(tree)
    for target,data in leaves:
        pack_into('<IIII',raw,target,len(raw),len(data),0,0);reloc.append(pack('<IIH',target,0,3));raw.extend(pad(data))
    reloc=b''.join(reloc)
    header=pack('<HHIIIHH',0x8664,1,0,60+len(raw)+len(reloc),1,0,0)
    section=pack('<8sIIIIIIHHI',b'.rsrc',0,0,len(raw),60,60+len(raw),0,len(leaves),0,0x40300040)
    symbol=pack('<8sIhHBB',b'.rsrc',0,1,0,3,0)
    return header+section+raw+reloc+symbol+pack('<I',4)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('kind',choices=NAMES)
    parser.add_argument('output',type=Path);parser.add_argument('--payload',type=Path);parser.add_argument('--coff',action='store_true')
    args=parser.parse_args();data=items(args.kind,args.payload);args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(coff(data) if args.coff else res(data))
