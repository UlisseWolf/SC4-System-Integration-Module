#include "AfterlifeModel.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace Afterlife;
// Order of the 23 constants, shared by every mode.
static std::vector<double*> ConstantPointers(Constants& k){
  return {&k.baseMortalityRatePerCapita,&k.healthCoverageWeight,&k.educationReference,&k.baseSinRatio,&k.crimeWeight,&k.educationWeight,&k.sinRatioMin,&k.sinRatioMax,&k.impClientRatio,&k.impSuccessChance,&k.impKillRandomVariance,&k.maxImpKillFractionPop,&k.redemptionRatio,&k.impRatio,&k.hellhoundRatio,&k.succubusRatio,&k.infestorRatio,&k.possessorRatio,&k.baphometRatio,&k.reaperRatio,&k.goetiaArsGoetiaRatio,&k.fallenAngels,&k.sevenDeadlySins};
}
// harness --defaults : prints the 23 default constants of AfterlifeModel.h
// harness --roster   : prints the built-in news roster (characters, then events)
// harness --pick EVENT INDEX : PickNewsCharacter with math.random returning INDEX
// harness            : stdin: 23 constants, then N cycles of: 4 inputs + jitter
int main(int argc,char** argv){
  if(argc>1 && !strcmp(argv[1],"--defaults")){ Constants k; for(double* p:ConstantPointers(k)) printf("%.17g\n",*p); return 0; }
  if(argc>1 && !strcmp(argv[1],"--roster")){
    int n; const NewsCharacter* c=NewsCharacters(n);
    for(int i=0;i<n;i++) printf("C\t%s\t%s\t%s\t%s\t%s\t%s\n",c[i].key,c[i].fullName,c[i].role,c[i].source,c[i].species,c[i].tickerTextId);
    const NewsEvent* e=NewsEvents(n);
    for(int i=0;i<n;i++){ printf("E\t%s",e[i].type); for(int j=0;j<e[i].count;j++) printf("\t%s",e[i].characters[j]); printf("\n"); }
    return 0; }
  if(argc>3 && !strcmp(argv[1],"--pick")){ int idx=atoi(argv[3]);
    const NewsCharacter* c=PickNewsCharacter(argv[2],[&](int lo,int hi){ return std::max(lo,std::min(hi,idx)); });
    printf("%s\n",c?c->key:"nil"); return 0; }
  Constants k;
  for(double* p:ConstantPointers(k)) if(scanf("%lf",p)!=1) return 1;
  State s; int n; if(scanf("%d",&n)!=1) return 1;
  for(int i=0;i<n;i++){ GameInputs g; double j; if(scanf("%lf %lf %lf %lf %lf",&g.healthCoverage,&g.residentPopulation,&g.crime,&g.education,&j)!=5) return 1;
    Step(s,g,k,[&](int lo,int hi){ return std::max(lo,std::min(hi,(int)j)); });
    double v[]={s.healthQuality,s.sinRatio,s.naturalDeaths,s.impClientsThisCycle,s.impKillsThisCycle,s.totalDeathsThisCycle,s.winnerCountThisCycle,s.sinnerCountThisCycle,s.redeemedThisCycle,s.winnerPopulationHeaven,s.sinnerPopulationHell,s.redeemedTotal,s.impKillsTotal,s.impPopulation,s.hellhoundPopulation,s.succubusPopulation,s.infestorPopulation,s.possessorPopulation,s.baphometPopulation,s.reaperPopulation,s.goetiaArsGoetiaPopulation,s.fallenAngelsPopulation,s.sevenDeadlySinsPopulation,s.hellNativeTotal};
    for(double x:v) printf("%.17g ",x);
    printf("\n"); }
}
