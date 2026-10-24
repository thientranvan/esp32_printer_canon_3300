"""Check heartbeat recovery without a connected printer."""
from pathlib import Path
import subprocess
import tempfile
source=Path('station/cloud_print.h').read_text()
block=source.split('    if(!id.length()) {',1)[1].split('    if(id.length()) {',1)[0]
code=r'''
#include <cassert>
#include <cstring>
struct View {const char *state,*error;};
View view;
View cloudSnapshot(){return view;}
void cloudState(const char *state){view={state,""};}
void recovered(){
''' + block + r'''
int main(){
 view={"server_error","Heartbeat failed (HTTP -1)"};recovered();assert(!strcmp(view.state,"idle"));
 view={"connecting","Waiting for initial heartbeat"};recovered();assert(!strcmp(view.state,"idle"));
 view={"server_error","Invalid claim response"};recovered();assert(!strcmp(view.state,"server_error"));
 view={"blocked","Manual inspection required"};recovered();assert(!strcmp(view.state,"blocked"));
 view={"uncertain","USB submission may have started"};recovered();assert(!strcmp(view.state,"uncertain"));
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(code);out=Path(tmp)/'check'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS: heartbeat recovery without printer; other faults retained')
