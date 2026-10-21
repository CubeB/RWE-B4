# INSTALL-UX-001: RWE distribution UX, not original installer conformance.
if(WIN32)
    # INSTALL-UX-002: Explorer/shortcuts and SDL use this executable resource.
    enable_language(RC)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/RweIcon.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/rwe-icon.rc" @ONLY)
    target_sources(rwe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rwe-icon.rc")
    target_sources(rwe_setup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rwe-icon.rc")
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/Import Game Data.cmd" DESTINATION .)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/Import-GameData.ps1" DESTINATION setup)
    install(FILES "${PROJECT_SOURCE_DIR}/assets/b4.ico" DESTINATION .)
endif()

set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_VERSION "${RWE_GIT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "RobotWarEngine-${RWE_GIT_DESCRIPTION}-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VENDOR "Michael Heasell")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Robot War Engine - play using your Total Annihilation game data")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Robot War Engine")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Robot War Engine")
set(CPACK_OUTPUT_FILE_PREFIX "dist")
set(CPACK_PACKAGE_EXECUTABLES
    "rwe;Play Robot War Engine;launcher\\rwe-launcher;Multiplayer Lobby")
set(CPACK_NSIS_DISPLAY_NAME "Robot War Engine")
set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/b4.ico")
set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/b4.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "rwe.exe")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
set(CPACK_NSIS_WELCOME_TITLE "Welcome to Robot War Engine")
set(CPACK_NSIS_FINISH_TITLE "Robot War Engine is installed")
set(CPACK_NSIS_DEFINES [=[
!define MUI_WELCOMEPAGE_TEXT "Install Robot War Engine, then import game files from your own copy of Total Annihilation.$\r$\n$\r$\nThe setup assistant opens after installation. It can find your game automatically or let you choose its folder.$\r$\n$\r$\nYour Total Annihilation installation and RWE saved games stay in place."
!define MUI_FINISHPAGE_TEXT "Next, import your Total Annihilation game files. If you have already imported them, close the assistant and use Play Robot War Engine.$\r$\n$\r$\nYou can reopen Import Game Data from the Start menu at any time."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open game setup (recommended)"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenGameSetup
Function OpenGameSetup
  ExecShell "open" "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" '-NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\setup\Import-GameData.ps1"'
FunctionEnd
]=])
set(CPACK_NSIS_CREATE_ICONS_EXTRA [=[
CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Import Game Data.lnk" "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" '-NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\setup\Import-GameData.ps1"' "$INSTDIR\b4.ico" 0
]=])
set(CPACK_NSIS_DELETE_ICONS_EXTRA [=[
Delete "$SMPROGRAMS\$MUI_TEMP\Import Game Data.lnk"
]=])
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
CreateShortCut "$DESKTOP\Robot War Engine.lnk" "$INSTDIR\rwe.exe"
]=])
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
Delete "$DESKTOP\Robot War Engine.lnk"
]=])
include(CPack)
