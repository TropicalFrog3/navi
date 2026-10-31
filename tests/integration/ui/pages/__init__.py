"""
Page Object Models for Navi UI integration tests.
"""

from .base_modal_page import BaseModalPage
from .cloud_settings_page import (
    CloudResetModal,
    CloudRestoreModal,
    CloudSettingsModal,
    CloudSettingsPage,
)
from .import_config_page import ConfigEditorPage, ImportConfigPage
from .library_details_page import (
    AppRestoreModal,
    GameManagerModal,
    LibraryDetailsPage,
    NaviModal,
    RemoveLuaModal,
    ResetSaveModal,
)
from .store_app_page import (
    DepotSelectionModal,
    ProviderConfigModal,
    StoreAppPage,
)

__all__ = [
    "BaseModalPage",
    "CloudResetModal",
    "CloudRestoreModal",
    "CloudSettingsModal",
    "CloudSettingsPage",
    "ConfigEditorPage",
    "ImportConfigPage",
    "LibraryDetailsPage",
    "GameManagerModal",
    "NaviModal",
    "AppRestoreModal",
    "RemoveLuaModal",
    "ResetSaveModal",
    "ProviderConfigModal",
    "DepotSelectionModal",
    "StoreAppPage",
]
