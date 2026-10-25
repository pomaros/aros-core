#!/usr/bin/env python3
"""Exercise production notify setup/disposal under ASan and UBSan."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
source = (here.parents[3] / "workbench/libs/muimaster/classes/application.c").read_text()
start = source.index("static void Application_SetupConfigNotify")
setup = source[start:source.index("/**************************************************************************\n OM_NEW", start)]
start = source.index("    if (data->app_CfgNotifyPort)\n    {\n        if (data->app_CfgNotifyStarted)")
cleanup = source[start:source.index("    if (data->app_GlobalInfo.mgi_Configdata)", start)]
with tempfile.TemporaryDirectory(prefix="zune-notify-") as directory:
    directory = Path(directory)
    (directory / "production_notify.inc").write_text(
        setup + "\nstatic void cleanup(Object *data){\n" + cleanup + "}\n")
    executable = directory / "fixture"
    subprocess.run(["clang", "-g", "-fsanitize=address,undefined", "-I" + str(directory),
                    str(here / "zune_notify_fixture.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
