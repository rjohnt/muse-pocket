#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Resolve dated bilingual offices using the pinned Divinum Officium engine.

The ESP32 and Mac consume the resulting pack offline; neither asks Muse to
choose the calendar, translate prayers, or generate liturgical content.
"""
import argparse
from datetime import date, timedelta
from html.parser import HTMLParser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

REVISION = 'b6e94c5825ba656b223e78bd2c49cf973f2eea1a'
HOURS = dict(Matins='Matutinum', Lauds='Laudes', Prime='Prima', Terce='Tertia',
             Sext='Sexta', None_='Nona', Vespers='Vespera', Compline='Completorium')
HOURS['None'] = HOURS.pop('None_')
ORDER = ['Matins', 'Lauds', 'Prime', 'Terce', 'Sext', 'None', 'Vespers', 'Compline']

class Node:
    def __init__(self, tag='', attrs=()):
        self.tag, self.attrs, self.children = tag, dict(attrs), []
    def text(self):
        if self.tag == 'br': return '\n'
        if self.tag in ('script', 'style') or (self.tag == 'div' and self.attrs.get('align', '').lower() == 'right'): return ''
        return ''.join(c if isinstance(c, str) else c.text() for c in self.children)
    def find(self, tag):
        for child in self.children:
            if isinstance(child, Node):
                if child.tag == tag: yield child
                yield from child.find(tag)

class Tree(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root=Node(); self.stack=[self.root]
    def handle_starttag(self, tag, attrs):
        node=Node(tag, attrs); self.stack[-1].children.append(node)
        if tag not in ('br','input','hr','img','meta','link'): self.stack.append(node)
    def handle_endtag(self, tag):
        for i in range(len(self.stack)-1,0,-1):
            if self.stack[i].tag==tag: self.stack=self.stack[:i]; break
    def handle_data(self, value): self.stack[-1].children.append(value)


def lines(cell):
    # Ignore provenance comments in braces, but retain rubrics and every verse.
    return [re.sub(r'\s+', ' ', re.sub(r'\{[^}]*\}', '', line)).strip()
            for line in cell.text().split('\n') if line.strip() and not re.fullmatch(r'\s*\{[^}]*\}\s*',line)]


def cell_heading(cell):
    for f in cell.find('font'):
        if f.attrs.get('size')=='+1' and f.attrs.get('color','').lower()=='red':
            value=f.text().strip()
            if len(value)>2 and list(f.find('b')): return value
    return ''


def parse_office(raw, hour):
    tree=Tree(); tree.feed(raw)
    tables=[t for t in tree.root.find('table') if 'contrastbg' in t.attrs.get('class','')]
    if len(tables)!=1: raise ValueError('Expected one bilingual office table')
    title=''
    for p in tree.root.find('p'):
        for f in p.find('font'):
            if f.attrs.get('color','').lower()=='blue': title=f.text().strip(); break
        if title:break
    if not title:
        for p in tree.root.find('p'):
            if p.attrs.get('align','').lower()=='center' and '~' in p.text():
                title=p.text().strip().split('\n')[0];break
    if not title or 'Monastic - 1963' not in raw: raise ValueError('Missing calendar or wrong rite')
    blocks=[]
    for tr in tables[0].find('tr'):
        cells=[c for c in tr.children if isinstance(c,Node) and c.tag=='td']
        if len(cells)!=2: raise ValueError('Missing bilingual cell')
        la,en=lines(cells[0]),lines(cells[1]); lh,eh=cell_heading(cells[0]),cell_heading(cells[1])
        if lh:
            blocks.append(dict(kind='heading',latin=lh,english=eh or lh,alignment='section'))
            if la and la[0]==lh:la.pop(0)
            if en and en[0]==eh:en.pop(0)
        # Psalm identifiers are the alignment anchors, including repeated IDs.
        verse=lambda s: re.match(r'^(\d+:\d+[a-z]?)\s',s)
        lav=[verse(x).group(1) for x in la if verse(x)]
        env=[verse(x).group(1) for x in en if verse(x)]
        aligned=len(la)==len(en) and (not lav or lav==env)
        # Poetic translations and long lessons are kept as semantic sections,
        # never paired by visual wrapping or guessed word order.
        if (lh.lower().startswith(('hymn','lectio','capitulum')) or not aligned) and (la or en):
            blocks.append(dict(kind='prayer',latin='\n'.join(la),english='\n'.join(en),alignment='section'))
        else:
            for a,b in zip(la,en):
                m=verse(a)
                blocks.append(dict(kind='prayer',latin=a,english=b,alignment='verse' if m else 'source-line'))
    # Divinum Officium repeats the Latin in the English column when it has no
    # translation for a text (the antiphons of some commons, for example).
    # Carrying that through as a translation prints the Latin twice, so a
    # repeated column means "no translation" and the Latin is shown once.
    for b in blocks:
        if b['english'].strip() and b['english'].strip()==b['latin'].strip(): b['english']=''
    if not blocks or not any(b['kind']=='prayer' for b in blocks): raise ValueError('Empty resolved office')
    if any(re.search(r'(^|\n)[@&][A-Za-z]',b['latin']) for b in blocks): raise ValueError('Unresolved source directive')
    return dict(title=title,hour=hour,blocks=blocks)


def resolve(root,day,hour):
    from urllib.parse import urlencode
    query=urlencode(dict(command='pray'+HOURS[hour],date1=f'{day.month}-{day.day}-{day.year}',
                         version='Monastic - 1963',lang1='Latin',lang2='English',votive='Hodie',priest='0',expand='all'))
    env=dict(os.environ,REQUEST_METHOD='GET',QUERY_STRING=query)
    script=root/'web/cgi-bin/horas'
    result=subprocess.run(['perl','-I'+str(root/'web/cgi-bin'),'officium.pl'],cwd=script,env=env,
                          capture_output=True,timeout=60)
    if result.returncode or result.stderr: raise ValueError(f'Divinum engine failed for {day} {hour}')
    return parse_office(result.stdout.decode('utf-8'),hour)


def build(root,start,count):
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    if actual!=REVISION: raise ValueError('Use the pinned Divinum Officium revision')
    pack=dict(schema=2,source=dict(name='Divinum Officium',revision=REVISION,rite='Monastic - 1963',
             calendar='General monastic calendar; Clear Creek local propers not applied',
             license='MIT',english='Divinum Officium traditional English'),texts={},days={})
    for n in range(count):
        day=start+timedelta(days=n); offices={}
        for hour in ORDER:
            office=resolve(root,day,hour); ids=[]
            for block in office.pop('blocks'):
                canonical=json.dumps(block,ensure_ascii=False,sort_keys=True)
                ident=hashlib.sha256(canonical.encode()).hexdigest()[:20]
                if ident in pack['texts'] and pack['texts'][ident]!=block: raise ValueError('Content ID collision')
                pack['texts'][ident]=block; ids.append(ident)
            offices[hour]=dict(title=office['title'],blocks=ids)
        pack['days'][day.isoformat()]={'monastic':offices}
        print('Resolved',day,flush=True)
    return pack


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('.references/divinum-engine'))
    p.add_argument('--start',type=date.fromisoformat,default=date.today())
    p.add_argument('--days',type=int,default=31)
    p.add_argument('--output',type=Path,default=Path('tools/office/office-pack.json'))
    a=p.parse_args()
    if not 1<=a.days<=366:p.error('days must be 1–366')
    pack=build(a.source.resolve(),a.start,a.days)
    a.output.write_text(json.dumps(pack,ensure_ascii=False,separators=(',',':'))+'\n')
    print('Pack:',len(pack['texts']),'distinct bilingual units;',a.output.stat().st_size,'bytes')
if __name__=='__main__':main()
