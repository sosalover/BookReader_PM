# Pocketmage 
This is a basic overview of pocketmage source code. This readme is intended to inform developers about developing apps for the pocketmage and contributing to the code. Feel free to expand this readme with any information that would be helpful for future developers.

### OTA_APPS

Pocketmage v3 can be compiled via platformio with different envelopes 

1. PM_V3 -> code intended to be main driver of the pocketmage.
2. OTA_APP -> framework for developing OTA_APPS for the pocketmage. The compiled binary and icon can be packaged as a .tar file and placed into apps/ for loading into one of the pocketmage's partitions. Please refer to https://www.youtube.com/watch?v=3Ytc-3-BbMM for more details about developing ota apps.
3. native -> environment for testing parts of the pocketmage code. Tests can be run with "pio test -e native" in the platformio CLI. Please refer to https://docs.platformio.org/en/latest/core/userguide/cmd_test.html to learn more.

### To Do:

1. Root out major bugs in beta.
2. publish code in lib/ to platformio registry to be used as an external library

# Guides:

## OTA_APPS:

- ota apps should be built with the OTA_APP environment set in platformio.ini line #14.
- ota app entry points are defined in APP_TEMPLATE.cpp.