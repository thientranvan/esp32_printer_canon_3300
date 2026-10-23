#include "station/job_control.h"
#include <cassert>
#include <cstdio>
int main(){
 PrintControl::Job j;
 assert(!j.command("stale","pause"));
 j.begin("job-1","lan",true);
 j.metadata("Tài liệu thử","MacBook","macos");assert(!strcmp(j.title,"Tài liệu thử")&&j.canPause()&&j.canCancel()&&!j.canResume()&&!j.canDelete());
 assert(!j.command("job-2","cancel"));assert(!j.command("job-1","delete"));
 assert(j.command("job-1","pause"));assert(!strcmp(j.request,"pause_requested"));assert(!j.canPause()&&j.canResume());
 assert(j.command("job-1","pause"));assert(j.command("job-1","resume"));
 assert(j.command("job-1","cancel"));assert(!j.command("job-1","resume"));
 j.finish("uncertain");assert(!j.canDelete()&&!j.canCancel());assert(!j.command("job-1","delete"));assert(!j.command("job-1","resume"));
 j.finish("resolved");assert(j.command("job-1","delete"));assert(!j.id[0]);
 j.begin("legacy","lan");assert(!j.title[0]&&!j.canPause());assert(!j.command("legacy","pause"));j.finish("completed");assert(j.command("legacy","delete"));
 assert(j.rejectBeforeStart("fault-job","lan",2,true));
 for(int retry=0;retry<50;++retry){assert(j.rejectBeforeStart("fault-job","lan",2,true));assert(!j.active && !strcmp(j.state,"failed") && j.total==2 && j.completed==0);}
 j.finish("uncertain");assert(!j.rejectBeforeStart("other","lan",3,true));assert(j.matches("fault-job"));
 puts("PASS: job identity, cooperative capability, pause/resume, cancel cannot undo, uncertain cannot delete/replay");
}
