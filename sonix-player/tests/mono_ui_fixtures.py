import runpy,sys,os,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
root=Path(sys.argv[1]);source=Path(__file__).resolve().parent.parent
runpy.run_path(str(source/'tests/graphite_fixtures.py'),run_name='__main__')
album=Image.new('RGB',(640,640),(170,34,12));d=ImageDraw.Draw(album);d.rectangle((80,80,310,570),fill=(40,150,80));d.rectangle((340,80,580,570),fill=(20,40,220));album.save(root/'large-album.jpg',progressive=False,quality=90)
track=root/'card/Graphite/Echoes.flac';subprocess.run(['metaflac','--remove','--block-type=PICTURE',str(track)],check=True);subprocess.run(['metaflac','--import-picture-from='+str(root/'large-album.jpg'),str(track)],check=True)
folder=root/'card/Screensaver';folder.mkdir();pic=Image.new('RGB',(480,720),(25,10,140));d=ImageDraw.Draw(pic);d.rectangle((20,40,220,690),fill=(30,210,180));d.ellipse((250,120,460,500),fill=(240,150,30));pic.save(folder/'different-photo.jpg',progressive=False,quality=90)
