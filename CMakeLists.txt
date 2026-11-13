# EVCarHUD cho board ESP32-2432S028 ("Cheap Yellow Display" 2.8 inch). Build: xem README.md
cmake_minimum_required(VERSION 3.16)
# Phien ban firmware = noi dung version.txt (vd 2.4): trang evcarhud.com/admin tu doc so nay khi chon file .bin.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(evcarhud_cyd)
