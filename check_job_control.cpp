#include "station/job_control.h"
#include <cassert>
#include <cstdio>
int main(){
 PrintControl::Job j;
 assert(!j.command("stale","pause"));
 j.begin("job-1","lan",true);
 assert(!j.command("job-2","cancel"));assert(!j.command("job-1","delete"));
 assert(j.command("job-1","pause"));assert(!strcmp(j.request,"pause_requested"));
 assert(j.command("job-1","pause"));assert(j.command("job-1","resume"));
 assert(j.command("job-1","cancel"));assert(!j.command("job-1","resume"));
 j.finish("uncertain");assert(!j.command("job-1","delete"));assert(!j.command("job-1","resume"));
 j.finish("resolved");assert(j.command("job-1","delete"));assert(!j.id[0]);
 j.begin("legacy","lan");assert(!j.command("legacy","pause"));j.finish("completed");assert(j.command("legacy","delete"));
 puts("PASS: job identity, cooperative capability, pause/resume, cancel cannot undo, uncertain cannot delete/replay");
}
