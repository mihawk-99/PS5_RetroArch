"""tools/content-version.py: each release's contentVersion, so stores offer it (issue #77)."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest

spec = spec_from_file_location('content_version',
                               Path(__file__).resolve().parent.parent / 'tools/content-version.py')
cv = module_from_spec(spec)
spec.loader.exec_module(cv)


class ContentVersion(unittest.TestCase):
    def test_tag_to_content_version(self):
        for tag, want in [('v4.3.0', '04.003.000'), ('v4.2.1-beta-4', '04.002.001'),
                          ('v10.0.12', '10.000.012'), ('4.3.0', '04.003.000')]:
            self.assertEqual(cv.expected(tag), want, tag)
        for tag in ('v4.3', 'latest', 'v100.0.0', 'v4.1000.0'):
            with self.assertRaises(ValueError, msg=tag):
                cv.expected(tag)

    def test_a_release_ships_its_own_and_a_higher_one(self):
        earlier = {'v4.1.0-beta-4': '01.000.000', 'v4.2.0-beta-4': '01.000.000'}
        self.assertEqual(cv.check('v4.3.0', '04.003.000', earlier), [])
        # Still the template's 01.000.000: the store sees no update.
        self.assertIn('ships 04.003.000', cv.check('v4.3.0', '01.000.000', earlier)[0])
        # The same version as an earlier release, or a lower one, is never offered.
        for shipped in ('04.003.000', '04.004.000'):
            problems = cv.check('v4.3.0-beta-5', '04.003.000', {'v4.3.0-beta-4': shipped})
            self.assertEqual(len(problems), 1)
            self.assertIn('not higher than v4.3.0-beta-4', problems[0])

    def test_this_repository_s_releases(self):
        # Up to v4.2.0-beta-4 every tag shipped 01.000.000 (or no param.json);
        # v4.3.0-beta-4 was the first to raise it. A later release passes, and one that
        # would reuse v4.3.0-beta-4's value does not.
        earlier = cv.tagged_versions('v99.0.0')
        self.assertEqual(earlier['v4.2.0-beta-4'], '01.000.000')
        self.assertEqual(earlier['v4.3.0-beta-4'], '04.003.000')
        self.assertEqual(cv.check('v99.0.0', '99.000.000', earlier), [])
        self.assertIn('not higher than v4.3.0-beta-4', cv.check('v4.3.0', '04.003.000', earlier)[0])


if __name__ == '__main__':
    unittest.main()
