#pragma once
#include "job_control.h"
static portMUX_TYPE jobLock=portMUX_INITIALIZER_UNLOCKED;
static PrintControl::Job currentJob;
static PrintControl::Job jobSnapshot() {portENTER_CRITICAL(&jobLock);auto j=currentJob;portEXIT_CRITICAL(&jobLock);return j;}
static bool jobCommand(const String &id,const String &action) {portENTER_CRITICAL(&jobLock);bool ok=currentJob.command(id.c_str(),action.c_str());portEXIT_CRITICAL(&jobLock);return ok;}
static void jobBegin(const char *id,const char *source,bool control=false) {portENTER_CRITICAL(&jobLock);currentJob.begin(id,source,control);portEXIT_CRITICAL(&jobLock);}
static void jobFinish(const char *state) {portENTER_CRITICAL(&jobLock);currentJob.finish(state);portEXIT_CRITICAL(&jobLock);}
static void jobProgress(unsigned total,unsigned completed) {portENTER_CRITICAL(&jobLock);currentJob.total=total;currentJob.completed=completed;portEXIT_CRITICAL(&jobLock);}

static void jobRejectBeforeStart(const char *id,unsigned total,bool control) {portENTER_CRITICAL(&jobLock);currentJob.rejectBeforeStart(id,"lan",total,control);portEXIT_CRITICAL(&jobLock);}
