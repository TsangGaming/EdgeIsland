"""Inspect PE32+ headers, version info, installer payloads and release hashes using the standard library."""
import argparse
import hashlib
import os
from pathlib import Path
from struct import unpack_from
from xml.etree import ElementTree
import resources

def require(condition, message):
    if not condition:
        raise ValueError(message)
def u16(data, n):
    return unpack_from("<H", data, n)[0]
def u32(data, n):
    return unpack_from("<I", data, n)[0]
class PE:
    def __init__(self, data):
        self.data = data
        require(data[:2] == b"MZ", "Missing DOS header")
        pe = u32(data, 60)
        require(data[pe:pe+4] == b"PE\0\0" and u16(data, pe+4) == 0x8664, "Expected x64 PE")
        h = pe+24
        require(u16(data, h) == 0x20b and u16(data, h+68) == 2, "Expected PE32+ Windows GUI executable")
        require(u16(data, h+70) & 0x160 == 0x160, "Missing DEP or ASLR flags")
        self.sections = []
        start = h + u16(data, pe+20)
        for i in range(u16(data, pe+6)):
            s = start+i*40
            vsize, rva, size, offset = unpack_from("<4I", data, s+8)
            self.sections.append((rva, max(vsize,size), offset, size))
        entry = self.offset(u32(data,h+16))
        require(0 < entry < len(data), "Entry point outside image")
        self.base = self.offset(u32(data,h+112+2*8))
        self.import_rva = u32(data,h+112+8)
        cert, size = unpack_from("<II", data, h+112+4*8)
        self.has_certificate = bool(cert and size)
        if self.has_certificate:
            require(cert+size <= len(data) and u16(data,cert+6)==2, "Invalid certificate table")
    def offset(self, rva):
        for base, length, offset, raw_size in self.sections:
            if base <= rva < base+length and rva-base < raw_size:
                return offset + rva-base
        raise ValueError("Unmapped resource or entry point")
    def resource(self, resource_type, name):
        pos = self.base
        for key in (resource_type, name, 0x409):
            named, numeric = unpack_from("<HH",self.data,pos+12)
            entries = dict(unpack_from("<II",self.data,pos+16+i*8) for i in range(named+numeric))
            require(key in entries, f"Missing resource {resource_type}:{name}")
            target = entries[key]
            pos = self.base + (target & 0x7fffffff)
        rva,length = unpack_from("<II",self.data,pos)
        start = self.offset(rva)
        require(start+length <= len(self.data), "Resource extends past image")
        return self.data[start:start+length]
    def imports(self):
        result = {}
        pos = self.offset(self.import_rva)
        while True:
            lookup, _, _, name, table = unpack_from("<5I", self.data, pos)
            if not name:
                return result
            library = self.string_at(name)
            entries = result.setdefault(library, [])
            thunk = self.offset(lookup or table)
            while True:
                value = unpack_from("<Q", self.data, thunk)[0]
                if not value:
                    break
                entries.append(value & 0xffff if value >> 63 else self.string_at(value + 2))
                thunk += 8
            pos += 20
    def string_at(self, rva):
        start = self.offset(rva)
        return self.data[start:self.data.index(b"\0", start)].decode("ascii")

def check_imports(pe, kind):
    imports = pe.imports()
    if kind == "app":
        # SDK import libraries can map this call through either Shcore.dll or an API set.
        names = {symbol for library in imports.values() for symbol in library}
        require("CreateStreamOverRandomAccessStream" in names and "SHCreateStreamOverRandomAccessStream" not in names,
                "Incorrect SHCore stream import in application")
    if os.name == "nt":
        import ctypes
        for library, symbols in imports.items():
            dll = ctypes.WinDLL(library, winmode=0x800)  # LOAD_LIBRARY_SEARCH_SYSTEM32
            for symbol in symbols:
                try:
                    dll[symbol]
                except (AttributeError, OSError) as error:
                    raise ValueError(f"{kind}: Windows cannot resolve {library}!{symbol}") from error

def check(data, kind, signed):
    pe = PE(data)
    check_imports(pe, kind)
    name = resources.NAMES[kind]
    require(pe.has_certificate == signed, name + ": unexpected Authenticode certificate table")
    vi = pe.resource(16,1)
    for text in ("StringFileInfo", "Edge Island", "OriginalFilename", name, resources.VERSION):
        require(resources.wide(text) in vi, name + ": invalid version info " + text)
    manifest = ElementTree.fromstring(pe.resource(24,1))
    require(manifest.find(".//{*}assemblyIdentity").get("version") == resources.VERSION, "Manifest version mismatch")
    request = manifest.find(".//{*}requestedExecutionLevel")
    require(request is not None and request.get("level") == "asInvoker" and request.get("uiAccess") == "false",
            "Unexpected privilege manifest")
    if kind == "setup":
        dialog_id, dialog = resources.wizard_dialog()
        require(pe.resource(5, dialog_id) == dialog, "Missing or changed Setup Wizard dialog resource")
        require(any(node.get("name") == "Microsoft.Windows.Common-Controls" and node.get("version") == "6.0.0.0"
                    for node in manifest.findall(".//{*}assemblyIdentity")), "Missing Setup Wizard controls manifest")
    require(pe.resource(14,1)[:4] == b"\0\0\1\0", "Missing application icon")
    return pe

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--setup",type=Path,required=True)
    p.add_argument("--payload-dir",type=Path,required=True)
    p.add_argument("--signed",action="store_true",help="Require certificates. Chain validation is done separately on Windows.")
    p.add_argument("--outer-unsigned",action="store_true",help="Allow the outer installer to await signing when inner files are already signed.")
    args = p.parse_args()
    setup = args.setup.read_bytes()
    pe = check(setup,"setup",args.signed and not args.outer_unsigned)
    for id,kind in ((101,"app"),(102,"uninstall")):
        name = resources.NAMES[kind]
        content = pe.resource(10,id)
        check(content,kind,args.signed)
        require(content == (args.payload_dir/name).read_bytes(), "Embedded payload differs from approved artifact: " + name)
        print(name,hashlib.sha256(content).hexdigest())
    for id,name in ((103,"LICENSE"),(104,"PRIVACY.md")):
        require(pe.resource(10,id) == (resources.ROOT/name).read_bytes(), "Packaged text differs: " + name)
    print(args.setup.name,hashlib.sha256(setup).hexdigest())
if __name__=="__main__":
    main()
