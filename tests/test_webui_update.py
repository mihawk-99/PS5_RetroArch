"""The real updater, exercised with small packages rather than console user data."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent

def sha(data):
    return hashlib.sha256(data).hexdigest()

class Update(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.work.name) / 'update'
        archive = subprocess.check_output(['bash', 'tools/build-webui-update.sh', 'host'], cwd=ROOT, text=True).strip()
        # No frame over 16 KiB: the updater runs on a thread of the WebUI daemon, a payload,
        # whose stack is small. A 64 KiB buffer there killed the daemon as it began to
        # verify a download, and the update started over at every try (issue 75).
        subprocess.run(['c++', '-std=c++17', '-pthread', '-Wframe-larger-than=16384', '-Werror',
                        '-I'+str(ROOT/'.deps/native/zlib/zlib-1.3.2/contrib/minizip'),
                        '-I'+str(ROOT/'vendor/retroarch/deps/mbedtls'), 'tests/webui_update_main.cpp',
                        'src/webui_update.cpp', archive, '-lz', '-o', str(cls.binary)], cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); (self.root/'.update').mkdir()
        self.files = {p: b'new contents' for p in ['eboot.bin', 'libvulkan.so.1', 'sce_module/libc.prx', 'sce_sys/param.json',
            'webui/version.json', 'licenses/components.json', 'video-assets.json', 'webui/app.js']}

    def package(self, extra=None, bad_manifest=False):
        data = dict(self.files)
        data['manifest.sha256'] = ''.join(f'{sha(v)} *{p}\n' for p,v in data.items()).encode()
        if bad_manifest: data['webui/app.js'] = b'tampered'
        self.zip = self.root/'release.zip'
        with zipfile.ZipFile(self.zip, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for p,v in data.items(): z.writestr('PPSA99169/'+p,v)
            if extra:
                for p,v in extra: z.writestr(p,v)
        self.checksum = sha(self.zip.read_bytes())

    def run_update(self, action='install', success=True):
        result = subprocess.run([str(self.binary),str(self.root),action,str(self.zip),self.checksum],capture_output=True,text=True)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout+result.stderr)
        return result.stdout

    def test_install_preserves_user_data_and_custom_assets(self):
        user = {'config/retroarch.cfg': b'owner', 'retroarch.cfg': b'owner root', 'content/game.bin':b'content',
                'saves/save.srm':b'save', 'system/Saturn/sega_101.bin':b'bios', 'overlays/custom.cfg':b'custom'}
        for p,v in user.items():
            dest=self.root/p;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(v)
            self.files[p]=b'packaged default'
        self.package(); self.run_update()
        for p,v in user.items(): self.assertEqual((self.root/p).read_bytes(),v)
        self.assertEqual((self.root/'webui/app.js').read_bytes(),b'new contents')
        self.assertEqual((self.root/'eboot.bin').stat().st_mode & 0o777, 0o755)
        self.assertFalse((self.root/'.update').exists())

    def test_every_program_of_a_release_is_installed(self):
        # EmulationStation, the picker, the forwarder and the Lapy service came after the
        # updater's list of what it installs: an update left them at the old version.
        shipped = ['es-de/es-de.bin', 'es-de/resources/a.svg', 'es-de/themes/t/theme.xml', 'picker/picker.bin',
                   'forwarder/launch.elf', 'lapy-root-daemon.elf']
        own = {'es-de/ES-DE/settings/es_settings.xml': b'mine', 'es-de/ES-DE/gamelists/nes/gamelist.xml': b'mine'}
        for p in shipped:
            dest = self.root/p; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(b'old'); self.files[p] = b'new contents'
        for p, v in own.items():
            dest = self.root/p; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(v)
        self.package(); self.run_update()
        for p in shipped: self.assertEqual((self.root/p).read_bytes(), b'new contents', p)
        for p, v in own.items(): self.assertEqual((self.root/p).read_bytes(), v, p)

    def test_a_release_s_empty_folders_are_taken(self):
        # A release carries content/<system>/ for every system: 49 folder entries, which are
        # in no manifest. Every one was refused as an invalid path (issue 75).
        (self.root/'content').mkdir(); (self.root/'content/game.bin').write_bytes(b'content')
        self.package(extra=[('PPSA99169/content/snes/', b''), ('PPSA99169/content/naomi2/', b''), ('PPSA99169/assets/empty/', b'')])
        self.run_update()
        self.assertTrue((self.root/'content/snes').is_dir() and (self.root/'content/naomi2').is_dir())
        self.assertEqual((self.root/'content/game.bin').read_bytes(), b'content')
        self.assertFalse((self.root/'assets/empty').exists())
        self.assertEqual((self.root/'webui/app.js').read_bytes(), b'new contents')
        # A folder entry with bytes in it, or one that climbs out, is still refused.
        for bad in [('PPSA99169/content/snes/', b'data'), ('PPSA99169/content/../outside/', b'')]:
            self.package(extra=[bad]); self.run_update('verify', False)
        self.assertFalse((self.root.parent/'outside').exists())

    def test_static_driver_release_and_hidden_asset_files(self):
        del self.files['libvulkan.so.1']  # RADV is statically linked in production.
        self.files['shaders/plugins/enabled/.keep'] = b''
        self.files['system/PPSSPP/debugger/.nojekyll'] = b''
        self.package();self.run_update()
        self.assertTrue((self.root/'shaders/plugins/enabled/.keep').exists())

    def test_unchanged_files_are_verified_without_staging_copies(self):
        (self.root/'webui').mkdir()
        (self.root/'webui/app.js').write_bytes(self.files['webui/app.js'])
        self.package();self.run_update('prepare')
        self.assertFalse((self.root/'.update/stage/webui/app.js').exists())
        self.assertNotIn('webui/app.js', (self.root/'.update/plan').read_text())
        self.package(bad_manifest=True);self.run_update('prepare', success=False)

    def test_checksum_and_manifest_reject_corruption(self):
        self.package();self.checksum='0'*64;self.run_update(success=False)
        self.package(bad_manifest=True);self.run_update(success=False)
        self.assertFalse((self.root/'eboot.bin').exists())

    def test_traversal_duplicate_and_symlink_rejected(self):
        link=zipfile.ZipInfo('PPSA99169/webui/link');link.create_system=3;link.external_attr=0o120777<<16
        for extra in [[('PPSA99169/../escape',b'bad')],[('other/file',b'bad')],
                      [('PPSA99169/webui/app.js',b'duplicate')],[(link,b'/tmp/escape')]]:
            with self.subTest(extra=str(extra)):
                self.package(extra);self.run_update(success=False)
        self.assertFalse((self.root/'eboot.bin').exists())

    def test_failure_restores_changed_files_and_removes_new_files(self):
        for p in ['eboot.bin','libvulkan.so.1','webui/app.js']:
            target=self.root/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'old')
        self.package();self.run_update('fail',success=False)
        for p in ['eboot.bin','libvulkan.so.1','webui/app.js']: self.assertEqual((self.root/p).read_bytes(),b'old')
        self.assertFalse((self.root/'video-assets.json').exists())
        self.assertFalse((self.root/'.update/journal').exists())

    def test_interrupted_transaction_restored_on_startup(self):
        self.package();self.run_update('prepare')
        (self.root/'.update/journal').write_text('webui/app.js\nvideo-assets.json\n')
        (self.root/'.update/backup/webui').mkdir(parents=True)
        (self.root/'.update/backup/webui/app.js').write_bytes(b'previous')
        (self.root/'webui').mkdir()
        (self.root/'.update/stage/webui/app.js').rename(self.root/'webui/app.js')
        (self.root/'.update/stage/video-assets.json').rename(self.root/'video-assets.json')
        self.run_update('recover')
        self.assertEqual((self.root/'webui/app.js').read_bytes(),b'previous')
        self.assertFalse((self.root/'video-assets.json').exists())

    def test_symlink_destination_rejected_without_touching_target(self):
        target=self.root/'owner';target.write_bytes(b'owner')
        (self.root/'libvulkan.so.1').symlink_to(target)
        self.package();self.run_update(success=False)
        self.assertEqual(target.read_bytes(),b'owner')
