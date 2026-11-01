if(NOT DEFINED D2X_CONFIG_SOURCE OR NOT DEFINED D2X_CONFIG_DESTINATION)
  message(FATAL_ERROR "Debug AI config copy requires source and destination directories")
endif()

# Runtime copies are editable user settings. Supply defaults without replacing
# an existing file when an unrelated code change links the executable again.
foreach(config IN ITEMS debug-ai.json debug-ai-campaign.json)
  if(NOT EXISTS "${D2X_CONFIG_DESTINATION}/${config}")
    configure_file("${D2X_CONFIG_SOURCE}/${config}"
      "${D2X_CONFIG_DESTINATION}/${config}" COPYONLY)
  endif()
endforeach()
