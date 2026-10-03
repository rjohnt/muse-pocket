# SPDX-License-Identifier: Apache-2.0
"""Offline dated prayer data, shared with the ESP32 content generator."""
from datetime import date, datetime
import json
from pathlib import Path
from zoneinfo import ZoneInfo

HOURS = ('Matins','Lauds','Prime','Terce','Sext','None','Vespers','Compline')
DEFAULT_PACK = Path(__file__).resolve().parents[1] / 'office/office-pack.json'

class OfficePack:
    def __init__(self, path=DEFAULT_PACK):
        self.data = json.loads(Path(path).read_text())
        if self.data.get('schema') != 2: raise ValueError('Unsupported office pack')
        for day,rites in self.data['days'].items():
            date.fromisoformat(day)
            for offices in rites.values():
                for office in offices.values():
                    if any(ident not in self.data['texts'] for ident in office['blocks']):
                        raise ValueError('Missing prayer content')
    def route(self, query):
        day=query.get('date',[datetime.now(ZoneInfo('America/Chicago')).date().isoformat()])[0]
        date.fromisoformat(day)
        rite=query.get('rite',['monastic'])[0];hour=query.get('hour',['Lauds'])[0]
        if rite not in ('monastic','modern') or hour not in HOURS: raise ValueError('Unsupported office')
        office=self.data['days'].get(day,{}).get(rite,{}).get(hour)
        if office is None:
            result=dict(available=False,date=day,rite=rite,hour=hour,
                        message='This date/tradition is not in the offline prayer pack. Import its source texts before praying the complete office.',
                        first_date=min(self.data['days']),last_date=max(self.data['days']))
        else:
            result=dict(available=True,date=day,rite=rite,hour=hour,title=office['title'],
                        source=self.data['source'],blocks=[self.data['texts'][i] for i in office['blocks']])
        return json.dumps(result,ensure_ascii=False).encode(),'application/json'
