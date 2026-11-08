"""Run actual Perl/C# cloud conversion and safe route recovery; no real print/upload."""
from pathlib import Path
import json,struct,subprocess,tempfile,hashlib,re
from configure_cloud import validate,save

station='esp32-1051db78a8fc'
with tempfile.TemporaryDirectory() as tmp:
 folder=Path(tmp);config=json.dumps(dict(schema=1,station_id=station,token='t'*48)).encode()
 save(config,folder,station);assert (folder/'cloud.json').stat().st_mode&0o777==0o600
 for bad in [b'{}',b'x'*4097,json.dumps(dict(schema=1,station_id=station,token='bad\nheader')).encode()]:
  try:validate(bad)
  except (ValueError,TypeError):pass
  else:raise AssertionError('Bad config accepted')
 (folder/'cloud-job.json').touch()
 try:save(config,folder,station)
 except ValueError:pass
 else:raise AssertionError('Credentials overwritten with unresolved receipt')
 (folder/'cloud-job.json').unlink()
 source=Path('mac_backend.pl').read_text().split('# Native CUPS monitor:',1)[0]
 test=r'''
$cache=$ARGV[0];$stationId='esp32-1051db78a8fc';@ARGV=('285','user','Test',2,'CNDuplex');
my $p="\0" x 64;substr($p,0,4,pack('vv',0xD0A0,44));substr($p,6,3,"\x94\x2a\x02");substr($p,34,4,pack('vv',2480,3507));substr($p,44,4,pack('vv',0xD0A4,12));substr($p,56,8,pack('vvvv',0xD0A1,4,0xD0A2,4));
my @pages=([$p,['abc']],[$p,['xyz']]);
my $bytes=cloud_prepare(\@pages,2,3,1);
die 'header' unless substr($bytes,0,16) eq pack('a8vvCCv',"ESPJOB1\n",2,2,3,1,0);
die 'band' unless substr($bytes,84,7) eq pack('V',3).'abc';
require Digest::SHA;open(my $artifact,'>',"$cache/test.espjob") or die;binmode($artifact);print $artifact $bytes;close($artifact);
my $hash=Digest::SHA::sha256_hex($bytes);my @ids;my $drop=1;
no warnings 'redefine';*cloud_request=sub {
 my($c,$method,$path,$file,$actual)=@_;my($id)=$path=~m{jobs/(client-[a-f0-9]{48})};die 'id' unless $id;push @ids,$id;
 if($method eq 'PUT'){
  die 'receipt not persisted' unless -e "$cache/cloud-job.json";
  open(my $f,'<',$file) or die;binmode($f);my $body=do{local $/;<$f>};close($f);die 'raster mutated' unless $body eq $bytes && $actual eq $hash;
  if($drop-- >0){die 'lost upload response';}
 }
 return {schema=>1,station_id=>$stationId,client_job_id=>$id,sha256=>$hash,state=>'completed'};
};
my $c=cloud_config();eval {cloud_print($c,\@pages,2,3,1,'')};die 'drop not retained' unless $@ && -e "$cache/cloud-job.json";
cloud_print($c,\@pages,2,3,1,'');die 'ID changed' unless $ids[0] eq $ids[1];die 'receipt retained after success' if -e "$cache/cloud-job.json";die 'completed retry route' unless cloud_was_selected(\@pages,2,3,1);
$ARGV[0]='286';die 'new job forced to cloud' if cloud_was_selected(\@pages,2,3,1);$ARGV[0]='285';
my @big=([$p,['x' x (4*1024*1024)]]);eval{cloud_prepare(\@big,1,1,1)};die 'limit' unless $@;
print "PASS: Perl CAPT conversion, options, stable receipt after lost response, cleanup, limits\n";
'''
 f=folder/'test.pl';f.write_text(source+test)
 subprocess.run(['perl',str(f),tmp],check=True,timeout=15)
 data=(folder/'test.espjob').read_bytes()
 validator=folder/'validate.cpp';validator.write_text('''#include "station/prepared_job.h"
#include <fstream>
#include <iterator>
int main(int n,char**v){std::ifstream f(v[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});Prepared::Job j;return Prepared::parse(b.data(),b.size(),j)?0:1;}
''')
 executable=folder/'validate'
 subprocess.run(['clang++','-std=c++11','-I',str(Path.cwd()),str(validator),'-o',str(executable)],check=True)
 assert subprocess.run([str(executable),str(folder/'test.espjob')]).returncode==0
 for bad in [data[:-1],data+b'x',b'X'+data[1:],data[:80]+bytes(4)+data[84:]]:
  invalid=folder/'bad.espjob';invalid.write_bytes(bad)
  assert subprocess.run([str(executable),str(invalid)]).returncode!=0,'Invalid artifact accepted by ESP'
 print('PASS: server upload validator rejects truncation, wrong magic, trailing bytes and empty bands')

# Compile actual Windows bridge, including existing self-check and cloud checks.
s=Path('installers/esp32-printer-setup-v3.ps1').read_text(encoding='utf-8-sig')
cs=s.split("$sourceCode=@'\n",1)[1].split("\n'@",1)[0]
cs=cs.replace('__RECOVERY_SUPPORTED__','true').replace('__METADATA_SUPPORTED__','true').replace('__STATION_IP__','192.168.1.200').replace('__STATION_ID__',station).replace('__LOCAL_PORT__','19102').replace('__PRINTER_NAME_BASE64__','VGVzdA==')
extra=r'''
  byte[] prepared=CloudPrepared(pipeline);Need(System.Text.Encoding.ASCII.GetString(prepared,0,8)=="ESPJOB1\n"&&U16(prepared,8)==pipeline.Pages.Count&&prepared[12]==pipeline.Duplex,"Cloud header/options");
  Need(U32(prepared,80)==pipeline.Pages[0].Bands.Count,"Cloud band framing");
  var config=CloudJson<CloudConfig>(System.Text.Encoding.UTF8.GetBytes("{\"schema\":1,\"station_id\":\"test\",\"token\":\"token\"}"));Need(config.schema==1&&config.station_id=="test","Cloud JSON parse");
  byte[] expected=(byte[])pipeline.Pages[0].Bands[0].Clone();int cloudStart=84;Need(U32(prepared,cloudStart)==expected.Length,"Cloud raster size");
  for(int i=0;i<expected.Length;i++)Need(prepared[cloudStart+4+i]==expected[i],"Cloud raster changed");
  Console.WriteLine("PASS: C# prepared format, duplex/copies, unchanged compressed bands, bounded JSON");
'''

# Replace only network boundary: test actual receipt persistence/retry/route code.
start=cs.index(' static CloudReceipt CloudRequestOnce(');end=cs.index(' static string CloudId(',start)
cs=cs[:start]+r'''
 static List<string> cloudIds=new List<string>();static bool loseResponse=true;
 static CloudReceipt CloudRequestOnce(CloudConfig c,string method,string id,byte[] body,string hash,string title){
  Need(File.Exists(Path.Combine(Root,"cloud-job.txt")),"Receipt must precede upload");cloudIds.Add(id);
  if(loseResponse){loseResponse=false;throw new WebException("Lost upload response");}
  return new CloudReceipt{schema=1,station_id=c.station_id,client_job_id=id,sha256=hash,state="completed"};
 }
'''+cs[end:]
extra+=r'''
  CloudConfig cloud=new CloudConfig{schema=1,station_id="esp32-1051db78a8fc",token=new string('t',48)};
  CloudSend(cloud,parsed,Path.Combine(Root,"capture.capt"));
  Need(cloudIds.Count==2&&cloudIds[0]==cloudIds[1],"Dropped response changed identity");
  Need(CloudSelected(parsed,Path.Combine(Root,"capture.capt")),"Completed cloud retry must not route to LAN");
  Need(!CloudSelected(parsed,Path.Combine(Root,"new.capt")),"New job must prefer LAN");
  File.WriteAllText(Path.Combine(Root,"capture.capt"),"placeholder");
  Need(!File.Exists(Path.Combine(Root,"cloud-job.txt"))&&File.Exists(Path.Combine(Root,"cloud-completed.txt")),"Receipt finalization failed");
  Console.WriteLine("PASS: C# lost PUT response retries same ID, durable receipt, completed/new route selection");
'''

cs=cs.replace('  Console.WriteLine("PASS: parser,',extra+'  Console.WriteLine("PASS: parser,')
with tempfile.TemporaryDirectory() as tmp:
 f=Path(tmp)/'bridge.cs';f.write_text(cs);exe=Path(tmp)/'bridge.exe'
 subprocess.run(['/opt/homebrew/bin/mcs','-r:System.Runtime.Serialization','-out:'+str(exe),str(f)],check=True)
 subprocess.run(['/opt/homebrew/bin/mono',str(exe),'--self-test'],check=True,timeout=30)
print('PASS: client configuration validation, ACL mode, unresolved credential replacement guard')
