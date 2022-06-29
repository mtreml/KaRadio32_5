#!/bin/bash
cd /z/esp-idf/
git pull
git submodule update --init --recursive
cd /z/examples/KaRadio32_4
