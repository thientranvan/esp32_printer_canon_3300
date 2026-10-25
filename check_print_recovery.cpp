#include "station/print_recovery.h"
#include <cassert>
#include <cstdio>
int main(){
 using PrintRecovery::Checkpoint;
 assert(Checkpoint::valid({0,4,2,0}));
 for(auto bad:std::vector<std::vector<uint8_t>>{{},{2},{4},{4,0},{0,6}})assert(!Checkpoint::valid(bad));
 Checkpoint r;r.begin("job",{0,4,2,0},7);
 assert(!r.observe(1));r.started=true;
 assert(r.observe(1)&&r.confirmed==1);
 assert(r.observe(2)&&r.confirmed==1); // Back image alone is not a complete sheet.
 assert(!r.restart(7)); // Timeout/network loss cannot prove printer memory cleared.
 assert(r.restart(8)&&r.base==1);
 r.started=true;assert(r.observe(1)&&r.confirmed==1);
 assert(r.observe(2)&&r.confirmed==3);
 assert(!r.observe(1));assert(!r.observe(4)); // Counter regression / out of range.
 assert(r.restart(9)&&r.base==3);r.started=true;
 assert(r.observe(1)&&r.confirmed==4);assert(r.restart(10)&&r.base==4);
 assert(r.matches("job",{0,4,2,0}));assert(!r.matches("other",{0,4,2,0}));
 puts("PASS: sheet boundaries, duplex retry, repeated restart, stale epoch and invalid counters");
}
