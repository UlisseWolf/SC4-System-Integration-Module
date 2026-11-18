# Collects every graph exemplar of the Graphs window into one file.
#   python make_graph_exemplars.py <SimCity_1.dat> <CAM_Exemplars.dat> <out_dir>
# Writes <out_dir>/CAM_Exemplar_Graphs.dat: the 18 graph exemplars of the game (type 0x6534284A, group 0xCA4AD545,
# instances 0x01-0x13 and 0x18), with CAM's own version wherever CAM_Exemplars.dat has one (Power 0x06, Water 0x07,
# Garbage 0x0A, Demands 0x0E). Also writes <out_dir>/CAM_Exemplars.dat without the graph exemplars, so each graph
# lives in one file only. Run make_utility_graphs.py on CAM_Exemplar_Graphs.dat afterwards.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf2

EXEMPLAR, GRAPH_GROUP = 0x6534284A, 0xCA4AD545

def is_graph(e):
    return e['t'] == EXEMPLAR and e['g'] == GRAPH_GROUP

def main(game, cam_exemplars, out_dir):
    _, game_entries = dbpf2.read(game)
    graphs = {e['i']: e for e in game_entries if is_graph(e)}
    assert len(graphs) == 18, len(graphs)
    raw, cam_entries = dbpf2.read(cam_exemplars)
    for e in cam_entries:
        if is_graph(e):
            graphs[e['i']] = e
    os.makedirs(out_dir, exist_ok=True)
    dbpf2.write(os.path.join(out_dir, 'CAM_Exemplar_Graphs.dat'), [graphs[i] for i in sorted(graphs)], header_src=raw)
    dbpf2.write(os.path.join(out_dir, 'CAM_Exemplars.dat'), [e for e in cam_entries if not is_graph(e)], header_src=raw)
    print('%d graph exemplars, CAM versions: %s' % (len(graphs), ', '.join('0x%02X' % e['i'] for e in cam_entries if is_graph(e))))

if __name__ == '__main__':
    main(*sys.argv[1:4])
