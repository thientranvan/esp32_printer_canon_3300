#!/usr/bin/perl
use strict;
use warnings;
use IO::Socket::INET;
use IO::Select;
use Fcntl qw(:DEFAULT :flock);
use POSIX qw(strftime);

# ponytail: A4/cassette 1 and <=100 pages, <=20 MiB. Validate other paper sources before extending.
my $socket;
my $stationHost;
my @completedSheets=(0);my $lastCompleted=-1;
my $cache = '/Library/Caches/ESP32Printer';
my $pending = "$cache/pending.txt";
sub packet { return pack('vv', $_[0], length($_[1] // '') + 4) . ($_[1] // ''); }
sub fail { die "$_[0]\n"; }
sub write_all {
    my ($data) = @_;
    while (length $data) {
        fail('TCP write timeout') unless IO::Select->new($socket)->can_write(70);
        my $n = syswrite($socket, $data);
        fail('TCP write failed; do not resend blindly') unless $n;
        substr($data, 0, $n, '');
    }
}
sub read_exact {
    my ($size) = @_;
    my $data = '';
    while (length($data) < $size) {
        fail('TCP read timeout; result uncertain') unless IO::Select->new($socket)->can_read(70);
        my $n = sysread($socket, my $part, $size - length($data));
        fail('TCP closed; result uncertain') unless $n;
        $data .= $part;
    }
    return $data;
}
sub command {
    my ($op, $data, $reply) = @_;
    write_all(packet($op, $data));
    return '' if defined($reply) && !$reply;
    my ($actual, $length) = unpack('vv', read_exact(4));
    fail('Invalid CAPT reply') unless $actual == $op && $length >= 4 && $length <= 4096;
    return read_exact($length - 4);
}
sub ack {
    my ($op, $data) = @_;
    my $result = command($op, $data);
    fail(sprintf('Printer rejected %04X: %s', $op, unpack('H*', $result)))
        unless length($result) >= 2 && !(ord(substr($result, 0, 1)) & 128);
    return $result;
}
sub status {
    my $s = ack(0xA0A8);
    fail('Truncated extended status') unless length($s) >= 36;
    my $flags=unpack('x12v',$s);
    my @conditions=(['media-jam-error', $flags&0x100],['cover-open-error',$flags&0x4000],
      ['marker-supply-low-warning',$flags&0x2000],['media-empty-error',unpack('x8v',$s)&0x4000],
      ['other-error',ord(substr($s,2,1))&8]);
    for my $c (@conditions){print STDERR 'STATE: ',($c->[1]?'+':'-'),$c->[0],"\n";}
    if(ord(substr($s,2,1))&8){
      my $service=ack(0xA2A2);
      if(length($service)>=8 && ord(substr($service,1,1))){
        my ($code,$sub)=unpack('x4vv',$service);
        print STDERR sprintf("ERROR: Canon service E%03X %04X\n",$code,$sub);
      }
      fail('Canon service fault; inspect printer before resuming');
    }
    my @c=unpack('x14v4',$s);
    if($c[3]!=$lastCompleted && @completedSheets>1){
      my $total=$#completedSheets;my $done=$c[3]>$total?$total:$c[3];
      print STDERR 'ATTR: job-media-progress=',int(100*$done/$total),"\n";
      print STDERR "PAGE: total $completedSheets[$done]\n";
      $lastCompleted=$c[3];
    }
    print STDERR "INFO: Printer completed $c[3] image(s)\n" unless $flags&(0x100|0x4000);
    return @c;
}
sub info {
    my ($id, $stage, $page) = @_;
    my @t = localtime;
    my $data = "\0" x 72;
    substr($data, 4, 4, pack('V', $page));
    substr($data, 16, 16, pack('CCvvvvCCCCCC', $stage, 1, $id, 65116, 65116,
        $t[5] + 1900, $t[4], $t[3], $t[2], $t[1], $t[0], 1));
    return $data;
}
sub pages {
    my ($job) = @_;
    fail('Unknown Canon wrapper') unless substr($job, 0, length('dBMpM`obED|~|000ccp0')) eq 'dBMpM`obED|~|000ccp0';
    my $p = index($job, pack('vv', 0xD0A0, 44));
    fail('Missing page') if $p < 0;
    my @pages;
    my $next = sub {
        fail('Truncated packet') if $p + 4 > length($job);
        my ($op, $n) = unpack('vv', substr($job, $p, 4));
        my $h = 4;
        if ($op == 0x8200) {
            fail('Truncated raster') if $p + 8 > length($job);
            $n = unpack('V', substr($job, $p + 4, 4)); $h = 8;
        }
        fail('Invalid packet length') if $n < $h || $p + $n > length($job);
        my $data = substr($job, $p + $h, $n - $h);
        $p += $n;
        return ($op, $data);
    };
    while (1) {
        my (@ops, @bands);
        my $parameters = '';
        while (1) {
            my ($op, $data) = $next->();
            push @ops, $op;
            if (($op >> 8) == 0xD0) { $parameters .= packet($op, $data); }
            elsif ($op == 0x8200) { push @bands, $data; }
            elsif ($op != 0xC0A4) { fail('Unexpected page command'); }
            last if $op == 0xD0A2;
        }
        fail('Invalid page sequence') unless @bands && @ops == @bands + 5 &&
            join(',', @ops[0..2]) eq join(',', 0xD0A0, 0xD0A4, 0xD0A1) &&
            join(',', @ops[-2..-1]) eq join(',', 0xC0A4, 0xD0A2) &&
            !grep { $_ != 0x8200 } @ops[3..$#ops-2];
        fail('Expected LBP3300 A4') unless substr($parameters, 0, 4) eq pack('vv', 0xD0A0, 44) &&
            substr($parameters, 6, 3) eq "\x94\x2a\x02";
        my ($op, $data) = $next->();
        fail('Missing page trailer') unless $op == 4 && length($data) == 4;
        push @pages, [$parameters, \@bands];
        fail('Maximum 100 pages') if @pages > 100;
        ($op, $data) = $next->();
        return @pages if $op == 3 && !length($data) && $p == length($job);
        fail('Invalid inter-page metadata') unless $op == 2 && length($data) <= 4092;
    }
}
sub plan_pages {
    my ($pages, $copies, $duplex, $collate) = @_;
    my @plan;
    my $step = $duplex ? 2 : 1;
    for (1..($collate ? $copies : 1)) {
        for (my $i = 0; $i < @$pages; $i += $step) {
            for (1..($collate ? 1 : $copies)) {
                my $paired = $duplex && $i + 1 < @$pages;
                for my $side (0..($paired ? 1 : 0)) {
                    my ($parameters, $bands) = @{$pages->[$paired && !$side ? $i + 1 : $i]};
                    substr($parameters, 9, 1, chr((ord(substr($parameters, 9, 1)) & ~6) | ($paired ? ($side ? 2 : 4) : 0)));
                    push @plan, [$parameters, $bands];
                }
            }
        }
    }
    return @plan;
}
sub check_control {
    my ($job) = @_; my $paused=0;
    require JSON::PP;
    while (1) {
        open(my $curl,'-|','/usr/bin/curl','--fail','--silent','--noproxy','*','--connect-timeout','1','--max-time','2',"http://$stationHost/printer/status") or fail('Cannot read job control');
        local $/; my $body=<$curl>;close($curl) or fail('ESP job control unavailable; outcome uncertain');
        my $s=eval { JSON::PP::decode_json($body) };fail('Invalid job control response') unless $s && ($s->{job_control}{id}//'') eq $job;
        my $request=$s->{job_control}{request}//'';
        fail('Operator cancelled submission; buffered pages may still print. Inspect before retry.') if $request eq 'cancel_requested';
        if ($request eq 'resume_requested') {ack(0xF003,pack('C',2));return;}
        return unless $request eq 'pause_requested';
        if (!$paused) {ack(0xF003,pack('C',1));$paused=1;}
        status();select(undef,undef,undef,0.25);
    }
}
sub print_pages {
    my (@pages) = @_;
    @completedSheets=(0);$lastCompleted=-1;
    for my $p (@pages){push @completedSheets,$completedSheets[-1]+((ord(substr($p->[0],9,1))&4)?0:1);}
    my $job="mac-".time."-$$";ack(0xF002,$job);
    ack(0xF001, pack('V', scalar(@pages)));
    my $model = command(0xA1A1);
    fail('Printer model mismatch') unless substr($model, 2, 2) eq "\x94\x2a";
    my @c = status();
    fail('Printer has an unresolved page') if grep { $_ != $c[0] } @c;
    sysopen(my $marker, $pending, O_WRONLY | O_CREAT | O_EXCL, 0600)
        or fail('Uncertain previous job; inspect pending.txt before resuming');
    print $marker "Job submission started; no automatic retry.\n" or fail('Cannot write pending marker');
    close($marker) or fail('Cannot save pending marker');
    ack(0xA3A2);
    my $reserved = ack(0xA2A0, pack('vvV', 0, 30, 0));
    fail('Invalid reservation') unless length($reserved) == 4;
    my $id = unpack('x2v', $reserved);
    ack(0xE1A1, info($id, 1, 0));
    ack($_) for (0xE0A3, 0xE0A2, 0xE0A4);
    ack(0xE0A5, pack('VvCCx8', 0xADEADBEE, 0, 0, 0));
    my $page = 0;
    for my $entry (@pages) {
        check_control($job);
        my ($parameters, $bands) = @$entry;
        ++$page;
        my $deadline = time + 60;
        while ($page > 2) { @c = status(); last if $c[1] >= $page - 2; fail('Engine start uncertain') if time > $deadline; select(undef, undef, undef, 0.2); }
        my ($w, $h) = unpack('vv', substr($parameters, 0x22, 4));
        my $shift = ord(substr($parameters, 0x11, 1));
        my $source = ack(0xE1A9, pack('CCvvCC', ord(substr($parameters, 8, 1)),
            ord(substr($parameters, 0x28, 1)), $w >> ($shift & 15), $h >> ($shift >> 4),
            1, ord(substr($parameters, 9, 1)) & 8));
        fail('A4 cassette 1 unavailable') unless $source eq "\0\1";
        substr($parameters, 0x0A, 1, "\1");
        command(0xD0A9, $parameters, 0);
        for my $band (@$bands) {
            for (my $i = 0; $i < length($band); $i += 4092) {
                command(0xC0A0, substr($band, $i, 4092), 0);
            }
        }
        command(0xC0A4, '', 0);
        $deadline = time + 20;
        while (1) { @c = status(); last if $c[0] >= $page; fail('Decode timeout') if time > $deadline; select(undef, undef, undef, 0.5); }
        fail('Unexpected decoded page count') unless $c[0] == $page;
        ack(0xE1A1, info($id, 2, $page));
        ack(0xE0A7, pack('v', $page));
        ack(0xE1A1, info($id, 6, $page));
        print STDERR "INFO: Submitted image $page/", scalar(@pages), "\n";
    }
    my $deadline = time + 60;
    while (1) { @c = status(); last if $c[3] >= $page; fail('Completion uncertain') if time > $deadline; sleep 1; }
    ack(0xE1A1, info($id, 4, $page));
    ack(0xE0A9, pack('v', $id));
    unlink($pending) or fail('Cannot clear pending marker');
}
# Native CUPS monitor: no USB ownership, no print submission, no queue resume.
sub monitor {
    my ($host,$queue,$once)=@_;
    fail('Invalid monitor address/queue') unless $host =~ /\A(?:[0-9]{1,3}\.){3}[0-9]{1,3}\z/ &&
      !grep({$_>255} split /\./,$host) && $queue =~ /\A(?:ESP32_esp32[0-9a-f]{12}|Canon_LBP3300_ESP32)\z/;
    require JSON::PP;require File::Temp;
    my ($file,$path)=File::Temp::tempfile('esp-status-XXXX',TMPDIR=>1,UNLINK=>1);close($file);
    my $last='';
    while(1){
      my @reasons;my $message='ESP disconnected';my $state;
      eval {
        open(my $curl,'-|','/usr/bin/curl','--fail','--silent','--noproxy','*','--connect-timeout','2','--max-time','3',"http://$host/printer/status") or die 'curl';
        my $json='';while(length($json)<=65536){my $n=read($curl,my $part,4096);last unless $n;$json.=$part;}close($curl);
        $state=JSON::PP->new->max_size(65536)->max_depth(16)->decode($json);
        die 'schema' unless ref($state) eq 'HASH' && ($state->{schema}//0)==1;
      };
      if($@ || !$state->{connected} || !$state->{responsive}){push @reasons,'offline-report';}
      elsif(!$state->{capt}{fresh}){push @reasons,'other-warning';$message='Waiting for fresh CAPT status';}
      else {
        my $p=$state->{capt};my @notes;
        for my $c (['paper_jam','media-jam-error','Paper jam'],['cover_open','cover-open-error','Cover open'],
          ['paper_required','media-empty-error','Check paper size/source'],['toner_warning','marker-supply-low-warning','Check toner'],
          ['service_error','other-error','Canon service fault']){
          if($p->{$c->[0]}){push @reasons,$c->[1];push @notes,$c->[2];}
        }
        push @notes,$p->{service_code} if ($p->{service_code}//'') =~ /\AE[0-9A-F]{3,4} [0-9A-F]{4}\z/;
        $message=@notes?join(' / ',@notes):'Printer connected';
        if($state->{active} && ($state->{total}//0)>0){$message.=' - completed '.int($state->{completed}//0).'/'.int($state->{total}).' impressions';}
      }
      my $key=join(',',@reasons).$message;
      if($key ne $last){
        # Preserve scheduler reasons (paused, backend failures, etc.). Only replace
        # reasons owned by this monitor; never resume a paused/uncertain queue.
        open(my $f,'>',$path) or die $!;
        print $f "{ OPERATION Get-Printer-Attributes\nGROUP operation-attributes-tag\nATTR charset attributes-charset utf-8\nATTR language attributes-natural-language en\nATTR uri printer-uri ipp://localhost/printers/$queue\nATTR keyword requested-attributes printer-state-reasons\nSTATUS successful-ok\n}\n";close($f);
        open(my $get,'-|','/usr/bin/ipptool','-tv','-T','3',"ipp://localhost/printers/$queue",$path) or die $!;
        my $current=do{local $/;<$get>};my $ok=close($get);
        if($ok && $current =~ /printer-state-reasons \((?:1setOf )?keyword\) = ([^\r\n]+)/){
          my %owned=map {$_=>1} qw(none offline-report other-warning media-jam-error cover-open-error media-empty-error marker-supply-low-warning other-error);
          push @reasons,grep {!$owned{$_} && /\A[a-z0-9-]+\z/} split(/,\s*/,$1);
          my $reason=join(',',@reasons)||'none';
          my $attr=sub {my($tag,$name,$value)=@_;return pack('Cn', $tag,length($name)).$name.pack('n',length($value)).$value;};
          my $body=pack('CCnNC',1,1,0x4003,1,1).
            $attr->(0x47,'attributes-charset','utf-8').$attr->(0x48,'attributes-natural-language','en').
            $attr->(0x45,'printer-uri',"ipp://localhost/printers/$queue").
            $attr->(0x42,'requesting-user-name',scalar(getpwuid($>))).pack('C',4).
            $attr->(0x41,'printer-state-message',$message);
          my $name='printer-state-reasons';for my $r (split /,/,$reason){$body.=$attr->(0x44,$name,$r);$name='';}$body.=pack('C',3);
          open($f,'>',$path) or die $!;binmode($f);print $f $body;close($f);
          # Unix socket preserves local peer credentials; TCP localhost requires
          # interactive Basic auth on macOS and cannot serve a background monitor.
          open(my $post,'-|','/usr/bin/curl','--silent','--show-error','--max-time','3','--noproxy','*',
            '--unix-socket','/var/run/cupsd','-H','Authorization: PeerCred '.getpwuid($>),'-H','Content-Type: application/ipp','--data-binary',"\@$path",'http://localhost/admin/') or die $!;
          my $reply='';read($post,$reply,4096);my $sent=close($post);
          if($sent && length($reply)>=8 && unpack('x2n',$reply)<0x100){$last=$key;}
          else {warn "CUPS status update rejected; check local administration permissions\n";}

        }
      }
      last if $once;sleep 3;
    }
}
sub main {
    if(@ARGV>=3 && ($ARGV[0] eq '--monitor' || $ARGV[0] eq '--monitor-once')){
      monitor($ARGV[1],$ARGV[2],$ARGV[0] eq '--monitor-once');return 0;
    }
    if (@ARGV == 1 && $ARGV[0] eq '--self-test') {
        my $page = "\0" x 40; substr($page, 2, 3, "\x94\x2a\x02");
        my $body = packet(0xD0A0, $page) . packet(0xD0A4, "\0" x 8) . packet(0xD0A1) .
            pack('vvV', 0x8200, 256, 11) . 'abc' . packet(0xC0A4) . packet(0xD0A2) . packet(4, "\0" x 4);
        my $job = 'dBMpM`obED|~|000ccp0' . $body . packet(2, "\0" x 108) . $body . packet(3);
        my @p = pages($job); fail('Parser self-test') unless @p == 2 && $p[1][1][0] eq 'abc';
        for my $bad (substr($job, 0, -1), $job . 'x') { eval { pages($bad) }; fail('Bad input accepted') unless $@; }
        fail('Job info layout') unless length(info(1, 2, 3)) == 72 && unpack('x4V', info(1, 2, 3)) == 3;
        my @pair = ([packet(0xD0A0, $page), ['front']], [packet(0xD0A0, $page), ['back']]);
        my @plan = plan_pages(\@pair, 2, 1, 1);
        fail('Duplex planner') unless @plan == 4 && $plan[0][1][0] eq 'back' && $plan[1][1][0] eq 'front' &&
            (ord(substr($plan[0][0], 9, 1)) & 6) == 4 && (ord(substr($plan[1][0], 9, 1)) & 6) == 2;
        push @pair, $pair[0]; @plan = plan_pages(\@pair, 1, 1, 1);
        fail('Odd duplex page') unless @plan == 3 && !(ord(substr($plan[2][0], 9, 1)) & 6);
        print "PASS: system Perl backend parser, malformed jobs, CAPT job-info layout\n";
        return;
    }
    if (!@ARGV) { print "network esp32capt \"Canon LBP3300\" \"ESP32-S3 CAPT LAN\"\n"; return; }
    fail('Invalid CUPS arguments') unless @ARGV == 5 || @ARGV == 6;
    # No proprietary Canon backchannel server: let filter use its PPD defaults.
    POSIX::close(3); POSIX::close(4);
    my ($host, $port, $id) = ($ENV{DEVICE_URI} // '') =~ m{\Aesp32capt://([a-zA-Z0-9.-]+)(?::([0-9]+))?(?:/(esp32-[0-9a-f]{12}))?/?\z};
    if ($id) { $cache .= "/$id"; $pending = "$cache/pending.txt"; }
    fail('Invalid device URI') unless $host;
    $stationHost=$host;
    my $duplex = $ARGV[4] =~ /(?:^|\s)CNDuplex=True(?:\s|$)/i;
    fail('Select Canon Print Style / Binding Location for duplex') if !$duplex && $ARGV[4] =~ /two-sided|duplex=(?:duplex|tumble)/i;
    my $collate = $ARGV[4] !~ /(?:^|\s)Collate=False(?:\s|$)/i;
    fail('Copies must be 1..99') unless $ARGV[3] =~ /\A[0-9]+\z/ && $ARGV[3] >= 1 && $ARGV[3] <= 99;
    my $input = \*STDIN;
    if (@ARGV == 6) { open($input, '<', $ARGV[5]) or fail('Cannot open spool file'); }
    binmode($input);
    my $job = '';
    while (1) {
        my $n = read($input, my $part, 65536); fail('Cannot read spool file') unless defined $n;
        last unless $n; $job .= $part; fail('Job exceeds 20 MiB') if length($job) > 20 * 1024 * 1024;
    }
    my @pages = pages($job); # Validate the entire job before touching USB.
    sysopen(my $lock, "$cache/lock", O_WRONLY | O_CREAT, 0600) or fail('Cannot open local lock');
    flock($lock, LOCK_EX) or fail('Cannot lock local printer');
    fail('Previous result uncertain; inspect pending.txt') if -e $pending;
    $socket = IO::Socket::INET->new(PeerAddr => $host, PeerPort => ($port || 9100), Proto => 'tcp', Timeout => 70)
        or fail('Cannot connect to ESP');
    $socket->autoflush(1);
    my @plan = plan_pages(\@pages, $ARGV[3], $duplex, $collate);
    print_pages(@plan);
    close($socket);
}
eval { main(); 1 } or do { print STDERR 'ERROR: ', ($@ || 'Unknown print failure'); exit 4; };
