"""The ES-DE themes shipped beside Alekfull NX (frontends/es-de/themes.list): each pinned
by commit, credited in the notices, and fetched at that commit when it is present."""
from pathlib import Path
import json
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent
REQUESTED = {'colorful-simplified-es-de', 'colorful-revisited-es-de', 'playstation-x-es-de',
             'art-book-next-es-de', 'retrofix-revisited-es-de', 'artflix-revisited-es-de'}


def pins():
    for line in (ROOT / 'frontends/es-de/themes.list').read_text().splitlines():
        found = re.fullmatch(r'(theme_\w+)=([0-9a-f]{40}) (https://github\.com/[\w.-]+/([\w.-]+))', line)
        if found:
            yield found.groups()
        else:
            assert not line.startswith('theme_'), f'malformed pin: {line}'


class EsdeThemes(unittest.TestCase):
    def test_pinned_credited_and_fetched(self):
        listed = list(pins())
        self.assertEqual({name for *_, name in listed}, REQUESTED)
        components = {c['id']: c for c in json.loads((ROOT / 'tooling/notices/components.json').read_text())['components']}
        for var, commit, url, name in listed:
            notice = components[name]
            self.assertEqual(notice['source'], {'kind': 'pin', 'file': 'frontends/es-de/themes.list', 'var': var,
                                                'url': url + '/tree/{rev}'})
            self.assertEqual(notice['artifacts'], [f'es-de/themes/{name}/**'])
            self.assertTrue(notice['licence'].startswith('CC-BY-NC-SA') and notice['noncommercial'])
            clone = ROOT / '.deps/esde-themes' / name
            if (clone / '.git').exists():
                head = subprocess.check_output(['git', '-C', str(clone), 'rev-parse', 'HEAD'], text=True).strip()
                self.assertEqual(head, commit, name)
                self.assertTrue((clone / 'theme.xml').is_file(), name)

    def test_stage_copies_every_listed_theme(self):
        stage = (ROOT / 'frontends/es-de/stage.sh').read_text()
        self.assertIn('done < "$root/frontends/es-de/themes.list"', stage)
        self.assertIn('frontends/es-de/themes.list', (ROOT / 'tools/build-esde.sh').read_text())


if __name__ == '__main__':
    unittest.main()
