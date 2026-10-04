#!/usr/bin/env python3
"""Prepare source-aligned ordinary Benedictine day hours; no calendar inference.

Input: pinned Divinum Officium checkout/reference tree. Output: portable JSON.
Feast/season substitutions are explicit markers, never guessed from civil date.
"""
import argparse
import hashlib
import json
import re
import unicodedata
from pathlib import Path

REVISION = 'b6e94c5825ba656b223e78bd2c49cf973f2eea1a'
HOURS = ['Matins', 'Lauds', 'Prime', 'Terce', 'Sext', 'None', 'Vespers', 'Compline']
DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday']
COMMON = 'Common/Prayers.txt'
MINOR = 'Special/Minor Special.txt'
PRIME = 'Special/Prima Special.txt'


def sections(text):
    result = {}
    key = None
    for line in text.splitlines():
        if line.startswith('['):
            key = line.strip()
            if key in result:
                raise ValueError('Duplicate source heading: ' + key)
            result[key] = []
        elif key is not None:
            result[key].append(line)
    return result


def clean(text):
    text = re.sub(r'/:.*?:/', '', text)
    text = re.sub(r'<[^>]+>', '', text)
    text = re.sub(r'^(?:[vVrR]\.\s*|Benedictio\.\s*)', '', text.strip())
    text = re.sub(r'\+{2,}', '+', text).replace('~', '')
    return unicodedata.normalize('NFC', re.sub(r'\s+', ' ', text).strip())


class Builder:
    def __init__(self, root):
        self.root = root
        self.inputs = {}
        self.texts = {}

    def read(self, lang, path):
        path = 'web/www/horas/' + lang + '/Psalterium/' + path
        raw = (self.root / path).read_bytes()
        self.inputs[path] = hashlib.sha256(raw).hexdigest()
        return raw.decode('utf-8-sig')

    def section(self, lang, file, key):
        return sections(self.read(lang, file))[key]

    def prayer(self, key, file=COMMON, latin_key=None):
        ident = file + ':' + (latin_key or key)
        if ident in self.texts:
            return ident
        bilingual = []
        for lang in ['Latin', 'English']:
            if key == '[Amen]' and lang == 'English':
                bilingual.append(['Amen.'])
                continue
            lines = self.section(lang, file, latin_key if lang == 'Latin' and latin_key else key)
            # Only explicit, literal sections are accepted by this importer.
            if any(line.startswith(('@', '(', '&')) for line in lines if line.strip()):
                raise ValueError('Unresolved rubric/reference: ' + ident)
            lines = [clean(line) for line in lines if line.strip() and not line.startswith(('!', '_', '$'))]
            bilingual.append(lines)
        la, en = bilingual
        if len(la) != len(en):
            raise ValueError('Prayer alignment mismatch: ' + ident)
        self.texts[ident] = [dict(kind='prayer', latin=a, english=b, id=ident+':'+str(i+1), source=file)
                             for i, (a, b) in enumerate(zip(la, en))]
        return ident

    def modified(self, ident, source, transform):
        self.texts[ident] = [dict(b) for b in self.texts[source]]
        transform(self.texts[ident])
        for i, b in enumerate(self.texts[ident]):
            b['id'] = ident+':'+str(i+1)
        return ident

    def note(self, text, marker=True):
        ident = ('marker:' if marker else 'note:') + text
        self.texts[ident] = [dict(kind='marker' if marker else 'note', latin='', english=text, id=ident)]
        return ident

    def heading(self, text):
        ident = 'heading:' + text
        self.texts[ident] = [dict(kind='heading', latin='', english=text, id=ident)]
        return ident

    def psalm(self, token, glory=True):
        ident = 'psalm:' + token + (':gloria' if glory else ':no-gloria')
        if ident in self.texts:
            return ident
        match = re.fullmatch(r'(\d+)(?:\((\d+)-(\d+)\))?', token)
        if not match:
            raise ValueError('Unsupported psalm range: '+token)
        number, start, stop = match.groups()
        bilingual = []
        for lang in ['Latin', 'English']:
            verses = {}
            for line in self.read(lang, 'Psalmorum/Psalm'+number+'.txt').splitlines():
                m = re.match(r'(\d+:(\d+)[a-z]?)\s+(.*)', line)
                if m and (not start or int(start) <= int(m[2]) <= int(stop)):
                    # Some source verses have two chant lines with the same ID.
                    # Retain both in source order rather than overwriting one.
                    verse_key = '122:2a' if lang == 'English' and m[1] == '122:2y' else m[1]
                    occurrence = 2
                    while verse_key in verses:
                        verse_key = m[1]+'#'+str(occurrence)
                        occurrence += 1
                    verses[verse_key] = clean(m[3])
            bilingual.append(verses)
        la, en = bilingual
        if not la or la.keys() != en.keys():
            raise ValueError('Psalm verse IDs do not match: '+token+' '+str(la.keys()-en.keys())+' '+str(en.keys()-la.keys()))
        title = ('Benedictus' if number == '231' else 'Magnificat' if number == '232'
                 else 'Canticle '+number if int(number) >= 200 else 'Psalm '+token)
        blocks = [dict(kind='heading', latin='', english=title, id=ident+':heading')]
        blocks += [dict(kind='prayer', latin=la[v], english=en[v], id=ident+':'+v,
                        verse=v, source='Psalmorum/Psalm'+number+'.txt') for v in la]
        if glory:
            blocks += self.texts[self.prayer('[Gloria]')]
        self.texts[ident] = blocks
        return ident

    def hymn(self, hour):
        file = PRIME if hour == 'Prima' else MINOR
        source = self.prayer('[Hymnus '+hour+']', file)
        def transform(blocks):
            for b in blocks:
                if hour == 'Prima':
                    b['latin'] = b['latin'].replace('Nunc et per omne sǽculum', 'Et nunc et in perpétuum')
                if hour == 'Sexta':
                    b['latin'] = b['latin'].replace('illúminas', 'ínstruis')
                if hour == 'Nona':
                    b['latin'] = b['latin'].replace('lumen', 'clarum')
                if hour == 'Completorium':
                    b['latin'] = b['latin'].replace('pro tua', 'sólita').replace('et custódia', 'ad custódiam')
            if hour == 'Completorium':
                for b, latin in zip(blocks[-5:], ['* Præsta, Pater omnípotens,', 'Per Jesum Christum Dóminum,', 'Qui tecum in perpétuum', 'Regnat cum Sancto Spíritu.', 'Amen.']):
                    b['latin'] = latin
            # Hymn translations are poetic: align complete stanzas, not individual lines.
            grouped = []
            for i in range(0, len(blocks)-1, 4):
                group = blocks[i:min(i+4, len(blocks)-1)]
                grouped.append(dict(kind='prayer', latin='\n'.join(b['latin'] for b in group),
                                    english='\n'.join(b['english'] for b in group), source=file))
            grouped.append(blocks[-1])
            blocks[:] = grouped
        return self.modified('hymn:monastic:'+hour, source, transform)

    def weekly_psalms(self, hour, day):
        if hour == 'Completorium':
            return [['4'], ['90'], ['133']]
        if hour in ['Prima', 'Tertia', 'Sexta', 'Nona']:
            lines = self.section('Latin', 'Psalmi/Psalmi minor.txt', '[Monastic_]')
            if day == 0:
                suffix = 'Dominica'
            elif day == 1:
                suffix = 'Feria II'
            elif hour == 'Prima':
                suffix = 'Sabbato' if day == 6 else 'Feria '+['', '', 'III', 'IV', 'V', 'VI'][day]
            else:
                suffix = 'Feria'
            prefix = hour+' '+suffix+'='
            line = next(line for line in lines if line.startswith(prefix))
            return [[p.strip()] for p in line.split(';;')[1].split(',')]
        key = '[Monastic '+hour+']'+(' (feria '+str(day+1)+')' if day else '')
        lines = self.section('Latin', 'Psalmi/Psalmi major.txt', key)
        if day == 0 and hour == 'Laudes':
            return [['50'], ['117'], ['62'], ['210'], ['148', '149', '150']]
        return [line.split(';;')[1].split(';') for line in lines if ';;' in line]

    def office(self, hour, day):
        p = self.prayer
        h = self.heading
        marker = self.note
        out = [marker('Regular weekly office. Feast and seasonal substitutions are not applied.', False)]
        if hour == 'Matins':
            return out+[h('Opening'), p('[Domine labia]'), marker('Matins psalmody, invitatory and readings: not yet loaded')]
        if hour == 'Completorium':
            out += [h('Blessing'), p('[Benedictio Completorium_]'), p('[Amen]'),
                    h('Short reading'), p('[Lectio Completorium]', MINOR), p('[Tu autem_]'),
                    h('Preparation'), p('[Adjutorium nostrum]'), p('[Pater noster]')]
            confession = p('[Confiteor_]')
            def monastic_confession(blocks):
                for b in blocks:
                    b['latin'] = b['latin'].replace('Paulo', 'Paulo, beáto Patri nostro Benedícto').replace('Paulum', 'Paulum, beátum Patrem nostrum Benedíctum')
                    b['english'] = b['english'].replace('Paul', 'Paul, our holy Father Benedict')
            out += [self.modified('confiteor:monastic-private', confession, monastic_confession),
                    p('[Misereatur]'), p('[Indulgentiam]'), p('[Converte nos]')]
        out += [h('Opening'), p('[Deus in adjutorium]'), marker('Alleluia, or Laus tibi according to the season')]
        if hour not in ['Laudes', 'Vespera', 'Completorium']:
            out += [h('Hymn'), self.hymn(hour), marker('Seasonal hymn doxology, if prescribed')]
        out += [h('Psalmody')]
        if hour == 'Laudes':
            out += [self.psalm('66', False)]
        groups = self.weekly_psalms(hour, day)
        for group in groups:
            if hour != 'Completorium':
                out.append(marker('Antiphon from the psalter, season or feast'))
            for n, token in enumerate(group):
                # Laudate 148-150: one unit, no Gloria; joined psalms one Gloria at end.
                glory = not (hour == 'Laudes' and group == ['148', '149', '150']) and n == len(group)-1
                out.append(self.psalm(token, glory))
            if hour != 'Completorium':
                out.append(marker('Repeat the antiphon'))
        if hour == 'Completorium':
            out += [h('Hymn'), self.hymn(hour), marker('Seasonal hymn doxology, if prescribed'),
                    h('Chapter and verse'), p('[Completorium_]', MINOR), p('[Deo gratias]'), p('[Versum 4]', MINOR)]
        elif hour == 'Prima':
            out += [h('Chapter'), p('[Dominica]', PRIME), marker('Prime verse / other rubrics as prescribed')]
        else:
            out += [marker('Chapter and verse from the psalter, season or feast')]
        if hour in ['Laudes', 'Vespera']:
            out += [marker('Short responsory and hymn'), marker('Canticle antiphon'),
                    self.psalm('231' if hour == 'Laudes' else '232'), marker('Repeat the canticle antiphon')]
        out += [h('Prayer'), p('[Kyrie]'), p('[Pater noster Et]'),
                marker('Private recitation: Domine exaudi; a priest uses Dominus vobiscum', False)]
        dominus = p('[Dominus]')
        exaudi = self.modified('domine-exaudi:private', dominus, lambda blocks: blocks.__setitem__(slice(None), blocks[2:4]))
        out += [exaudi, p('[Oremus]')]
        if hour == 'Completorium':
            out += [p('[Oratio Visita_]'), p('[Per Dominum]')]
        elif hour == 'Prima':
            out += [p('[oratio_Domine]'), p('[Per Dominum]')]
        else:
            out += [marker('Collect of the preceding Sunday, or of the season / feast, with its conclusion')]
        out += [h('Conclusion'), exaudi, p('[Benedicamus Domino]')]
        if hour == 'Completorium':
            out += [p('[Benedictio Completorium2]'), p('[Amen]'), marker('Seasonal Marian antiphon, verse and prayer'),
                    h('Silent concluding prayers'), p('[Pater noster]'), p('[Ave Maria]'), p('[Credo]'),
                    marker('Clear Creek custom: Angelus follows', False)]
        else:
            out += [p('[Fidelium animae]')]
            if hour == 'Prima':
                out += [marker('Chapter office / Martyrology follows where observed')]
        return out

    def build(self):
        source_names = ['Matins', 'Laudes', 'Prima', 'Tertia', 'Sexta', 'Nona', 'Vespera', 'Completorium']
        week = {str(day): {name: self.office(source, day) for name, source in zip(HOURS, source_names)} for day in range(7)}
        return dict(schema=1, profile='Monastic 1963 regular weekly office', revision=REVISION,
                    psalm_numbering='Vulgate', english_edition='Divinum Officium traditional English',
                    scope='Day-hours weekly psalmody and ordinary; seasonal and feast substitutions marked; Matins incomplete',
                    sources=self.inputs, texts=self.texts, week=week)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('.references/divinum-officium'))
    parser.add_argument('--output', type=Path, default=Path('tools/office/benedictine.json'))
    args = parser.parse_args()
    data = Builder(args.source).build()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n')
    print('Built', len(data['texts']), 'text units from', len(data['sources']), 'pinned source files')


if __name__ == '__main__':
    main()
