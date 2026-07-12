cmake_minimum_required(VERSION 3.25)

project(g_generals LANGUAGES C CXX)

if (RTS_BUILD_GENERALS_DOCS)
    find_package(Doxygen REQUIRED)
    doxygen_add_docs(g_docs Code)
endif()

# Set where the build results will end up
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Add main build targets
add_subdirectory(Code)

# If we are building on windows for windows, try and get the game install path from the registry.
if("${CMAKE_HOST_SYSTEM}" MATCHES "Windows" AND "${CMAKE_SYSTEM}" MATCHES "Windows")
    include(${CMAKE_SOURCE_DIR}/cmake/registry.cmake)

    # Check the EA App/CD registry path
    fetch_registry_value(
        "HKEY_LOCAL_MACHINE/SOFTWARE/Electronic Arts/EA Games/Generals"
        "InstallPath"
        RTS_INSTALL_PREFIX_GENERALS
        "Generals install path from registry")

    # Check the "First Decade" registry path
    fetch_registry_value(
        "HKEY_LOCAL_MACHINE/SOFTWARE/Electronic Arts/EA Games/Command and Conquer The First Decade"
        "gr_folder"
        RTS_INSTALL_PREFIX_GENERALS
        "Generals install path from registry")

    # Check the Steam registry path
    fetch_registry_value(
        "HKEY_LOCAL_MACHINE/SOFTWARE/Electronic Arts/EA Games/Generals"
        "installPath"
        RTS_INSTALL_PREFIX_GENERALS
        "Generals install path from registry")
endif()

if(RTS_INSTALL_PREFIX_GENERALS)
    install(TARGETS g_generals RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
    install(FILES $<TARGET_PDB_FILE:g_generals> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)

    # Compiled HLSL shaders for the programmable render path.
    if(EXISTS "${RTS_SHADER_OUT_DIR}")
        install(DIRECTORY "${RTS_SHADER_OUT_DIR}/" DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}/shaders")
    endif()

    # Loose game data: the INI patches under mod/. Installed with the executable because
    # they are the data half of features the executable carries -- without
    # Data\INI\Patch\HazardFields.ini, for instance, HazardFieldDecalClientUpdate is
    # configured onto nothing and the hazard-field effect is dead code. Patches only write
    # the settings they name (see mod/README.md), so this adds files rather than replacing
    # any shipped one, but it does change the INI CRC that replay headers carry: a client
    # built from this tree and a stock client cannot play together, by design.
    #
    # The tree is per game because a patch must name definitions that game actually
    # declares, and the loader stops the load if one does not exist. mod/GeneralsMD names
    # Generals-Challenge objects (NukeGeneral.ini) that base Generals has never had.
    #
    # Check where this actually lands before putting anything here: the registry lookups
    # above can resolve both install prefixes to the same folder (an EA App or First Decade
    # machine with only Zero Hour installed does exactly that), in which case a patch meant
    # for base Generals is deployed into a Zero Hour install and stops its load.
    if(EXISTS "${CMAKE_SOURCE_DIR}/mod/Generals/Data")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/mod/Generals/Data/" DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}/Data")
    endif()

    if(TARGET g_worldbuilder)
        install(TARGETS g_worldbuilder RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:g_worldbuilder> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET core_particleeditor)
        install(TARGETS core_particleeditor RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:core_particleeditor> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET core_debugwindow)
        install(TARGETS core_debugwindow RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:core_debugwindow> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET g_guiedit)
        install(TARGETS g_guiedit RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:g_guiedit> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET g_imagepacker)
        install(TARGETS g_imagepacker RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:g_imagepacker> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET g_mapcachebuilder)
        install(TARGETS g_mapcachebuilder RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:g_mapcachebuilder> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()

    if(TARGET g_w3dview)
        install(TARGETS g_w3dview RUNTIME DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}")
        install(FILES $<TARGET_PDB_FILE:g_w3dview> DESTINATION "${RTS_INSTALL_PREFIX_GENERALS}" OPTIONAL)
    endif()
else()
    message(STATUS "Generals install path not found. Registry keys for 'Generals' not found. "
                   "Generals targets will be built, but not installed. "
                   "You can specify the path manually by setting RTS_INSTALL_PREFIX_GENERALS.")
endif()
