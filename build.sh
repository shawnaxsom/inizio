#!/bin/bash
qmake Todour.pro && make && rm -rf /Applications/Todour.app/ && mv Todour.app/ /Applications/
