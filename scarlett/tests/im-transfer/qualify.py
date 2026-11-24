"""Optional: pin pass-0 native containers in a local 1.0.2.1 archive, without extracting files."""
import hashlib, struct, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"native-ps"))
from local_record import inflate

pairs=(
    (271136308,'VS_Blit_8e9c2e8a_Wrapped',1440,'d3b69854f269aab074c6c683db709dbeb0b3fb55444fbe65be4a98ff858d40c2'),
    (271837884,'PS_BlitUISDRToBackbuffer_8e9c2e8a_Wrapped',2388,'08995b8a32d9c44f398c9af9abc3893eee7f5edff637493d62beb302e76e9c98'))
with open(sys.argv[1],'rb') as archive:
    for offset,name,size,sha in pairs:
        archive.seek(offset)
        label=name.encode()+b'\0'
        if archive.read(len(label)+2)!=bytes([len(label)])+label+b'2': raise ValueError(name+' record')
        n,_,duplicate,raw=struct.unpack('<4I',archive.read(16))
        if raw!=size or not 0<n==duplicate<4096: raise ValueError(name+' size')
        blob=inflate(archive.read(n),size,-15,12)
        if hashlib.sha256(blob).hexdigest()!=sha: raise ValueError(name+' digest')
        print('PASS selected container',name,sha)
