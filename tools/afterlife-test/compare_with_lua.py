# Compares src/afterlife/AfterlifeModel.cpp (through ./harness) with AFTERLIFE.lua.
#   python3 compare_with_lua.py AFTERLIFE.lua [PACK_CONSTANTS.lua]
# With PACK_CONSTANTS.lua it also checks that the DLL defaults and the built-in news roster match that file, and runs
# the cycles with the real constants loaded from it.
import random, subprocess, sys
from lupa import LuaRuntime

src = open(sys.argv[1], encoding='utf-8').read()
pack = open(sys.argv[2], encoding='utf-8').read() if len(sys.argv) > 2 else None

names=["healthQuality","sinRatio","natural","clients","kills","total","wcount","scount","redeemed","heaven","hell","redtotal","killstotal","imp","hound","succ","inf","poss","bap","reap","goetia","fallen","sins","nativetotal","angel","cherub","exorcist","seraphim","council","heaventotal","lifeexp","base","waves","premature"]
fields=["HEALTH_QUALITY","SIN_RATIO","NATURAL_DEATHS","IMP_CLIENTS_THIS_CYCLE","IMP_KILLS_THIS_CYCLE","TOTAL_DEATHS_THIS_CYCLE","WINNER_COUNT_THIS_CYCLE","SINNER_COUNT_THIS_CYCLE","REDEEMED_THIS_CYCLE","WINNER_POPULATION_HEAVEN","SINNER_POPULATION_HELL","REDEEMED_TOTAL","IMP_KILLS_TOTAL","IMP_POPULATION","HELLHOUND_POPULATION","SUCCUBUS_POPULATION","INFESTOR_POPULATION","POSSESSOR_POPULATION","BAPHOMET_POPULATION","REAPER_POPULATION","GOETIA_ARS_GOETIA_POPULATION","FALLEN_ANGELS_POPULATION","SEVEN_DEADLY_SINS_POPULATION","HELL_NATIVE_TOTAL","ANGEL_POPULATION","CHERUB_POPULATION","EXORCIST_POPULATION","SERAPHIM_POPULATION","SERAPHIM_COUNCIL_POPULATION","HEAVEN_NATIVE_TOTAL","LIFE_EXPECTANCY","BASE_DEATHS","WAVE_DEATHS","PREMATURE_DEATHS"]
kn=[("mortality_ratio","BASE_MORTALITY_RATE_PER_CAPITA"),("mortality_ratio","HEALTH_COVERAGE_WEIGHT"),("judgment_ratio","EDUCATION_REFERENCE"),("judgment_ratio","BASE_SIN_RATIO"),("judgment_ratio","CRIME_WEIGHT"),("judgment_ratio","EDUCATION_WEIGHT"),("judgment_ratio","SIN_RATIO_MIN"),("judgment_ratio","SIN_RATIO_MAX"),("imp_ratio","IMP_CLIENT_RATIO"),("imp_ratio","IMP_SUCCESS_CHANCE"),("imp_ratio","IMP_KILL_RANDOM_VARIANCE"),("imp_ratio","MAX_IMP_KILL_FRACTION_POP"),("redemption_ratio","REDEMPTION_RATIO")]+[("hell_native_ratio",x) for x in ["IMP_RATIO","HELLHOUND_RATIO","SUCCUBUS_RATIO","INFESTOR_RATIO","POSSESSOR_RATIO","BAPHOMET_RATIO","REAPER_RATIO","GOETIA_ARS_GOETIA_RATIO"]]+[("hell_native_fixed","FALLEN_ANGELS"),("hell_native_fixed","SEVEN_DEADLY_SINS")]+[("heaven_native_ratio",x) for x in ["ANGEL_RATIO","CHERUB_RATIO","EXORCIST_RATIO","SERAPHIM_RATIO"]]+[("heaven_native_fixed","SERAPHIM_COUNCIL")]+[("mortality_ratio","LIFE_EXPECTANCY_DEATH_SCALE"),("death_waves","WAVE_DEATH_SHARE"),("death_waves","MAX_WAVE_DEATH_FRACTION_POP"),("death_waves","PREMATURE_SIN_BONUS")]

PRELUDE = """
function dofile() end
function hex2dec(s) return tonumber(s, 16) end
game={}
jit=0
math.random=function(lo,hi) lo=(lo<0) and -math.floor(-lo) or math.floor(lo); hi=(hi<0) and -math.floor(-hi) or math.floor(hi); if jit<lo then return lo elseif jit>hi then return hi end return math.floor(jit) end
math.randomseed=function() end
"""

def new_lua():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(PRELUDE)
    return lua

def run(lua, K, cycles):
    """Runs the cycles in the Lua (constants already set) and in the harness with K; returns (compared, mismatches)."""
    g = lua.globals()
    def line(c):
        h,pop,crime,edu,le,jit,waves=c
        return " ".join(repr(float(x)) for x in (h,pop,crime,edu,le,jit))+f" {len(waves)} "+" ".join(f"{float(p)!r} {float(e)!r} {int(pr)}" for p,e,pr in waves)
    inp=" ".join(repr(x) for x in K)+f"\n{len(cycles)}\n"+"\n".join(line(c) for c in cycles)
    out=subprocess.run(["./harness"],input=inp,capture_output=True,text=True).stdout.strip().split("\n")
    bad=0
    for c,line in zip(cycles,out):
        waves=",".join(f"{{population={float(p)!r},education={float(e)!r},premature={'true' if pr else 'false'}}}" for p,e,pr in c[6])
        lua.execute(f"game.g_health_coverage_p={c[0]!r}; game.g_city_r_population={c[1]}; game.ga_crime={c[2]!r}; game.ga_education={c[3]!r}; game.ga_life_exp={c[4]!r}; jit={c[5]}; afterlife_waves={{{waves}}}")
        g.run_afterlife_balance_variables("D")
        got=[float(x) for x in line.split()]
        exp=[float(g.afterlife_grid_var[f]) for f in fields]
        if len(got)!=len(exp) or any(abs(a-b)>1e-9*max(1,abs(b)) for a,b in zip(got,exp)):
            bad+=1
            if bad<4: print("  MISMATCH",[(n,a,b) for n,a,b in zip(names,got,exp) if abs(a-b)>1e-9*max(1,abs(b))][:4])
    return len(cycles), bad

def random_cycles(r, n=40, pop=2_000_000):
    def waves():
        return [(r.randint(0,4000),r.uniform(0,200),r.random()<0.3) for _ in range(r.choice([0,0,1,3,8]))]
    return [(r.uniform(-20,130),r.randint(0,pop),r.uniform(0,150),r.uniform(0,200),r.choice([0,0,r.uniform(1,200),r.randint(20,120)]),r.randint(-15,15),waves()) for _ in range(n)]

failed = False

# 1) Random constants (set directly in Lua, AFTERLIFE.lua loaded after them).
bad=0; total=0
for trial in range(300):
    r=random.Random(trial)
    K=[r.uniform(0.0002,0.002),r.uniform(0,3),r.uniform(50,700),r.uniform(0.2,0.7),r.uniform(0,0.5),r.uniform(0,0.5),r.uniform(0,0.1),r.uniform(0.8,1),r.uniform(0,0.05),r.uniform(0,1),r.choice([0,3,10.7]),r.uniform(0,0.01),r.uniform(0,0.1)]
    K+=[r.uniform(0,0.3) for _ in range(8)]+[2,7]+[r.uniform(0,1.2) for _ in range(4)]+[3]+[r.uniform(0.5,2),r.uniform(0,0.3),r.uniform(0,0.05),r.uniform(0,0.3)]
    lua=new_lua()
    for t in set(a for a,_ in kn): lua.execute(f"{t}={{}}")
    for (t,f),v in zip(kn,K): lua.execute(f"{t}.{f}={v!r}")
    lua.execute(src)
    n,b=run(lua,K,random_cycles(r)); total+=n; bad+=b
print("random constants   - cycles compared:",total,"mismatches:",bad)
failed |= bad>0

if pack:
    lua=new_lua(); lua.execute(pack); lua.execute(src); g=lua.globals()
    real=[float(g[t][f]) for t,f in kn]

    # 2) DLL defaults == PACK_CONSTANTS.lua
    defaults=[float(x) for x in subprocess.run(["./harness","--defaults"],capture_output=True,text=True).stdout.split()]
    diff=[(f"{t}.{f}",d,v) for (t,f),d,v in zip(kn,defaults,real) if d!=v]
    print("defaults           - constants compared:",len(kn),"mismatches:",len(diff))
    for x in diff: print("  ",x)
    failed |= bool(diff) or len(defaults)!=len(kn)

    # 3) Built-in roster == hell_news_characters / news_ticker_event_characters
    lines=subprocess.run(["./harness","--roster"],capture_output=True,text=True).stdout.strip().split("\n")
    built_c={l.split("\t")[1]:l.split("\t")[2:] for l in lines if l.startswith("C\t")}
    built_e={l.split("\t")[1]:l.split("\t")[2:] for l in lines if l.startswith("E\t")}
    lua_c={k:[v.full_name,v.role,v.source,v.species,v.ticker_text_id] for k,v in g.hell_news_characters.items()}
    lua_e={k:[v[i] for i in range(1,len(v)+1)] for k,v in g.news_ticker_event_characters.items()}
    roster_ok = built_c==lua_c and built_e==lua_e
    print("news roster        - characters:",len(lua_c),"events:",len(lua_e),"match:",roster_ok)
    failed |= not roster_ok

    # 4) Ticker pick: same entry for every event type and index (and nil for an unknown type).
    pick_bad=0; picks=0
    for ev in list(lua_e)+["NO_SUCH_EVENT"]:
        for idx in range(1,4):
            lua.execute(f"jit={idx}")
            entry=g.get_news_ticker_character(ev)
            exp="nil" if entry is None else next(k for k,v in g.hell_news_characters.items() if v.ticker_text_id==entry.ticker_text_id)
            got=subprocess.run(["./harness","--pick",ev,str(idx)],capture_output=True,text=True).stdout.strip()
            picks+=1; pick_bad+= got!=exp
    print("ticker picks       - compared:",picks,"mismatches:",pick_bad)
    failed |= pick_bad>0

    # 5) The real constants of PACK_CONSTANTS.lua over long runs (Hell grows large enough for I.M.P. and redemption).
    bad=0; total=0
    for trial in range(100):
        r=random.Random(1000+trial)
        lua=new_lua(); lua.execute(pack); lua.execute(src)
        n,b=run(lua,real,random_cycles(r,n=400,pop=5_000_000)); total+=n; bad+=b
    print("PACK_CONSTANTS run - cycles compared:",total,"mismatches:",bad)
    failed |= bad>0

sys.exit(1 if failed else 0)
