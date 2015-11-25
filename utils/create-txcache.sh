#! /bin/sh

../../bitcoin-iterate/bitcoin-iterate -q --start=352304 --end=353025 --tx="%th %tF %tX" | ./add-to-txcache
