import sys, glob
from PIL import Image
d=sys.argv[1]; fs=sorted(f for f in glob.glob(d+'/*.png') if 'sheet' not in f)
if len(sys.argv)>2: fs=[f for f in fs if any(k in f for k in sys.argv[2:])]
cols=min(4,len(fs)); im=Image.new('RGB',(330*cols-10,330*((len(fs)+cols-1)//cols)-10),'white')
for i,f in enumerate(fs): im.paste(Image.open(f),((i%cols)*330,(i//cols)*330))
im.save(d+'/sheet.png'); print(fs)
