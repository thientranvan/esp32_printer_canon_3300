#!/usr/bin/perl
use strict;
use warnings;
use IO::Socket::INET;
use IO::Select;
use Fcntl qw(:DEFAULT :flock);
use POSIX qw(strftime);

# ponytail: A4 simplex/cassette 1 and <=100 pages, <=20 MiB. Extend after hardware tests.
my $socket;
my $pending = '/Library/Caches/ESP32Printer/pending.txt';
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
    return unpack('x14v4', $s);
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
sub print_page {
    my ($parameters, $bands) = @_;
    my $model = command(0xA1A1);
    fail('Printer model mismatch') unless substr($model, 2, 2) eq "\x94\x2a";
    my @c = status();
    fail('Printer has an unresolved page') if grep { $_ != $c[0] } @c;
    sysopen(my $marker, $pending, O_WRONLY | O_CREAT | O_EXCL, 0600)
        or fail('Uncertain previous job; inspect pending.txt before resuming');
    print $marker "Page submission started; no automatic retry.\n" or fail('Cannot write pending marker');
    close($marker) or fail('Cannot save pending marker');
    ack(0xA3A2);
    my $reserved = ack(0xA2A0, pack('vvV', 0, 30, 0));
    fail('Invalid reservation') unless length($reserved) == 4;
    my $id = unpack('x2v', $reserved);
    ack(0xE1A1, info($id, 1, 0));
    ack($_) for (0xE0A3, 0xE0A2, 0xE0A4);
    ack(0xE0A5, pack('VvCCx8', 0xADEADBEE, 0, 0, 0));
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
    my $deadline = time + 20;
    while (1) { @c = status(); last if $c[0] >= 1; fail('Decode timeout') if time > $deadline; select(undef, undef, undef, 0.5); }
    my $page = $c[0];
    ack(0xE1A1, info($id, 2, $page));
    ack(0xE0A7, pack('v', $page));
    ack(0xE1A1, info($id, 6, $page));
    $deadline = time + 60;
    while (1) { @c = status(); last if $c[3] >= $page; fail('Completion uncertain') if time > $deadline; sleep 1; }
    ack(0xE1A1, info($id, 4, $page));
    ack(0xE0A9, pack('v', $id));
    unlink($pending) or fail('Cannot clear pending marker');
}
sub main {
    if (@ARGV == 1 && $ARGV[0] eq '--self-test') {
        my $page = "\0" x 40; substr($page, 2, 3, "\x94\x2a\x02");
        my $body = packet(0xD0A0, $page) . packet(0xD0A4, "\0" x 8) . packet(0xD0A1) .
            pack('vvV', 0x8200, 256, 11) . 'abc' . packet(0xC0A4) . packet(0xD0A2) . packet(4, "\0" x 4);
        my $job = 'dBMpM`obED|~|000ccp0' . $body . packet(2, "\0" x 108) . $body . packet(3);
        my @p = pages($job); fail('Parser self-test') unless @p == 2 && $p[1][1][0] eq 'abc';
        for my $bad (substr($job, 0, -1), $job . 'x') { eval { pages($bad) }; fail('Bad input accepted') unless $@; }
        fail('Job info layout') unless length(info(1, 2, 3)) == 72 && unpack('x4V', info(1, 2, 3)) == 3;
        print "PASS: system Perl backend parser, malformed jobs, CAPT job-info layout\n";
        return;
    }
    if (!@ARGV) { print "network esp32capt \"Canon LBP3300\" \"ESP32-S3 CAPT LAN\"\n"; return; }
    fail('Invalid CUPS arguments') unless @ARGV == 5 || @ARGV == 6;
    # No proprietary Canon backchannel server: let filter use its PPD defaults.
    POSIX::close(3); POSIX::close(4);
    my ($host, $port) = ($ENV{DEVICE_URI} // '') =~ m{\Aesp32capt://([a-zA-Z0-9.-]+)(?::([0-9]+))?/?\z};
    fail('Invalid device URI') unless $host;
    fail('Select one-sided A4') if $ARGV[4] =~ /two-sided|duplex=(?:duplex|tumble)/i;
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
    sysopen(my $lock, '/Library/Caches/ESP32Printer/lock', O_WRONLY | O_CREAT, 0600) or fail('Cannot open local lock');
    flock($lock, LOCK_EX) or fail('Cannot lock local printer');
    fail('Previous result uncertain; inspect pending.txt') if -e $pending;
    $socket = IO::Socket::INET->new(PeerAddr => $host, PeerPort => ($port || 9100), Proto => 'tcp', Timeout => 70)
        or fail('Cannot connect to ESP');
    $socket->autoflush(1);
    for (1..$ARGV[3]) {
        my $number = 0;
        for my $page (@pages) {
            print STDERR 'INFO: Sending page ', ++$number, '/', scalar(@pages), "\n";
            print_page(@$page);
            print STDERR "PAGE: $number 1\n";
        }
    }
    close($socket);
}
eval { main(); 1 } or do { print STDERR 'ERROR: ', ($@ || 'Unknown print failure'); exit 4; };
