"""Runnable checks for automatic enrollment, binding and reinstall identity."""
from pathlib import Path
import subprocess,tempfile,json
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as work:
    work=Path(work)
    cpp=work/'check.cpp'
    cpp.write_text('''#define ENROLLMENT_VALIDATION_ONLY
#include "station/client_enrollment.h"
#include <cassert>
#include <string>
int main(){std::string id(32,'a'),hash(64,'b'),t(32,'X');
assert(ClientEnrollment::valid(id.c_str(),hash.c_str(),"windows","PC"));
assert(!ClientEnrollment::valid(id.c_str(),hash.c_str(),"unknown","PC"));
assert(!ClientEnrollment::valid(id.c_str(),hash.c_str(),"linux","bad\\nname"));
assert(!ClientEnrollment::hex("ABC",3));assert(ClientEnrollment::ticket(t.c_str()));
assert(!ClientEnrollment::ticket("bad ticket"));}
''')
    subprocess.run(['c++','-std=c++11','-I',str(root),str(cpp),'-o',str(work/'check')],check=True)
    subprocess.run([str(work/'check')],check=True)
    source=(root/'mac_backend.pl').read_text().split('sub cloud_config {')[0]
    source=source.replace("'/Library/Caches/ESP32Printer'",repr(str(work))).replace('die "Enrollment requires root\\n" if $>;','')
    source+='''
sub cloud_config {return;}
my $calls=0;
{no warnings 'redefine'; *enrollment_request=sub {
 my ($url,$body,$lan)=@_;$calls++;
 if($lan){die 'verifier leaked to LAN' if exists $body->{verifier};
 return {schema=>1,station_id=>'esp32-1051db786a34',client_id=>$body->{client_id},challenge=>$body->{challenge},ticket=>('T'x32)};}
 die 'invalid redemption' unless Digest::SHA::sha256_hex($body->{verifier}) eq $expected;
 return {schema=>1,station_id=>$body->{station_id},client_id=>$body->{client_id},token=>('S'x32)};
};}
'''
    # Persistent identity survives install retries; random challenge matches redeem verifier.
    source=source.replace('my $calls=0;','my $calls=0;my $expected;').replace("if($lan){die", "if($lan){$expected=$body->{challenge};die")
    source+='''
my $base=$cache;my $id='esp32-1051db786a34';mkdir "$base/$id",0700;
enroll('192.168.1.201',$id);
open(my $f,'<',"$cache/enrollment.json") or die;my $first=do{local $/;<$f>};close($f);
die 'credential permissions' if ((stat("$cache/cloud.json"))[2]&0777)!=0600;
unlink "$cache/cloud.json";$cache=$base;enroll('192.168.1.201',$id);
open($f,'<',"$cache/enrollment.json") or die;my $second=do{local $/;<$f>};close($f);
die 'identity changed' unless $first eq $second;die 'request count' unless $calls==4;
'''
    source+='''
unlink "$cache/cloud.json";$cache=$base;
{no warnings 'redefine'; *enrollment_request=sub {return {schema=>1,station_id=>'esp32-000000000001',client_id=>('a'x32),challenge=>('b'x64),ticket=>('T'x32)};};}
eval {enroll('192.168.1.201',$id)};die 'accepted mismatched station' unless $@=~/identity mismatch/;
die 'saved mismatched credential' if -e "$cache/cloud.json";
'''
    script=work/'test.pl';script.write_text(source)
    subprocess.run(['perl',str(script)],check=True)
print('PASS: enrollment validators, no LAN verifier, challenge binding, persistent identity, credential permissions')
