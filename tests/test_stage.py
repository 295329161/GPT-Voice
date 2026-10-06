"""Regression coverage for component changes being missed by PlatformIO."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('pio_run',Path(__file__).resolve().parents[1]/'scripts/pio_run.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class StageTests(unittest.TestCase):
 def test_component_manifest_invalidates_cmake_cache(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'project with spaces';root.mkdir();(root/'src').mkdir()
   for name in ['platformio.ini','CMakeLists.txt','partitions.csv','sdkconfig.defaults']:(root/name).write_text('baseline')
   manifest=root/'src/idf_component.yml';manifest.write_text('dependencies: {}')
   with patch.object(module.tempfile,'gettempdir',return_value=tmp):
    stage=module.stage_project(root);cache=stage/'.pio/build/szp_s3/CMakeCache.txt';cache.parent.mkdir(parents=True);cache.write_text('cache')
    module.stage_project(root);self.assertTrue(cache.exists(),'unchanged inputs should keep cache')
    manifest.write_text('dependencies:\n  new/component: "1.0"')
    module.stage_project(root);self.assertFalse(cache.exists(),'manifest changes must re-resolve components')
    self.assertEqual((stage/'src/idf_component.yml').read_text(),manifest.read_text())
if __name__=='__main__':unittest.main()
