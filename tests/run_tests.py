from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'test'
    cmd=['g++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'tests/mock'),'-I'+str(root/'ClimateCamera_2_1_0_Psychrometer'),str(root/'tests/test.cpp'),str(root/'ClimateCamera_2_1_0_Psychrometer/Ds18b20Psychrometer.cpp'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
