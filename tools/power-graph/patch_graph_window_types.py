# Updates graph_window_types in CAM's advisor constants (CAM_LUA_Controller.dat, Lua 0xCA63E2A3 / 0x4A5E8EF6 /
# 0xFF048A3E) for the graphs of CAM_Exemplar_Graphs.dat. game.window_graph(graph_window_types.X) opens a graph from
# advisor messages and scripts:
#  - WATER_POLLUTION (0x09) has no button any more (merged into Pollution, Graph Button Hidden): it now opens 0x08;
#  - POLLUTION (0x08), POLICE_JAILS (0x1C), FIRE_PROTECTION (0x1D), HEALTH (0x1E), POPULATION_RCI (0x1F),
#    DEMAND_FORECAST (0x20), TAX_RATES (0x21), MASS_TRANSIT (0x22), TRAFFIC_CONGESTION (0x23), POWER_GRID (0x1A) and WATER_GRID (0x1B) are added.
#   python patch_graph_window_types.py <CAM_LUA_Controller.dat> <out_dir>
# Writes <out_dir>/CAM_LUA_Controller.dat; every other entry is unchanged. Running it twice changes nothing more.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2

LUA_TYPE, LUA_GROUP, CONSTANTS = 0xCA63E2A3, 0x4A5E8EF6, 0xFF048A3E
OLD = b"   graph_window_types.WATER_POLLUTION = hex2dec('00000009')\r\n"
NEW = (b"   graph_window_types.WATER_POLLUTION = hex2dec('00000008') -- SC4SIM: merged into Pollution (0x09 has no button)\r\n"
       b"   graph_window_types.POLLUTION = hex2dec('00000008') -- SC4SIM: air, water, garbage and radiation pollution\r\n"
       b"   graph_window_types.POLICE_JAILS = hex2dec('0000001C') -- SC4SIM\r\n"
       b"   graph_window_types.FIRE_PROTECTION = hex2dec('0000001D') -- SC4SIM\r\n"
       b"   graph_window_types.HEALTH = hex2dec('0000001E') -- SC4SIM\r\n"
       b"   graph_window_types.POPULATION_RCI = hex2dec('0000001F') -- SC4SIM\r\n"
       b"   graph_window_types.DEMAND_FORECAST = hex2dec('00000020') -- SC4SIM\r\n"
       b"   graph_window_types.TAX_RATES = hex2dec('00000021') -- SC4SIM\r\n"
       b"   graph_window_types.MASS_TRANSIT = hex2dec('00000022') -- SC4SIM\r\n"
       b"   graph_window_types.TRAFFIC_CONGESTION = hex2dec('00000023') -- SC4SIM\r\n"
       b"   graph_window_types.POWER_GRID = hex2dec('0000001A') -- SC4SIM\r\n"
       b"   graph_window_types.WATER_GRID = hex2dec('0000001B') -- SC4SIM\r\n")

def main(path, out_dir):
    raw, entries = dbpf2.read(path)
    entry = next(e for e in entries if (e['t'], e['g'], e['i']) == (LUA_TYPE, LUA_GROUP, CONSTANTS))
    data = entry['data']
    if NEW not in data:
        # An earlier run of this script left its lines: put back the Maxis line first.
        start = data.find(b"   graph_window_types.WATER_POLLUTION = hex2dec('00000008') -- SC4SIM")
        if start >= 0:
            end = data.find(b"   graph_window_types.WATER = ", start)
            data = data[:start] + OLD + data[end:]
        assert data.count(OLD) == 1, 'graph_window_types.WATER_POLLUTION line not found'
        entry['data'] = data.replace(OLD, NEW)
    os.makedirs(out_dir, exist_ok=True)
    dbpf2.write(os.path.join(out_dir, os.path.basename(path)), entries, header_src=raw)

if __name__ == '__main__':
    main(*sys.argv[1:3])
