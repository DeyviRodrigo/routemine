"""Convert a numerical benchmark into a project for independent viewer validation.

This does not certify the input. The viewer must revalidate it on opening.
"""
import argparse
import csv
import json
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('prefix',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    prefix=str(a.prefix)
    config=Path(prefix+'.request').read_text(encoding='utf-8-sig')
    values=dict(line.split('=',1) for line in config.splitlines() if '=' in line and not line.startswith('#'))
    with open(prefix+'.csv',encoding='utf-8-sig',newline='') as f:
        route=[[float(row[c]) for c in ['X','Y','Z']] for row in csv.DictReader(f)]
    with open(prefix+'.curves.csv',encoding='utf-8-sig',newline='') as f:
        curves=[{'first':int(row['first']),'last':int(row['last']),
                 'center':[float(row[c]) for c in ['cx','cy','cz']],
                 'sweep_degrees':float(row['sweep_degrees'])} for row in csv.DictReader(f)]
    obj={'software':'RouteTec','schema_version':1,'configuration':config,
         'source_dxf':json.loads(values['terrain_path']),'route':route,
         'routing':{'strategy':'continuous_ramp','curves':curves,'reference_route':[],
                    'interventions':[],'local_sections':[],
                    'effective_xy_spacing_m':float(values['xy_spacing_m']),
                    'effective_heading_step_degrees':float(values['heading_step_degrees'])}}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(obj,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'{len(route)} points and {len(curves)} curves; requires independent viewer validation')

if __name__=='__main__':main()
