#include "AfterlifeModel.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace Afterlife;
// Order of the 32 constants, shared by every mode.
static std::vector<double*> ConstantPointers(Constants& k){
  return {&k.baseMortalityRatePerCapita,&k.healthCoverageWeight,&k.educationReference,&k.baseSinRatio,&k.crimeWeight,&k.educationWeight,&k.sinRatioMin,&k.sinRatioMax,&k.impClientRatio,&k.impSuccessChance,&k.impKillRandomVariance,&k.maxImpKillFractionPop,&k.redemptionRatio,&k.impRatio,&k.hellhoundRatio,&k.succubusRatio,&k.infestorRatio,&k.possessorRatio,&k.baphometRatio,&k.reaperRatio,&k.goetiaArsGoetiaRatio,&k.fallenAngels,&k.sevenDeadlySins,&k.angelRatio,&k.cherubRatio,&k.exorcistRatio,&k.seraphimRatio,&k.seraphimCouncil,&k.lifeExpectancyDeathScale,&k.waveDeathShare,&k.maxWaveDeathFractionPop,&k.prematureSinBonus};
}
// harness --defaults : prints the 32 default constants of AfterlifeModel.h
// harness --roster   : prints the built-in news roster (characters, then events)
// harness --pick EVENT INDEX : PickNewsCharacter with math.random returning INDEX
// harness            : stdin: 32 constants, then N cycles of: 4 inputs + jitter
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
  // each cycle: health coverage, residents, crime, education, life expectancy, jitter, wave count, then per wave:
  // population, education, premature (0/1)
  for(int i=0;i<n;i++){ GameInputs g; double j; int nw;
    if(scanf("%lf %lf %lf %lf %lf %lf %d",&g.healthCoverage,&g.residentPopulation,&g.crime,&g.education,&g.lifeExpectancy,&j,&nw)!=7) return 1;
    std::vector<DeathWave> waves(nw);
    for(DeathWave& w:waves){ int p; if(scanf("%lf %lf %d",&w.population,&w.education,&p)!=3) return 1; w.premature=p!=0; }
    Step(s,g,waves,k,[&](int lo,int hi){ return std::max(lo,std::min(hi,(int)j)); });
    double v[]={s.healthQuality,s.sinRatio,s.naturalDeaths,s.impClientsThisCycle,s.impKillsThisCycle,s.totalDeathsThisCycle,s.winnerCountThisCycle,s.sinnerCountThisCycle,s.redeemedThisCycle,s.winnerPopulationHeaven,s.sinnerPopulationHell,s.redeemedTotal,s.impKillsTotal,s.impPopulation,s.hellhoundPopulation,s.succubusPopulation,s.infestorPopulation,s.possessorPopulation,s.baphometPopulation,s.reaperPopulation,s.goetiaArsGoetiaPopulation,s.fallenAngelsPopulation,s.sevenDeadlySinsPopulation,s.hellNativeTotal,s.angelPopulation,s.cherubPopulation,s.exorcistPopulation,s.seraphimPopulation,s.seraphimCouncilPopulation,s.heavenNativeTotal,s.lifeExpectancy,s.baseDeaths,s.waveDeaths,s.prematureDeaths};
    for(double x:v) printf("%.17g ",x);
    printf("\n"); }
}
