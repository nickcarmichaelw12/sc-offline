"""Collect notices for Python and build-time packaging tools in the release."""
from importlib import metadata
from pathlib import Path
import shutil
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
python_license = Path(sys.base_prefix) / 'LICENSE.txt'
if not python_license.exists():
    raise RuntimeError('Python license was not found; do not publish incomplete notices.')
shutil.copyfile(python_license, out / 'Python-LICENSE.txt')
for name in ('pyinstaller', 'pyinstaller-hooks-contrib'):
    dist = metadata.distribution(name)
    count = 0
    for file in dist.files or []:
        if any(word in Path(str(file)).name.lower() for word in ('license', 'copying')):
            source = Path(dist.locate_file(file))
            if source.is_file():
                count += 1
                shutil.copyfile(source, out / f'{name}-{count}-{source.name}')
    if not count:
        raise RuntimeError(f'No license notices found for {name}')
(out / 'BUILD.txt').write_text('Python '+sys.version+'\nPyInstaller '+metadata.version('pyinstaller')+'\nHooks '+metadata.version('pyinstaller-hooks-contrib')+'\nSQLite is in the public domain.\n', encoding='utf-8')
