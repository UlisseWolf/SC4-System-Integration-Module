import random, subprocess, lupa
from lupa import LuaRuntime
import sys
src=open(sys.argv[1],encoding='utf-8').read()
names=["healthQuality","sinRatio","natural","clients","kills","total","wcount","scount","redeemed","heaven","hell","redtotal","killstotal","imp","hound","succ","inf","poss","bap","reap","goetia","fallen","sins","nativetotal"]
fields=["HEALTH_QUALITY","SIN_RATIO","NATURAL_DEATHS","IMP_CLIENTS_THIS_CYCLE","IMP_KILLS_THIS_CYCLE","TOTAL_DEATHS_THIS_CYCLE","WINNER_COUNT_THIS_CYCLE","SINNER_COUNT_THIS_CYCLE","REDEEMED_THIS_CYCLE","WINNER_POPULATION_HEAVEN","SINNER_POPULATION_HELL","REDEEMED_TOTAL","IMP_KILLS_TOTAL","IMP_POPULATION","HELLHOUND_POPULATION","SUCCUBUS_POPULATION","INFESTOR_POPULATION","POSSESSOR_POPULATION","BAPHOMET_POPULATION","REAPER_POPULATION","GOETIA_ARS_GOETIA_POPULATION","FALLEN_ANGELS_POPULATION","SEVEN_DEADLY_SINS_POPULATION","HELL_NATIVE_TOTAL"]
bad=0; total=0
for trial in range(300):
    r=random.Random(trial)
    K=[r.uniform(0.0002,0.002),r.uniform(0,3),r.uniform(50,150),r.uniform(0.2,0.7),r.uniform(0,0.5),r.uniform(0,0.5),r.uniform(0,0.1),r.uniform(0.8,1),r.uniform(0,0.05),r.uniform(0,1),r.choice([0,3,10.7]),r.uniform(0,0.01),r.uniform(0,0.1)]
    K+= [r.uniform(0,0.3) for _ in range(8)]+[2,7]
    cycles=[(r.uniform(-20,130),r.randint(0,2_000_000),r.uniform(0,150),r.uniform(0,200),r.randint(-15,15)) for _ in range(40)]
    lua=LuaRuntime(unpack_returned_tuples=True)
    lua.execute("""
    function dofile() end
    game={}
    jit=0
    math.random=function(lo,hi) lo=(lo<0) and -math.floor(-lo) or math.floor(lo); hi=(hi<0) and -math.floor(-hi) or math.floor(hi); if jit<lo then return lo elseif jit>hi then return hi end return math.floor(jit) end
    math.randomseed=function() end
    """)
    g=lua.globals()
    kn=[("mortality_ratio","BASE_MORTALITY_RATE_PER_CAPITA"),("mortality_ratio","HEALTH_COVERAGE_WEIGHT"),("judgment_ratio","EDUCATION_REFERENCE"),("judgment_ratio","BASE_SIN_RATIO"),("judgment_ratio","CRIME_WEIGHT"),("judgment_ratio","EDUCATION_WEIGHT"),("judgment_ratio","SIN_RATIO_MIN"),("judgment_ratio","SIN_RATIO_MAX"),("imp_ratio","IMP_CLIENT_RATIO"),("imp_ratio","IMP_SUCCESS_CHANCE"),("imp_ratio","IMP_KILL_RANDOM_VARIANCE"),("imp_ratio","MAX_IMP_KILL_FRACTION_POP"),("redemption_ratio","REDEMPTION_RATIO")]+[("hell_native_ratio",x) for x in ["IMP_RATIO","HELLHOUND_RATIO","SUCCUBUS_RATIO","INFESTOR_RATIO","POSSESSOR_RATIO","BAPHOMET_RATIO","REAPER_RATIO","GOETIA_ARS_GOETIA_RATIO"]]+[("hell_native_fixed","FALLEN_ANGELS"),("hell_native_fixed","SEVEN_DEADLY_SINS")]
    for t in set(a for a,_ in kn): lua.execute(f"{t}={{}}")
    for (t,f),v in zip(kn,K): lua.execute(f"{t}.{f}={v!r}")
    lua.execute(src)
    inp=" ".join(repr(x) for x in K)+f"\n{len(cycles)}\n"+"\n".join(" ".join(repr(float(x)) for x in c) for c in cycles)
    out=subprocess.run(["./harness"],input=inp,capture_output=True,text=True).stdout.strip().split("\n")
    for c,line in zip(cycles,out):
        lua.execute(f"game.g_health_coverage_p={c[0]!r}; game.g_city_r_population={c[1]}; game.ga_crime={c[2]!r}; game.ga_education={c[3]!r}; jit={c[4]}")
        g.run_afterlife_balance_variables("D")
        got=[float(x) for x in line.split()]
        exp=[float(g.afterlife_grid_var[f]) for f in fields]
        total+=1
        if any(abs(a-b)>1e-9*max(1,abs(b)) for a,b in zip(got,exp)):
            bad+=1
            if bad<4: print("MISMATCH trial",trial,[(n,a,b) for n,a,b in zip(names,got,exp) if abs(a-b)>1e-9*max(1,abs(b))][:4])
print("cycles compared:",total,"mismatches:",bad)
