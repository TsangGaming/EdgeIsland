"""Create human-readable release names and SHA-256 lists for a tested build."""
from pathlib import Path
import argparse
import hashlib
import shutil
from resources import VERSION

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--setup',type=Path,required=True)
    p.add_argument('--uninstaller',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--signed',action='store_true')
    args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    version=VERSION.rsplit('.',1)[0]
    tag='' if args.signed else '-UNSIGNED'
    rows=[]
    for source,role in ((args.setup,'Setup'),(args.uninstaller,'Uninstall')):
        dest=args.out/f'EdgeIsland-{version}-x64{tag}-{role}.exe'
        shutil.copyfile(source,dest)
        rows.append(f'{hashlib.sha256(dest.read_bytes()).hexdigest()}  {dest.name}')
    (args.out/'SHA256SUMS.txt').write_text('\n'.join(rows)+'\n',encoding='ascii')
if __name__=='__main__':
    main()
