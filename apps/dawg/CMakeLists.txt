# The existing shell still lives in Source/App until application composition
# is migrated. Only its audio platform dependency changes in this step.
target_link_libraries(DAWg PRIVATE dawg_platform_juce)
