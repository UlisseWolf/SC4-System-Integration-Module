#include "AfterlifeModel.h"
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace Afterlife;
// stdin: 26 constants, then N cycles of: 4 inputs + jitter
int main(){
  Constants k; double* c[]={&k.baseMortalityRatePerCapita,&k.healthCoverageWeight,&k.educationReference,&k.baseSinRatio,&k.crimeWeight,&k.educationWeight,&k.sinRatioMin,&k.sinRatioMax,&k.impClientRatio,&k.impSuccessChance,&k.impKillRandomVariance,&k.maxImpKillFractionPop,&k.redemptionRatio,&k.impRatio,&k.hellhoundRatio,&k.succubusRatio,&k.infestorRatio,&k.possessorRatio,&k.baphometRatio,&k.reaperRatio,&k.goetiaArsGoetiaRatio,&k.fallenAngels,&k.sevenDeadlySins};
  for(double* p:c) if(scanf("%lf",p)!=1) return 1;
  State s; int n; scanf("%d",&n);
  for(int i=0;i<n;i++){ GameInputs g; double j; scanf("%lf %lf %lf %lf %lf",&g.healthCoverage,&g.residentPopulation,&g.crime,&g.education,&j);
    Step(s,g,k,[&](int lo,int hi){ return std::max(lo,std::min(hi,(int)j)); });
    double v[]={s.healthQuality,s.sinRatio,s.naturalDeaths,s.impClientsThisCycle,s.impKillsThisCycle,s.totalDeathsThisCycle,s.winnerCountThisCycle,s.sinnerCountThisCycle,s.redeemedThisCycle,s.winnerPopulationHeaven,s.sinnerPopulationHell,s.redeemedTotal,s.impKillsTotal,s.impPopulation,s.hellhoundPopulation,s.succubusPopulation,s.infestorPopulation,s.possessorPopulation,s.baphometPopulation,s.reaperPopulation,s.goetiaArsGoetiaPopulation,s.fallenAngelsPopulation,s.sevenDeadlySinsPopulation,s.hellNativeTotal};
    for(double x:v) printf("%.17g ",x); printf("\n"); }
}
