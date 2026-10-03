from ftplib import error_perm
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ps5_ftp import ensure_directory


class FakeFTP:
    def __init__(self):
        self.checked = []
        self.current = '/'
        self.denied = None

    def mkd(self, path):
        self.checked.append(path)
        raise error_perm('550 File exists')

    def pwd(self):
        return self.current

    def cwd(self, path):
        if path == self.denied:
            raise error_perm('550 Permission denied')
        self.current = path


class FTPDirectories(unittest.TestCase):
    def test_shared_ancestors_are_checked_once_per_connection(self):
        ftp = FakeFTP()
        ensure_directory(ftp, '/data/title/shaders')
        ensure_directory(ftp, '/data/title/shaders')
        ensure_directory(ftp, '/data/title/overlays')
        self.assertEqual(ftp.checked, ['/data', '/data/title', '/data/title/shaders', '/data/title/overlays'])
        self.assertEqual(ftp.current, '/')
        other = FakeFTP()
        ensure_directory(other, '/data/title/shaders')
        self.assertEqual(len(other.checked), 3)

    def test_failure_is_not_cached(self):
        ftp = FakeFTP()
        ftp.denied = '/data/title'
        with self.assertRaises(error_perm):
            ensure_directory(ftp, '/data/title')
        ftp.denied = None
        ensure_directory(ftp, '/data/title')
        self.assertEqual(ftp.checked, ['/data', '/data/title', '/data/title'])
