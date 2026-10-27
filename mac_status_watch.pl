#!/usr/bin/perl
use strict;
use warnings;
$ENV{LC_ALL}='C';

# One user-session watcher for all installed stations. No USB or server reports.
sub new_jobs {
    my ($text,$seen)=@_;my %latest;
    for my $line (split /\n/,$text){
        next unless $line =~ /\A(ESP32_esp32[0-9a-f]{12})-([0-9]+)\s/;
        $latest{$1}=$2 if !exists($latest{$1}) || $2>$latest{$1};
    }
    my @queues;
    for my $queue (sort keys %latest){
        push @queues,$queue if exists($seen->{$queue}) && $latest{$queue}>$seen->{$queue};
        $seen->{$queue}=$latest{$queue} if !exists($seen->{$queue}) || $latest{$queue}>$seen->{$queue};
    }
    return @queues;
}
if(@ARGV && $ARGV[0] eq '--self-test'){
    my %seen;
    die 'Historical job opened' if new_jobs("ESP32_esp3298c377a136b0-259 user 1\n",\%seen);
    my @q=new_jobs("ESP32_esp3298c377a136b0-260 user 1\nESP32_esp321051db78a8fc-261 user 1\n",\%seen);
    die 'New job missing' unless @q==1 && $q[0] eq 'ESP32_esp3298c377a136b0';
    die 'Repeated or invalid job' if new_jobs("ESP32_esp3298c377a136b0-260 user 1\nBAD-999 user 1\n",\%seen);
    @q=new_jobs("ESP32_esp321051db78a8fc-262 user 1\n",\%seen);
    die 'Second station missing' unless @q==1 && $q[0] eq 'ESP32_esp321051db78a8fc';
    print "PASS: shared Mac watcher detects new jobs once, ignores history/invalid queues\n";exit 0;
}
die 'No arguments expected' if @ARGV;
my %seen;my $initialized=0;
while(1){
    # Seed even empty queues so their first job after Quit is detected.
    if(open(my $list,'-|','/usr/bin/lpstat','-p')){
        while(<$list>){$seen{$1}//=0 if /\Aprinter (ESP32_esp32[0-9a-f]{12})\s/;}
        close($list);
    }
    if(open(my $jobs,'-|','/usr/bin/lpstat','-W','all','-o')){
        local $/;my $text=<$jobs>//'';my $ok=close($jobs);
        if($ok){
            my @queues=new_jobs($text,\%seen);
            if($initialized){
                for my $queue (@queues){
                    my ($mac)=$queue =~ /\AESP32_esp32([0-9a-f]{12})\z/;
                    my $app="/Library/Printers/ESP32Printer/Status-esp32-$mac.app";
                    system('/usr/bin/open',$app) if -d $app;
                }
            }
            $initialized=1;
        }
    }
    select(undef,undef,undef,0.5);
}
