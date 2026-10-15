#!/usr/bin/env python3
import sys
import os
import logging
from PyQt6.QtCore import QCoreApplication, QEventLoop

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_SRC_DIR = os.path.dirname(_SCRIPT_DIR)  # bin/src/
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

from core.tasks.process_zip_task import ProcessZipTask
from core.tasks.download_depots_task import DownloadDepotsTask
from core.tasks.steam_cdp_injector import SteamCDPInjector
from managers.cli_manager import CLITaskManager
from scripts.manifest_packager import ManifestPackager
from utils.settings import get_settings
from utils.task_runner import TaskRunner
from core.steam_helpers import get_steam_libraries

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def main():
    if len(sys.argv) != 2:
        sys.exit(1)
        
    appid = sys.argv[1].strip()
    
    app = QCoreApplication(sys.argv)
    
    packager = ManifestPackager()
    zip_path, error = packager.package(appid)
    if error or not zip_path:
        logger.error(f"Failed to package manifest: {error}")
        sys.exit(1)
        
    zip_task = ProcessZipTask()
    zip_runner = TaskRunner()
    
    game_data_holder = [None]
    def on_zip(result):
        game_data_holder[0] = result
        loop.quit()
        
    loop = QEventLoop()
    zip_runner.run(zip_task.run, zip_path).finished.connect(on_zip)
    loop.exec()
    
    game_data = game_data_holder[0]
    if not game_data or not game_data.get("depots"):
        logger.error("No game data or depots found")
        sys.exit(1)
        
    selected_depots = list(game_data["depots"].keys())
    game_data["selected_depots_list"] = selected_depots
    
    dest_path = None
    libraries = get_steam_libraries()
    if libraries:
        dest_path = libraries[0]
    else:
        dest_path = os.path.expanduser("~")
        
    cdp = SteamCDPInjector(appid, game_data.get("game_name", f"App {appid}"))
    cdp.start()
    
    download_task = DownloadDepotsTask()
    download_runner = TaskRunner()
    
    is_paused = [False]
    def on_progress(pct):
        cdp.update(pct)
        if getattr(cdp, 'cancelled', False):
            logger.info("Download cancelled from UI")
            download_task.stop()
        elif getattr(cdp, 'paused', False) and not is_paused[0]:
            logger.info("Download paused from UI")
            try:
                download_task.toggle_pause(True)
                is_paused[0] = True
            except Exception as e:
                logger.error(f"Pause failed: {e}")
        elif not getattr(cdp, 'paused', False) and is_paused[0]:
            logger.info("Download resumed from UI")
            try:
                download_task.toggle_pause(False)
                is_paused[0] = False
            except Exception as e:
                logger.error(f"Resume failed: {e}")
        
    download_task.progress_percentage.connect(on_progress)
    
    d_loop = QEventLoop()
    def on_download_finished():
        d_loop.quit()
        
    download_runner.run(download_task.run, game_data, selected_depots, dest_path).finished.connect(on_download_finished)
    d_loop.exec()
    
    cdp.update(100)
    cdp.stop()
    
    settings = get_settings()
    cli_mgr = CLITaskManager(settings, logger)
    cli_mgr.run_post_processing(game_data, download_task, dest_path)
    
    try:
        os.remove(zip_path)
    except:
        pass
        
    sys.exit(0)

if __name__ == "__main__":
    main()
